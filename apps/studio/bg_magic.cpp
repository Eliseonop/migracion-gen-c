#include "bg_magic.hpp"

#include <QColor>
#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <algorithm>
#include <cmath>

#include "editor_model.hpp"
#include "gc/core/bg_ui.hpp"

namespace gc::studio {

namespace {

const QColor kFill(22, 196, 222, 133);       // selección (cian de CapCut)
const QColor kEdge(165, 243, 252, 230);      // borde fino de la selección
const QColor kLineKeep(22, 196, 222, 217);   // trazo inteligente sin respuesta
const QColor kEraseFill(239, 68, 68, 89);    // rastro del borrador
const QColor kEraseEdge(239, 68, 68, 242);
constexpr int kEdgePx = 2;
constexpr double kMaxSide = 960; // lienzo sin selección de SAM (solo pincel normal)

QImage blank(int w, int h) {
    QImage out(w, h, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    return out;
}

// El trazo con sus cortes (`m`). Un trazo de un solo punto se alarga una centésima para que el extremo pinte.
QPainterPath trace(const Json& e, int w, int h) {
    QPainterPath path;
    const Json points = e.value("points", Json::array());
    bool start = true;
    for (const Json& p : points) {
        const QPointF at(p.value("x", 0.0) * w, p.value("y", 0.0) * h);
        if (p.contains("m")) {
            start = true;
        }
        if (start) {
            path.moveTo(at);
            start = false;
        } else {
            path.lineTo(at);
        }
    }
    if (points.size() == 1) {
        path.lineTo(path.currentPosition() + QPointF(0.01, 0));
    }
    return path;
}

void stroke_line(QPainter& painter, const Json& e, int w, int h, const QColor& color, double extra = 0) {
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(color, std::max(1.0, e.value("size", 0.08) * h) + extra, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawPath(trace(e, w, h));
}

// Rastro del borrador: interior rojo translúcido con borde rojo.
void erase_trail(QPainter& out, const Json& e, int w, int h) {
    QImage tmp = blank(w, h);
    QPainter t(&tmp);
    t.setRenderHint(QPainter::Antialiasing);
    stroke_line(t, e, w, h, kEraseEdge, kEdgePx * 2);
    t.setCompositionMode(QPainter::CompositionMode_DestinationOut);
    stroke_line(t, e, w, h, Qt::black);
    t.setCompositionMode(QPainter::CompositionMode_SourceOver);
    stroke_line(t, e, w, h, kEraseFill);
    t.end();
    out.drawImage(0, 0, tmp);
}

} // namespace

void BgMagic::setMask(const std::string& clip, std::int64_t frame, const QImage& mask, const Json& marks) {
    if (mask.isNull()) {
        return;
    }
    Entry entry{frame, {}, mask.convertToFormat(QImage::Format_ARGB32_Premultiplied)};
    for (const Json& m : marks) {
        entry.covered.insert(m.dump());
    }
    masks_[clip] = std::move(entry);
}

bool BgMagic::covers(const std::string& clip, std::int64_t frame, const Json& smart) const {
    const auto it = masks_.find(clip);
    if (it == masks_.end() || it->second.frame != frame || it->second.covered.size() != smart.size()) {
        return false;
    }
    return std::ranges::all_of(smart, [&](const Json& m) { return it->second.covered.contains(m.dump()); });
}

void BgMagic::clear(const std::string& clip) { masks_.erase(clip); }

void BgMagic::paintEdits(QImage& target, const Json& edits) {
    const int w = target.width();
    const int h = target.height();
    QPainter painter(&target);
    painter.setRenderHint(QPainter::Antialiasing);
    for (const char* op : {"keep", "erase"}) {
        painter.setCompositionMode(std::string_view(op) == "keep" ? QPainter::CompositionMode_SourceOver
                                                                  : QPainter::CompositionMode_DestinationOut);
        for (const Json& e : edits) {
            if (e.value("op", "") == op) {
                stroke_line(painter, e, w, h, Qt::white);
            }
        }
    }
}

QImage BgMagic::compose(const std::string& clip, std::int64_t frame, const Json& marks, bool live, double src_w,
                        double src_h) const {
    if (!(src_w > 0) || !(src_h > 0)) {
        return {};
    }
    const auto it = masks_.find(clip);
    const Entry* mask = it != masks_.end() && it->second.frame == frame ? &it->second : nullptr;
    // selectionLayers: pincel normal, y los trazos inteligentes que aún no tienen respuesta.
    Json manual = Json::array();
    Json pending = Json::array();
    for (const Json& e : marks) {
        if (core::bg_manual_mark(e)) {
            manual.push_back(e);
        } else if (!mask || !mask->covered.contains(e.dump())) {
            pending.push_back(e);
        }
    }
    const bool trail = live && !marks.empty() && core::bg_manual_mark(marks.back()) && marks.back().value("op", "") == "erase";
    if (!mask && manual.empty() && pending.empty()) {
        return {};
    }
    const double scale = std::min(1.0, kMaxSide / std::max(src_w, src_h));
    const int w = mask ? mask->mask.width() : std::max(2, static_cast<int>(std::lround(src_w * scale)));
    const int h = mask ? mask->mask.height() : std::max(2, static_cast<int>(std::lround(src_h * scale)));

    // Selección = SAM ∪ pincel − borrador (alfa blanco).
    QImage sel = mask ? mask->mask : blank(w, h);
    if (!manual.empty()) {
        paintEdits(sel, manual);
    }
    // Relleno cian dentro de la selección.
    QImage out = blank(w, h);
    QPainter o(&out);
    o.setRenderHint(QPainter::Antialiasing);
    o.fillRect(out.rect(), kFill);
    o.setCompositionMode(QPainter::CompositionMode_DestinationIn);
    o.drawImage(0, 0, sel);
    // Borde fino: selección − selección erosionada, en su color.
    QImage ring = sel;
    {
        QPainter r(&ring);
        r.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        for (const QPoint d : {QPoint(-kEdgePx, 0), QPoint(kEdgePx, 0), QPoint(0, -kEdgePx), QPoint(0, kEdgePx)}) {
            r.drawImage(d, sel);
        }
    }
    QImage edge = sel;
    {
        QPainter t(&edge);
        t.setCompositionMode(QPainter::CompositionMode_DestinationOut);
        t.drawImage(0, 0, ring);
        t.setCompositionMode(QPainter::CompositionMode_SourceIn);
        t.fillRect(edge.rect(), kEdge);
    }
    o.setCompositionMode(QPainter::CompositionMode_SourceOver);
    o.drawImage(0, 0, edge);
    // Lo que aún no tiene respuesta de SAM, y el rastro del borrador.
    for (const Json& e : pending) {
        if (e.value("op", "") == "erase") {
            erase_trail(o, e, w, h);
        } else {
            stroke_line(o, e, w, h, kLineKeep);
        }
    }
    if (trail) {
        erase_trail(o, marks.back(), w, h);
    }
    o.end();
    return out;
}

BgMagicOverlay::BgMagicOverlay(QQuickItem* parent) : QQuickPaintedItem(parent) { setAntialiasing(true); }

QObject* BgMagicOverlay::model() const { return model_; }

void BgMagicOverlay::setModel(QObject* model) {
    auto* next = qobject_cast<EditorModel*>(model);
    if (next == model_) {
        return;
    }
    if (model_) {
        disconnect(model_, nullptr, this, nullptr);
    }
    model_ = next;
    if (model_) {
        connect(model_, &EditorModel::bgMagicChanged, this, [this] { update(); });
    }
    emit modelChanged();
    update();
}

void BgMagicOverlay::setFrame(const QRectF& frame) {
    if (frame != frame_) {
        frame_ = frame;
        emit frameChanged();
        update();
    }
}

void BgMagicOverlay::paint(QPainter* painter) {
    if (model_) {
        model_->paintBgMagic(*painter, frame_);
    }
}

} // namespace gc::studio
