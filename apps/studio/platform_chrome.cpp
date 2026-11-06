#include "platform_chrome.hpp"

#include <QPainter>
#include <QQuickWindow>
#include <QVariantMap>
#include <cmath>
#include <string>

#ifdef GC_STUDIO_PREVIEW
#include "gc/render/canvas.hpp"
#include "gc/render/platform.hpp"
#endif

namespace gc::studio {

PlatformChrome::PlatformChrome(QQuickItem* parent) : QQuickPaintedItem(parent) {
    setAntialiasing(true);
    connect(this, &QQuickItem::widthChanged, this, [this] { update(); });
    connect(this, &QQuickItem::heightChanged, this, [this] { update(); });
}

void PlatformChrome::setKind(const QString& kind) {
    if (kind == kind_) {
        return;
    }
    kind_ = kind;
    emit kindChanged();
    update();
}

void PlatformChrome::paint(QPainter* painter) {
#ifdef GC_STUDIO_PREVIEW
    const double dpr = window() != nullptr ? window()->effectiveDevicePixelRatio() : 1.0;
    const int w = static_cast<int>(std::lround(width() * dpr));
    const int h = static_cast<int>(std::lround(height() * dpr));
    const std::string kind = kind_.toStdString();
    if (w <= 0 || h <= 0 || render::overlay_label(kind).empty() || kind == "none") {
        image_ = {};
        return;
    }
    if (image_.width() != w || image_.height() != h || drawn_ != kind_) {
        auto canvas = render::Canvas::make(w, h);
        if (!canvas) {
            image_ = {};
            return;
        }
        canvas->clear({0, 0, 0, 0});
        render::draw_platform_chrome(*canvas, 0, 0, w, h, kind);
        const render::Image shot = canvas->snapshot();
        // RGBA sin premultiplicar, como el lienzo; copia, que `shot` se va.
        image_ = QImage(shot.pixels.data(), shot.width, shot.height, static_cast<qsizetype>(shot.width) * 4,
                        QImage::Format_RGBA8888)
                     .copy();
        drawn_ = kind_;
    }
    painter->drawImage(QRectF(0, 0, width(), height()), image_);
#else
    (void)painter;
#endif
}

QVariantList PlatformChrome::overlays() {
    QVariantList out;
#ifdef GC_STUDIO_PREVIEW
    for (const auto& o : render::kPlatformOverlays) {
        out << QVariantMap{{"id", QString::fromUtf8(o.id.data(), static_cast<qsizetype>(o.id.size()))},
                           {"label", QString::fromUtf8(o.label.data(), static_cast<qsizetype>(o.label.size()))}};
    }
#endif
    return out;
}

} // namespace gc::studio
