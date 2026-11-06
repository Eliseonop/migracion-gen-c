#pragma once

// RoundedImage (QML, GenClip.Preview): una imagen del disco que llena su rectángulo sin deformarse,
// recortada al centro (`object-fit: cover`), con las esquinas de ARRIBA redondeadas: el recuadro de
// la miniatura de ProjectCard.qml, que es la parte de arriba de una tarjeta redondeada. Qt Quick no
// recorta con esquinas redondeadas sin los efectos de Qt 6.5 (MultiEffect), y el studio tiene que
// compilar con Qt 6.4, así que se pinta con QPainter.

#include <QImage>
#include <QQuickPaintedItem>
#include <QString>

namespace gc::studio {

class RoundedImage : public QQuickPaintedItem {
    Q_OBJECT
    // La ruta del archivo, no una URL. Vacía, o si no se puede leer, no pinta nada.
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(qreal radius READ radius WRITE setRadius NOTIFY radiusChanged)
    // Hay imagen que pintar (se leyó bien).
    Q_PROPERTY(bool loaded READ loaded NOTIFY sourceChanged)

public:
    explicit RoundedImage(QQuickItem* parent = nullptr);

    [[nodiscard]] QString source() const { return source_; }
    void setSource(const QString& source);
    [[nodiscard]] qreal radius() const { return radius_; }
    void setRadius(qreal radius);
    [[nodiscard]] bool loaded() const { return !image_.isNull(); }

    void paint(QPainter* painter) override;

signals:
    void sourceChanged();
    void radiusChanged();

private:
    QString source_;
    qreal radius_ = 0;
    QImage image_;
    QImage scaled_; // image_ a los píxeles de la pantalla, para no reducirla en cada pintado
};

} // namespace gc::studio
