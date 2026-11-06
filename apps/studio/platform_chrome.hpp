#pragma once

// PlatformChrome (QML, GenClip.Preview): el marco de TikTok o YouTube Shorts encima de la vista
// previa (drawPlatformChrome de canvas.js, que lo pinta sobre el recuadro exportable). Solo vista
// previa: no se exporta. Se dibuja con gc::render::draw_platform_chrome a los píxeles de la pantalla
// y solo de nuevo si cambia el tamaño o el marco. Sin gc_render (sin la vista previa), nada.

#include <QImage>
#include <QQuickPaintedItem>
#include <QString>
#include <QVariantList>

namespace gc::studio {

class PlatformChrome : public QQuickPaintedItem {
    Q_OBJECT
    // "none", "tiktok" o "shorts" (PLATFORM_OVERLAYS); otro, como "none".
    Q_PROPERTY(QString kind READ kind WRITE setKind NOTIFY kindChanged)
    // PLATFORM_OVERLAYS para el selector del visor: [{id, label}], vacía sin gc_render.
    Q_PROPERTY(QVariantList overlays READ overlays CONSTANT)

public:
    explicit PlatformChrome(QQuickItem* parent = nullptr);

    [[nodiscard]] QString kind() const { return kind_; }
    void setKind(const QString& kind);
    // Lo último que se pintó, a los píxeles de la pantalla (vacía si no hay marco). Para los tests.
    [[nodiscard]] QImage image() const { return image_; }

    void paint(QPainter* painter) override;

    [[nodiscard]] static QVariantList overlays();

signals:
    void kindChanged();

private:
    QString kind_ = "none";
    QImage image_;
    QString drawn_; // el marco de image_
};

} // namespace gc::studio
