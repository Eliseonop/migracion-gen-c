#pragma once

// ViewModel de los ajustes (EdSettings.jsx) sobre data/settings.json: exportación, transcripción y
// API keys. Las claves nunca llegan a la interfaz: solo qué proveedores tienen y cuántas.

#include <QObject>
#include <QString>
#include <QVariantList>
#include <filesystem>
#include <string>
#include <vector>

#include "gc/core/settings.hpp"

namespace gc::studio {

// Trae a `file` las claves de otra carpeta (la de video-yt, o su backend/): su settings.json
// (data/ o backend/data/) y sus .env (la carpeta y backend/), con SettingsFile::import_keys: solo
// las de los proveedores que aquí no tienen. Devuelve los proveedores que llegaron.
[[nodiscard]] Result<std::vector<std::string>> import_keys_from(core::SettingsFile& file,
                                                                const std::filesystem::path& folder);
// «Traídas las claves de 3 proveedores: gemini, groq y openai.» (o que no faltaba ninguna).
[[nodiscard]] QString imported_keys_message(const std::vector<std::string>& providers);

class SettingsModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(int fps READ fps NOTIFY changed)
    Q_PROPERTY(QString quality READ quality NOTIFY changed)
    Q_PROPERTY(QString transcribeModel READ transcribeModel NOTIFY changed)
    Q_PROPERTY(QVariantList providers READ providers NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    // Lo que llegó al traer las claves de video-yt.
    Q_PROPERTY(QString message READ message NOTIFY changed)

public:
    explicit SettingsModel(core::SettingsFile& file, QObject* parent = nullptr);

    [[nodiscard]] int fps() const { return fps_; }
    [[nodiscard]] QString quality() const { return quality_; }
    [[nodiscard]] QString transcribeModel() const { return transcribe_; }
    // API_PROVIDERS de EdSettings.jsx con {count} de claves guardadas (y los proveedores
    // guardados que no están en la lista, al final).
    [[nodiscard]] QVariantList providers() const { return providers_; }
    [[nodiscard]] QString error() const { return error_; }
    [[nodiscard]] QString message() const { return message_; }

    Q_INVOKABLE void reload();
    // Trae las claves de video-yt (la URL file:/// del selector de carpetas o una ruta).
    Q_INVOKABLE bool importKeys(const QString& folder);
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
    QString message_;
};

} // namespace gc::studio
