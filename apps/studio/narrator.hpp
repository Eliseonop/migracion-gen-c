#pragma once

// «Cargar audio» del material (AudioTab.jsx y TtsControls.jsx): el narrador (voz desde un guion) y extraer el
// audio de un enlace de YouTube. Los hace el backend de video-yt por el puente (backend_jobs.hpp) hasta F6 y F7:
// él escribe el archivo y añade el audio a projects.json, y aquí se vuelve a leer el material al acabar.

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

#include "backend_jobs.hpp"
#include "gc/core/json.hpp"
#include "gc/core/settings.hpp"

namespace gc::studio {

class Narrator : public QObject {
    Q_OBJECT
    // GET /api/voices: [{id, label, available, voices, styles, reason, needs_key, region}]; vacía sin backend.
    Q_PROPERTY(QVariantList engines READ engines NOTIFY enginesChanged)
    // El motor elegido, de `engines` (curEngine); vacío si no está.
    Q_PROPERTY(QVariantMap current READ current NOTIFY formChanged)
    // El formulario: engine, voice, voice2, blend, speed, pause, style, name, text, url, geminiKey, azureKey y azureRegion.
    Q_PROPERTY(QVariantMap form READ form NOTIFY formChanged)
    // El job: {status, progress, message, error}; vacío si no hay.
    Q_PROPERTY(QVariantMap job READ job NOTIFY jobChanged)
    // De qué es el job: «tts» o «youtube» (jobKindRef).
    Q_PROPERTY(QString kind READ kind NOTIFY jobChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY jobChanged)
    // Segundos desde que se lanzó (0 si no hay job en marcha).
    Q_PROPERTY(int elapsed READ elapsed NOTIFY elapsedChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)
    // El enlace que ya se extrajo (extractedKey): no se vuelve a pedir.
    Q_PROPERTY(QString extracted READ extracted NOTIFY jobChanged)

public:
    explicit Narrator(QObject* parent = nullptr);

    [[nodiscard]] QVariantList engines() const { return engines_; }
    [[nodiscard]] QVariantMap current() const;
    [[nodiscard]] QVariantMap form() const { return form_; }
    [[nodiscard]] QVariantMap job() const { return job_; }
    [[nodiscard]] QString kind() const { return kind_; }
    [[nodiscard]] bool busy() const;
    [[nodiscard]] int elapsed() const { return elapsed_; }
    [[nodiscard]] QString error() const { return error_; }
    [[nodiscard]] QString extracted() const { return extracted_; }

    // Para los tests: su dirección y cada cuánto se sondea.
    [[nodiscard]] BackendJobs& backend() { return jobs_; }
    void setSettings(core::SettingsFile* settings) { settings_ = settings; }
    // Al abrir otro proyecto, todo de nuevo (el AudioTab se vuelve a montar). Con «», sin proyecto.
    void setProject(const QString& pid);

    // Al enseñarse: los motores y, la primera vez en cada proyecto, lo último que se usó (settings.tts).
    Q_INVOKABLE void load();
    Q_INVOKABLE void set(const QString& field, const QVariant& value);
    // generate: guarda la elección en settings.tts y lanza POST /api/tts.
    Q_INVOKABLE void generate();
    // extractYoutube: POST /api/youtube-audio con el enlace del formulario.
    Q_INVOKABLE void extractYoutube();
    Q_INVOKABLE void saveGeminiKey();
    Q_INVOKABLE void saveAzure();

signals:
    void enginesChanged();
    void formChanged();
    void jobChanged();
    void elapsedChanged();
    void errorChanged();
    // El job ha acabado bien: hay material nuevo en projects.json.
    void audioAdded(const QString& message);
    // Un aviso flotante (el Toast de AudioTab).
    void notice(const QString& type, const QString& message);

private:
    void reset_form();
    void apply_settings();
    void reload_engines();
    void engines_loaded(const Json& voices);
    void normalize_voice();
    void set_error(const QString& message);
    void start(const QString& kind, const QString& path, const Json& body);
    void update(const Json& job);
    void stop_clock();

    core::SettingsFile* settings_ = nullptr;
    BackendJobs jobs_;
    QTimer tick_;
    QElapsedTimer started_;
    QString project_;
    QVariantList engines_;
    QVariantMap form_;
    QVariantMap job_;
    QString kind_;
    QString extracted_;
    QString requested_url_; // el enlace del job en curso (jobKindRef.currentUrl)
    QString error_;
    int elapsed_ = 0;
    bool settings_loaded_ = false;
};

} // namespace gc::studio
