#include "mask_overlay.hpp"

#include <QBrush>
#include <QColor>
#include <QPainter>
#include <QPen>
#include <QPolygonF>
#include <QQuickWindow>
#include <algorithm>
#include <numbers>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "gc/core/mask_ui.hpp"
#include "gc/core/masks.hpp"
#include "qt_json.hpp"

namespace gc::studio {

namespace {

constexpr double kDeg = 180 / std::numbers::pi;

QPolygonF polygon(const std::vector<core::Point>& pts) {
    QPolygonF out;
    out.reserve(static_cast<qsizetype>(pts.size()));
    for (const core::Point& p : pts) {
        out << QPointF(p.x, p.y);
    }
    return out;
}

// strokeMaskShape de lib/clipMask.js, ya trasladado al centro de la máscara y girado.
void stroke_shape(QPainter& painter, const Json& mask, const core::MaskGeometry& g) {
    const std::string type = mask.value("type", "circle");
    const double big = (std::max(g.hw, g.hh) * 4) + (g.ref * 4);
    if (type == "brush" || type == "text") {
        painter.drawRect(QRectF(-g.hw, -g.hh, g.hw * 2, g.hh * 2));
    } else if (type == "linear") {
        painter.drawLine(QPointF(-big, 0), QPointF(big, 0));
    } else if (type == "film") {
        painter.drawLine(QPointF(-big, -g.hh), QPointF(big, -g.hh));
        painter.drawLine(QPointF(-big, g.hh), QPointF(big, g.hh));
    } else if (type == "rectangle") { // pathRect: esquinas redondeadas si el radio pasa de medio píxel
        const double half = std::min(g.hw, g.hh);
        const double r = std::min(mask.value("radius", 0.0) * half * 2, half);
        const QRectF box(-g.hw, -g.hh, g.hw * 2, g.hh * 2);
        if (r > 0.5) {
            painter.drawRoundedRect(box, r, r);
        } else {
            painter.drawRect(box);
        }
    } else if (type == "star") {
        painter.drawPolygon(polygon(core::star_points(g.hw, g.hh)));
    } else if (type == "heart") {
        painter.drawPolygon(polygon(core::heart_points(g.hw, g.hh)));
    } else { // el círculo, y lo que no tiene forma propia (MASK_SHAPES[type] || circle)
        painter.drawEllipse(QPointF(0, 0), g.hw, g.hh);
    }
}

} // namespace

MaskOverlay::MaskOverlay(QQuickItem* parent) : QQuickPaintedItem(parent) { setAntialiasing(true); }

void MaskOverlay::setMask(const QVariantMap& mask) {
    if (mask == mask_) {
        return;
    }
    mask_ = mask;
    emit maskChanged();
    update();
}

void MaskOverlay::setFrame(const QRectF& frame) {
    if (frame == frame_) {
        return;
    }
    frame_ = frame;
    emit frameChanged();
    update();
}

qreal MaskOverlay::canvasScale() const {
    return std::min<qreal>(2, window() != nullptr ? window()->effectiveDevicePixelRatio() : 1);
}

void MaskOverlay::paint(QPainter* painter) {
    if (mask_.isEmpty() || frame_.isEmpty()) {
        return;
    }
    const Json mask = core::normalize_mask(to_json(mask_));
    const double k = canvasScale();
    const core::Rect frame{frame_.x() * k, frame_.y() * k, frame_.width() * k, frame_.height() * k};
    painter->setRenderHint(QPainter::Antialiasing);
    painter->scale(1 / k, 1 / k);
    painter->setBrush(Qt::NoBrush);

    const QColor sky(56, 189, 248);
    const core::MaskGeometry g = core::mask_geometry(mask, frame);
    painter->save();
    painter->translate(g.cx, g.cy);
    painter->rotate(g.rot * kDeg);
    QColor line = sky;
    line.setAlphaF(0.95F);
    painter->setPen(QPen(line, 2));
    stroke_shape(*painter, mask, g);
    painter->restore();

    // La caja de los tiradores, a trazos, y la línea hasta el de giro.
    const core::MaskHandles h = core::mask_handles(mask, frame);
    const core::MaskGeometry& box = h.box;
    painter->save();
    painter->translate(box.cx, box.cy);
    painter->rotate(box.rot * kDeg);
    QColor faint = sky;
    faint.setAlphaF(0.45F);
    QPen dashed(faint, 1);
    dashed.setDashPattern({6, 5});
    painter->setPen(dashed);
    painter->drawRect(QRectF(-box.hw, -box.hh, box.hw * 2, box.hh * 2));
    painter->setPen(QPen(faint, 1));
    painter->drawLine(QPointF(0, -box.hh), QPointF(0, -box.hh - std::max(16.0, frame.h * 0.022)));
    painter->restore();

    // Los tiradores, en el orden de maskHandles (sin el de mover): r, b, br, rot y fea.
    constexpr double hs = 5;
    painter->setPen(QPen(Qt::white, 1.4));
    for (const auto& [key, p] : {std::pair{"r", h.r}, std::pair{"b", h.b}, std::pair{"br", h.br},
                                 std::pair{"rot", h.rot}, std::pair{"fea", h.fea}}) {
        const std::string_view name = key;
        painter->setBrush(name == "fea" ? QColor(0xfb, 0xbf, 0x24) : sky);
        if (name == "rot") {
            painter->drawEllipse(QPointF(p.x, p.y), hs + 1, hs + 1);
        } else {
            painter->drawRect(QRectF(p.x - hs, p.y - hs, hs * 2, hs * 2));
        }
    }
}

} // namespace gc::studio
