#pragma once

// La pestaña YouTube del material («Cargar clips» de EdMaterial.jsx): pegar un enlace y analizarlo (título,
// duración, tramos recomendados por el heatmap y calidades), el historial de enlaces (settings.yt_history) y
// agregar el vídeo completo al material en una calidad. El análisis y la descarga los hace el backend de
// video-yt por el puente (POST /api/analyze y /api/youtube-video) hasta F7 (yt-dlp).

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

class YoutubeLoader : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString url READ url WRITE setUrl NOTIFY urlChanged)
    Q_PROPERTY(bool analyzing READ analyzing NOTIFY stateChanged)
    // Segundos analizando (los mensajes de espera cambian a los 8 y 25 s).
    Q_PROPERTY(int elapsed READ elapsed NOTIFY elapsedChanged)
    // {title, duration, id, segments: [{index, start, end, score}], qualities: [{quality, short, tag, meta}]}; vacío sin vídeo.
    Q_PROPERTY(QVariantMap result READ result NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    // «… está en el material.» al acabar una descarga.
    Q_PROPERTY(QString message READ message NOTIFY stateChanged)
    Q_PROPERTY(QVariantList history READ history NOTIFY historyChanged)
    // La descarga: {title, progress, message, elapsed}; vacía si no hay.
    Q_PROPERTY(QVariantMap download READ download NOTIFY downloadChanged)
    Q_PROPERTY(bool canAddFull READ canAddFull NOTIFY stateChanged)

public:
    explicit YoutubeLoader(QObject* parent = nullptr);

    [[nodiscard]] QString url() const { return url_; }
    void setUrl(const QString& url);
    [[nodiscard]] bool analyzing() const { return analyzing_; }
    [[nodiscard]] int elapsed() const { return elapsed_; }
    [[nodiscard]] QVariantMap result() const { return result_; }
    [[nodiscard]] QString error() const { return error_; }
    [[nodiscard]] QString message() const { return message_; }
    [[nodiscard]] QVariantList history() const { return history_; }
    [[nodiscard]] QVariantMap download() const { return download_; }
    [[nodiscard]] bool canAddFull() const;

    // Para los tests: la dirección del backend y cada cuánto se sondea la descarga.
    [[nodiscard]] BackendJobs& backend() { return jobs_; }
    void setSettings(core::SettingsFile* settings) { settings_ = settings; }
    // Otro proyecto: la pestaña empieza de nuevo (sigue el historial).
    void setProject(const QString& pid);

    // loadYt: analiza el enlace (o el de `url` si se pasa, que pasa a ser el del campo).
    Q_INVOKABLE void load(const QString& url = {});
    // El historial, de los ajustes (al abrirlo); quitar una entrada lo guarda.
    Q_INVOKABLE void reloadHistory();
    Q_INVOKABLE void removeHistory(const QVariantMap& item);
    // addFullYt: el vídeo completo en esa calidad (0: la mejor que haya).
    Q_INVOKABLE void addFull(int quality = 0);
    Q_INVOKABLE void cancelDownload();

signals:
    void urlChanged();
    void stateChanged();
    void elapsedChanged();
    void historyChanged();
    void downloadChanged();
    // La descarga acabó: hay un vídeo nuevo en projects.json.
    void videoAdded(const QString& message);
    // La timeline ya usaba ese vídeo en menos calidad: {index, quality, versions} de «Subir calidad», en JSON.
    void upgradeOffered(const QString& ask);

private:
    void set_result(const Json& res);
    void update(const Json& job);

    core::SettingsFile* settings_ = nullptr;
    BackendJobs jobs_;    // la descarga
    BackendJobs analyze_; // el análisis (un POST suelto)
    QTimer tick_;
    QElapsedTimer started_;
    QElapsedTimer download_started_;
    QString project_;
    QString url_;
    QString analyzed_url_; // el del resultado
    bool analyzing_ = false;
    int elapsed_ = 0;
    int generation_ = 0; // un análisis que llega tarde (otro enlace, otro proyecto) se tira
    QVariantMap result_;
    QString error_;
    QString message_;
    QVariantList history_;
    QVariantMap download_;
};

} // namespace gc::studio
