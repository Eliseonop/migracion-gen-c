#pragma once

// La SELECCIÓN de la Eliminación personalizada sobre la vista previa, como la enseña CapCut (bgMagic.js): relleno
// cian dentro de lo elegido, un borde fino, la línea de los trazos inteligentes que SAM aún no ha contestado y
// el rastro rojo del borrador. Se compone en el espacio de la FUENTE y se dibuja por la misma transformación
// que el clip, así que sigue su recorte, su pose y su escala.

#include <QImage>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QRectF>
#include <map>
#include <set>
#include <string>

#include "gc/core/json.hpp"

namespace gc::studio {

class EditorModel;

class BgMagic {
public:
    // setMagicMask: la selección de SAM de un fotograma (alfa = máscara) y qué trazos cubre.
    void setMask(const std::string& clip, std::int64_t frame, const QImage& mask, const Json& marks);
    // magicCovers: ¿la selección guardada es la de exactamente estos trazos?
    [[nodiscard]] bool covers(const std::string& clip, std::int64_t frame, const Json& smart) const;
    void clear(const std::string& clip);
    void clearAll() { masks_.clear(); }

    // magicOverlayCanvas: la imagen del overlay (aspecto de la fuente) para el fotograma, o nula si no hay nada
    // que enseñar. `live`: hay un trazo en curso.
    [[nodiscard]] QImage compose(const std::string& clip, std::int64_t frame, const Json& marks, bool live, double src_w,
                                 double src_h) const;

    // paintEdits: pinta las correcciones en el alfa de `target` (primero conservar, luego eliminar).
    static void paintEdits(QImage& target, const Json& edits);

private:
    struct Entry {
        std::int64_t frame = 0;
        std::set<std::string> covered;
        QImage mask;
    };
    std::map<std::string, Entry> masks_;
};

// BgMagicOverlay (QML, GenClip.Preview): pinta lo que da el modelo (EditorModel::paintBgMagic).
class BgMagicOverlay : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QObject* model READ model WRITE setModel NOTIFY modelChanged)
    // El recuadro de salida dentro del item.
    Q_PROPERTY(QRectF frame READ frame WRITE setFrame NOTIFY frameChanged)

public:
    explicit BgMagicOverlay(QQuickItem* parent = nullptr);
    [[nodiscard]] QObject* model() const;
    void setModel(QObject* model);
    [[nodiscard]] QRectF frame() const { return frame_; }
    void setFrame(const QRectF& frame);
    void paint(QPainter* painter) override;

signals:
    void modelChanged();
    void frameChanged();

private:
    QPointer<EditorModel> model_;
    QRectF frame_;
};

} // namespace gc::studio
