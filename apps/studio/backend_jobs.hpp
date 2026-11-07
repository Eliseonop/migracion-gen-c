#pragma once

// El puente con el backend de video-yt (plan, F5): lo que aún no está portado (transcribir, TTS, eliminar
// fondo, YouTube) se pide a su API REST (:8000) hasta F6 y F7. Un job: el POST que lo lanza devuelve un Job y
// se sondea con GET /api/job/{id} cada segundo hasta que acaba (done o error), como los hooks de React
// (useSubtitles, useExportJob…). Uno a la vez.

#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <functional>
#include <memory>

#include "gc/core/json.hpp"

class QNetworkReply;

namespace gc::studio {

class BackendJobs : public QObject {
    Q_OBJECT

public:
    explicit BackendJobs(QObject* parent = nullptr);

    // La raíz de la API: http://127.0.0.1:8000, o la de GENCLIP_BACKEND.
    [[nodiscard]] QUrl baseUrl() const { return base_; }
    void setBaseUrl(const QUrl& url) { base_ = url; }
    // Cada cuánto se sondea (1 s, el setInterval de React).
    void setPollInterval(int ms) { timer_.setInterval(ms); }

    // Lanza el job: POST `path` con `body`. `update` recibe el Job al lanzarlo y en cada sondeo, hasta el
    // último (done o error). Si el POST falla llega {status: error, error: el detail del backend, como el
    // req() de services/api.js}; un sondeo que falla se reintenta. Sustituye al que hubiera.
    using Update = std::function<void(const Json& job)>;
    void start(const QString& path, const Json& body, Update update);
    // Deja de esperarlo (en el backend sigue).
    void stop();
    [[nodiscard]] bool busy() const { return update_ != nullptr; }

private:
    void poll();
    void deliver(const Json& job);
    [[nodiscard]] QUrl url(const QString& path) const;

    // Al primer job: un QNetworkAccessManager pide un QCoreApplication, y los tests del modelo van sin él.
    [[nodiscard]] QNetworkAccessManager& net();
    std::unique_ptr<QNetworkAccessManager> net_;
    QTimer timer_;
    QUrl base_;
    QString job_id_;
    Update update_;
    QPointer<QNetworkReply> pending_; // el POST o el sondeo en curso
};

} // namespace gc::studio
