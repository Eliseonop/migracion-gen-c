#include "rounded_image.hpp"

#include <QBrush>
#include <QPainter>
#include <QPainterPath>
#include <QQuickWindow>
#include <QTransform>
#include <algorithm>

namespace gc::studio {

RoundedImage::RoundedImage(QQuickItem* parent) : QQuickPaintedItem(parent) {
    setAntialiasing(true);
}

void RoundedImage::setSource(const QString& source) {
    if (source == source_) {
        return;
    }
    source_ = source;
    image_ = source.isEmpty() ? QImage() : QImage(source);
    scaled_ = QImage();
    emit sourceChanged();
    update();
}

void RoundedImage::setRadius(qreal radius) {
    if (radius == radius_) {
        return;
    }
    radius_ = radius;
    emit radiusChanged();
    update();
}

void RoundedImage::paint(QPainter* painter) {
    const qreal w = width();
    const qreal h = height();
    if (image_.isNull() || w <= 0 || h <= 0) {
        return;
    }
    // Reducida una vez a los píxeles que ocupa (con la densidad de la pantalla) y con un buen filtro:
    // el patrón del pincel solo interpola entre píxeles vecinos.
    const qreal dpr = window() != nullptr ? window()->effectiveDevicePixelRatio() : 1.0;
    const qreal cover = std::max(w * dpr / image_.width(), h * dpr / image_.height());
    const QSize want(std::max(1, qRound(image_.width() * cover)), std::max(1, qRound(image_.height() * cover)));
    if (scaled_.size() != want) {
        scaled_ = image_.scaled(want, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    // Centrada: lo que sobra de un lado y del otro se queda fuera.
    QBrush brush(scaled_);
    brush.setTransform(QTransform::fromTranslate((w - (scaled_.width() / dpr)) / 2, (h - (scaled_.height() / dpr)) / 2)
                           .scale(1 / dpr, 1 / dpr));
    const qreal r = std::clamp(radius_, 0.0, std::min(w, h) / 2);
    QPainterPath path;
    path.moveTo(0, h);
    path.lineTo(0, r);
    path.arcTo(0, 0, 2 * r, 2 * r, 180, -90);
    path.lineTo(w - r, 0);
    path.arcTo(w - (2 * r), 0, 2 * r, 2 * r, 90, -90);
    path.lineTo(w, h);
    path.closeSubpath();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setRenderHint(QPainter::SmoothPixmapTransform);
    painter->fillPath(path, brush);
}

} // namespace gc::studio
