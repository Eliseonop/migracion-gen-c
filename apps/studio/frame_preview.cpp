#include "frame_preview.hpp"

#include <QQuickWindow>
#include <QSGImageNode>
#include <QSGTexture>
#include <QtQml/qqml.h>
#include <algorithm>

#include "rounded_image.hpp"

namespace gc::studio {

FramePreview::FramePreview(QQuickItem* parent) : QQuickItem(parent) {
    setFlag(ItemHasContents, true);
    connect(this, &QQuickItem::windowChanged, this, [this] { report_size(); }); // su densidad de píxeles
}

void FramePreview::report_size() {
    if (source_) {
        const double dpr = window() != nullptr ? window()->effectiveDevicePixelRatio() : 1.0;
        source_->setDisplaySize(qRound(width() * dpr), qRound(height() * dpr));
    }
}

void FramePreview::setSource(QObject* source) {
    auto* next = qobject_cast<PreviewSource*>(source);
    if (next == source_) {
        return;
    }
    if (source_) {
        disconnect(source_, nullptr, this, nullptr);
    }
    source_ = next;
    if (source_) {
        connect(source_, &PreviewSource::frameChanged, this, &FramePreview::take_frame);
    }
    report_size();
    take_frame();
    emit sourceChanged();
}

void FramePreview::take_frame() {
    const QSize before = image_.size();
    image_ = source_ ? source_->image() : QImage();
    dirty_ = true;
    if (image_.size() != before) {
        emit frameRectChanged();
    }
    update();
}

QRectF FramePreview::frameRect() const {
    if (image_.isNull() || width() <= 0 || height() <= 0) {
        return {};
    }
    // Encajado: la escala que cabe entera, centrado.
    const double scale = std::min(width() / image_.width(), height() / image_.height());
    const double w = image_.width() * scale;
    const double h = image_.height() * scale;
    return {(width() - w) / 2, (height() - h) / 2, w, h};
}

void FramePreview::geometryChange(const QRectF& next, const QRectF& previous) {
    QQuickItem::geometryChange(next, previous);
    if (next.size() != previous.size()) {
        report_size(); // la escala de la vista previa depende de cuánto se ve
        emit frameRectChanged();
        update();
    }
}

// En el hilo del render, con el de la interfaz parado (por eso puede leer image_ y dirty_).
QSGNode* FramePreview::updatePaintNode(QSGNode* old, UpdatePaintNodeData* /*data*/) {
    auto* node = dynamic_cast<QSGImageNode*>(old);
    const QRectF rect = frameRect();
    const auto drop = [&] -> QSGNode* { // como QQuickImage: sin nada que pintar, sin nodo
        delete node; // NOLINT(cppcoreguidelines-owning-memory): el grafo de escena nos lo devuelve para soltarlo
        dirty_ = true;
        return nullptr;
    };
    if (image_.isNull() || rect.isEmpty() || window() == nullptr) {
        return drop();
    }
    if (node == nullptr) {
        node = window()->createImageNode();
        node->setOwnsTexture(true);
        node->setFiltering(QSGTexture::Linear);
        node->setMipmapFiltering(QSGTexture::Linear); // al reducir mucho el fotograma, sin dientes
        dirty_ = true;
    }
    if (dirty_) {
        // Su propia textura (no el atlas: un fotograma es grande), con mipmaps.
        QSGTexture* texture = window()->createTextureFromImage(image_, QQuickWindow::TextureHasMipmaps);
        if (texture == nullptr) {
            return drop();
        }
        node->setTexture(texture);
        dirty_ = false;
    }
    node->setRect(rect);
    return node;
}

void register_qml_types() {
    qmlRegisterType<FramePreview>("GenClip.Preview", 1, 0, "FramePreview");
    qmlRegisterType<RoundedImage>("GenClip.Preview", 1, 0, "RoundedImage");
    qmlRegisterUncreatableType<PreviewSource>("GenClip.Preview", 1, 0, "PreviewSource",
                                              "lo crea el editor (EditorModel.preview)");
}

} // namespace gc::studio
