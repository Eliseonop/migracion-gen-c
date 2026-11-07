#include "frame_image.hpp"

#include <QPainter>
#include <QQuickWindow>
#include <algorithm>

namespace gc::studio {

FrameImage::FrameImage(QQuickItem* parent) : QQuickPaintedItem(parent) {}

void FrameImage::setImage(const QImage& image) {
    image_ = image;
    scaled_ = QImage();
    emit imageChanged();
    update();
}

void FrameImage::paint(QPainter* painter) {
    const qreal w = width();
    const qreal h = height();
    if (image_.isNull() || w <= 0 || h <= 0) {
        return;
    }
    // Reducida una vez a los píxeles que ocupa (con la densidad de la pantalla) y con un buen filtro:
    // drawImage escalando solo interpola entre píxeles vecinos.
    const qreal dpr = window() != nullptr ? window()->effectiveDevicePixelRatio() : 1.0;
    const QSize want(std::max(1, qRound(w * dpr)), std::max(1, qRound(h * dpr)));
    if (scaled_.size() != want) {
        scaled_ = image_.scaled(want, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    }
    painter->drawImage(QRectF(0, 0, w, h), scaled_);
}

} // namespace gc::studio
