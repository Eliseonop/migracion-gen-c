#pragma once

// FrameImage (QML, GenClip.Preview): pinta una QImage que le da el modelo (el fotograma del vídeo en
// el modal Recortar), estirada a su rectángulo (quien lo coloca ya le da el aspecto de la imagen).
// Vacía, no pinta nada.

#include <QImage>
#include <QQuickPaintedItem>

namespace gc::studio {

class FrameImage : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QImage image READ image WRITE setImage NOTIFY imageChanged)

public:
    explicit FrameImage(QQuickItem* parent = nullptr);

    [[nodiscard]] QImage image() const { return image_; }
    void setImage(const QImage& image);

    void paint(QPainter* painter) override;

signals:
    void imageChanged();

private:
    QImage image_;
    QImage scaled_; // image_ a los píxeles de la pantalla, para no reducirla en cada pintado
};

} // namespace gc::studio
