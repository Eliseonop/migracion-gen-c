#pragma once

// FramePreview (QML, GenClip.Preview): pinta el último fotograma de un PreviewSource, encajado en
// su rectángulo sin deformarlo y centrado. Sube el fotograma a una textura solo cuando cambia.

#include <QImage>
#include <QPointer>
#include <QQuickItem>

#include "preview_source.hpp"

namespace gc::studio {

class FramePreview : public QQuickItem {
    Q_OBJECT
    Q_PROPERTY(QObject* source READ source WRITE setSource NOTIFY sourceChanged)
    // Dónde queda el fotograma dentro del item (en sus coordenadas), para lo que se dibuje encima.
    Q_PROPERTY(QRectF frameRect READ frameRect NOTIFY frameRectChanged)

public:
    explicit FramePreview(QQuickItem* parent = nullptr);

    [[nodiscard]] QObject* source() const { return source_; }
    void setSource(QObject* source);
    [[nodiscard]] QRectF frameRect() const;

signals:
    void sourceChanged();
    void frameRectChanged();

protected:
    QSGNode* updatePaintNode(QSGNode* old, UpdatePaintNodeData* data) override;
    void geometryChange(const QRectF& next, const QRectF& previous) override;

private:
    void take_frame();

    QPointer<PreviewSource> source_;
    QImage image_;       // el que se pinta
    bool dirty_ = false; // hay que subirlo a una textura nueva
};

// Registra FramePreview en QML (import GenClip.Preview). Antes de cargar el QML.
void register_qml_types();

} // namespace gc::studio
