#include "crop_editor.hpp"

#include <algorithm>

#include "gc/core/clip_layout_ui.hpp"

namespace gc::studio {

namespace {

constexpr double kHit = 14; // tolerancia de los tiradores (px)

const std::pair<const char*, const char*> kRatios[] = {
    {"free", "Libre"}, {"original", "Original"}, {"output", "Proyecto"}, {"16:9", "16:9"},
    {"9:16", "9:16"},  {"1:1", "1:1"},           {"4:3", "4:3"},         {"3:4", "3:4"},
    {"4:5", "4:5"},    {"2.35:1", "2.35:1"}};

Qt::CursorShape cursor_of(const std::string& css) {
    if (css == "nwse-resize") {
        return Qt::SizeFDiagCursor;
    }
    if (css == "nesw-resize") {
        return Qt::SizeBDiagCursor;
    }
    return css == "ew-resize"   ? Qt::SizeHorCursor
           : css == "ns-resize" ? Qt::SizeVerCursor
                                : Qt::SizeAllCursor;
}

core::CropWindow clamped(const core::CropWindow& c) {
    return core::clamp_crop(c.cx, c.cy, c.wf, c.hf);
}

} // namespace

QVariantList CropEditor::handles() const {
    QVariantList out;
    for (const auto& h : core::crop_handle_norms(crop_)) {
        out << QVariantMap{{"hx", h.hx}, {"hy", h.hy}, {"x", h.x}, {"y", h.y}};
    }
    return out;
}

QVariantList CropEditor::ratios() {
    QVariantList out;
    for (const auto& [id, label] : kRatios) {
        out << QVariantMap{{"id", id}, {"label", label}};
    }
    return out;
}

void CropEditor::setRotation(double deg) {
    const double next = std::clamp(deg, -180.0, 180.0);
    if (next != rotation_) {
        rotation_ = next;
        emit cropChanged();
    }
}

void CropEditor::load(const QVariantMap& start) {
    src_aspect_ = start.value("srcAspect", 16.0 / 9.0).toDouble();
    out_aspect_ = start.value("outAspect", 16.0 / 9.0).toDouble();
    crop_ = clamped(
        {start["cx"].toDouble(), start["cy"].toDouble(), start["wf"].toDouble(), start["hf"].toDouble()});
    rotation_ = start["rotation"].toDouble();
    ratio_ = "free";
    grab_.reset();
    emit cropChanged();
}

bool CropEditor::press(double nx, double ny, double w, double h) {
    const auto handle = core::crop_handle_at(nx, ny, crop_, w, h, kHit);
    if (!handle && !core::inside_crop(nx, ny, crop_)) {
        return false;
    }
    grab_ = Grab{crop_, nx, ny, handle};
    return true;
}

void CropEditor::drag(double nx, double ny) {
    if (!grab_) {
        return;
    }
    const auto& g = *grab_;
    if (!g.handle) {
        set(clamped({g.start.cx + nx - g.nx, g.start.cy + ny - g.ny, g.start.wf, g.start.hf}));
        return;
    }
    const auto [hx, hy] = *g.handle;
    const auto rn = ratio_norm();
    set(clamped(rn ? core::resize_crop_locked(g.start, hx, hy, nx, ny, *rn, 0.05)
                   : core::resize_crop_free(g.start, hx, hy, nx, ny)));
}

int CropEditor::cursorAt(double nx, double ny, double w, double h) const {
    if (const auto handle = core::crop_handle_at(nx, ny, crop_, w, h, kHit)) {
        return cursor_of(core::crop_cursor(handle->first, handle->second));
    }
    return core::inside_crop(nx, ny, crop_) ? Qt::SizeAllCursor : Qt::ArrowCursor;
}

void CropEditor::pickRatio(const QString& id) {
    ratio_ = id;
    if (const auto rn = ratio_norm()) {
        crop_ = core::fit_crop_ratio(crop_, *rn);
    }
    emit cropChanged();
}

void CropEditor::reset() {
    ratio_ = "free";
    crop_ = {};
    rotation_ = 0;
    emit cropChanged();
}

void CropEditor::set(const core::CropWindow& crop) {
    crop_ = crop;
    emit cropChanged();
}

std::optional<double> CropEditor::ratio_norm() const {
    return core::crop_ratio_norm(ratio_.toStdString(), src_aspect_, out_aspect_);
}

} // namespace gc::studio
