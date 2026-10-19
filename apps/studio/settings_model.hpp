#pragma once

// ViewModel de los ajustes (EdSettings.jsx) sobre data/settings.json: exportación, transcripción y
// API keys. Las claves nunca llegan a la interfaz: solo qué proveedores tienen y cuántas.

#include <QObject>
#include <QString>
#include <QVariantList>

#include "gc/core/settings.hpp"

namespace gc::studio {

class SettingsModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(int fps READ fps NOTIFY changed)
    Q_PROPERTY(QString quality READ quality NOTIFY changed)
    Q_PROPERTY(QString transcribeModel READ transcribeModel NOTIFY changed)
    Q_PROPERTY(QVariantList providers READ providers NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)

public:
    explicit SettingsModel(core::SettingsFile& file, QObject* parent = nullptr);

    [[nodiscard]] int fps() const { return fps_; }
    [[nodiscard]] QString quality() const { return quality_; }
    [[nodiscard]] QString transcribeModel() const { return transcribe_; }
    // API_PROVIDERS de EdSettings.jsx con {count} de claves guardadas (y los proveedores
    // guardados que no están en la lista, al final).
    [[nodiscard]] QVariantList providers() const { return providers_; }
    [[nodiscard]] QString error() const { return error_; }

    Q_INVOKABLE void reload();
    Q_INVOKABLE void setExport(int fps, const QString& quality);
    Q_INVOKABLE void setTranscribeModel(const QString& model);
    Q_INVOKABLE void addKey(const QString& provider, const QString& value);
    Q_INVOKABLE void setKey(const QString& provider, int index, const QString& value);
    Q_INVOKABLE void removeKey(const QString& provider, int index);

signals:
    void changed();

private:
    template <class T>
    bool ok(const Result<T>& r);

    core::SettingsFile& file_;
    int fps_ = 30;
    QString quality_ = "standard";
    QString transcribe_ = "base";
    QVariantList providers_;
    QString error_;
};

} // namespace gc::studio
