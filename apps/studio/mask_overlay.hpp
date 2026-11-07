#pragma once

// MaskOverlay (QML, GenClip.Preview): la guía para editar la máscara sobre la vista previa
// (drawMaskOverlay de render/canvas.js): su contorno, la caja girada con la línea al tirador de giro y
// los tiradores (cuadrados; el de giro, redondo; el de la pluma, ámbar). Pinta en los píxeles del lienzo
// del editor de React (los del item × min(2, densidad)), así que los grosores y los tiradores miden lo mismo.

#include <QQuickPaintedItem>
#include <QRectF>
#include <QVariantMap>

namespace gc::studio {

class MaskOverlay : public QQuickPaintedItem {
    Q_OBJECT
    // La máscara normalizada, en el cabezal (vacía: no pinta nada).
    Q_PROPERTY(QVariantMap mask READ mask WRITE setMask NOTIFY maskChanged)
    // El recuadro de salida dentro del item (el frameRect de FramePreview).
    Q_PROPERTY(QRectF frame READ frame WRITE setFrame NOTIFY frameChanged)
    // La escala del lienzo de React: min(2, densidad de la pantalla).
    Q_PROPERTY(qreal canvasScale READ canvasScale NOTIFY frameChanged)

public:
    explicit MaskOverlay(QQuickItem* parent = nullptr);

    [[nodiscard]] QVariantMap mask() const { return mask_; }
    void setMask(const QVariantMap& mask);
    [[nodiscard]] QRectF frame() const { return frame_; }
    void setFrame(const QRectF& frame);
    [[nodiscard]] qreal canvasScale() const;

    void paint(QPainter* painter) override;

signals:
    void maskChanged();
    void frameChanged();

private:
    QVariantMap mask_;
    QRectF frame_;
};

} // namespace gc::studio
