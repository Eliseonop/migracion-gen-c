#include "backend_jobs.hpp"

#include <QByteArray>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QVariant>
#include <utility>

namespace gc::studio {

namespace {

// El mensaje de un error del backend: su `detail` si es un texto, o detail.message, o «Error N» (req()).
QString error_of(const QNetworkReply& reply, const QByteArray& body) {
    const int status = reply.attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status == 0) { // sin respuesta: el backend no está
        return QString("No se pudo conectar con el backend de video-yt (%1).").arg(reply.url().adjusted(QUrl::RemovePath).toString());
    }
    const Json err = Json::parse(body.toStdString(), nullptr, false);
    const Json detail = err.is_object() ? err.value("detail", Json()) : Json();
    if (detail.is_string()) {
        return QString::fromStdString(detail.get<std::string>());
    }
    if (detail.is_object() && detail.value("message", Json()).is_string()) {
        return QString::fromStdString(detail.at("message").get<std::string>());
    }
    return QString("Error %1").arg(status);
}

} // namespace

BackendJobs::BackendJobs(QObject* parent)
    : QObject(parent), base_(qEnvironmentVariableIsSet("GENCLIP_BACKEND") ? QUrl(qEnvironmentVariable("GENCLIP_BACKEND"))
                                                                          : QUrl("http://127.0.0.1:8000")) {
    timer_.setInterval(1000);
    connect(&timer_, &QTimer::timeout, this, &BackendJobs::poll);
}

QUrl BackendJobs::url(const QString& path) const { return base_.resolved(QUrl(path)); }

QNetworkAccessManager& BackendJobs::net() {
    if (!net_) {
        net_ = std::make_unique<QNetworkAccessManager>();
    }
    return *net_;
}

void BackendJobs::start(const QString& path, const Json& body, Update update) {
    stop();
    update_ = std::move(update);
    QNetworkRequest request(url(path));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    QNetworkReply* reply = net().post(request, QByteArray::fromStdString(body.dump()));
    pending_ = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply != pending_) {
            return; // de un job que ya no se espera
        }
        pending_ = nullptr;
        const QByteArray data = reply->readAll();
        const Json job = reply->error() == QNetworkReply::NoError ? Json::parse(data.toStdString(), nullptr, false)
                                                                  : Json();
        if (!job.is_object() || !job.value("id", Json()).is_string()) {
            deliver({{"status", "error"}, {"error", error_of(*reply, data).toStdString()}});
            return;
        }
        job_id_ = QString::fromStdString(job["id"].get<std::string>());
        deliver(job);
        if (update_) {
            timer_.start();
        }
    });
}

void BackendJobs::poll() {
    if (job_id_.isEmpty() || pending_) {
        return; // el sondeo anterior aún no ha vuelto
    }
    QNetworkReply* reply = net().get(QNetworkRequest(url("/api/job/" + job_id_)));
    pending_ = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply != pending_) {
            return;
        }
        pending_ = nullptr;
        if (reply->error() != QNetworkReply::NoError) {
            return; // se reintenta en el siguiente
        }
        const Json job = Json::parse(reply->readAll().toStdString(), nullptr, false);
        if (job.is_object()) {
            deliver(job);
        }
    });
}

void BackendJobs::get(const QString& path, Reply reply) {
    QNetworkReply* pending = net().get(QNetworkRequest(url(path)));
    connect(pending, &QNetworkReply::finished, this, [pending, reply = std::move(reply)] {
        pending->deleteLater();
        const Json body = pending->error() == QNetworkReply::NoError
                              ? Json::parse(pending->readAll().toStdString(), nullptr, false)
                              : Json();
        reply(body.is_discarded() ? Json() : body);
    });
}

void BackendJobs::deliver(const Json& job) {
    const std::string status = job.value("status", std::string());
    const bool over = status == "done" || status == "error";
    const Update update = update_;
    if (over) {
        stop();
    }
    if (update) {
        update(job);
    }
}

void BackendJobs::cancel() {
    if (job_id_.isEmpty()) {
        return;
    }
    QNetworkReply* reply = net().deleteResource(QNetworkRequest(url("/api/job/" + job_id_)));
    connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater); // .catch(() => {})
}

void BackendJobs::stop() {
    timer_.stop();
    job_id_.clear();
    update_ = nullptr;
    if (pending_) {
        QNetworkReply* reply = pending_;
        pending_ = nullptr;
        reply->abort();
    }
}

} // namespace gc::studio
