#pragma once

// El recuadro del modal Recortar (EdCropModal.jsx): vive aquí hasta Confirmar, así que cancelar
// no toca la timeline. Las coordenadas son fracciones (0–1) de la fuente entera.

#include <QObject>
#include <QVariantList>
#include <optional>
#include <utility>

#include "gc/core/clip_layout.hpp"

namespace gc::studio {

class CropEditor : public QObject {
    Q_OBJECT
    Q_PROPERTY(double cx READ cx NOTIFY cropChanged)
    Q_PROPERTY(double cy READ cy NOTIFY cropChanged)
    Q_PROPERTY(double wf READ wf NOTIFY cropChanged)
    Q_PROPERTY(double hf READ hf NOTIFY cropChanged)
    Q_PROPERTY(QVariantList handles READ handles NOTIFY cropChanged)
    Q_PROPERTY(QString ratio READ ratio NOTIFY cropChanged)
    Q_PROPERTY(double rotation READ rotation WRITE setRotation NOTIFY cropChanged)
    Q_PROPERTY(double srcAspect READ srcAspect NOTIFY cropChanged)
    Q_PROPERTY(QVariantList ratios READ ratios CONSTANT)

public:
    using QObject::QObject;

    [[nodiscard]] double cx() const { return crop_.cx; }
    [[nodiscard]] double cy() const { return crop_.cy; }
    [[nodiscard]] double wf() const { return crop_.wf; }
    [[nodiscard]] double hf() const { return crop_.hf; }
    [[nodiscard]] QVariantList handles() const;
    [[nodiscard]] QString ratio() const { return ratio_; }
    [[nodiscard]] double rotation() const { return rotation_; }
    void setRotation(double deg);
    [[nodiscard]] double srcAspect() const { return src_aspect_; }
    [[nodiscard]] static QVariantList ratios();
    [[nodiscard]] const core::CropWindow& crop() const { return crop_; }

    // El estado de cropStart (EditorModel): recorte, giro y proporciones de la fuente y la salida.
    Q_INVOKABLE void load(const QVariantMap& start);
    // Puntero sobre la imagen de w × h px: true si agarra un tirador o el recuadro.
    Q_INVOKABLE bool press(double nx, double ny, double w, double h);
    Q_INVOKABLE void drag(double nx, double ny);
    Q_INVOKABLE void release() { grab_.reset(); }
    // Qt::CursorShape para el puntero sin pulsar.
    Q_INVOKABLE int cursorAt(double nx, double ny, double w, double h) const;
    Q_INVOKABLE void pickRatio(const QString& id);
    Q_INVOKABLE void reset();

signals:
    void cropChanged();

private:
    struct Grab {
        core::CropWindow start;
        double nx = 0;
        double ny = 0;
        std::optional<std::pair<int, int>> handle;
    };
    void set(const core::CropWindow& crop);
    [[nodiscard]] std::optional<double> ratio_norm() const;

    core::CropWindow crop_;
    QString ratio_ = "free";
    double rotation_ = 0;
    double src_aspect_ = 16.0 / 9.0;
    double out_aspect_ = 16.0 / 9.0;
    std::optional<Grab> grab_;
};

} // namespace gc::studio
