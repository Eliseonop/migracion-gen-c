#include "youtube_loader.hpp"

#include <algorithm>
#include <cmath>

#include "gc/core/yt_ui.hpp"
#include "qt_json.hpp"

namespace gc::studio {

namespace {

// YT_ANALYZE_OPTS de EdMaterial.jsx.
const Json kAnalyzeOpts = {{"min_score", 0.4}, {"max_clips", 10}, {"max_duration", 60}, {"padding", 10}};

QString text_of(const Json& v, const char* key) {
    const Json x = v.is_object() ? v.value(key, Json()) : Json();
    return x.is_string() ? QString::fromStdString(x.get<std::string>()) : QString();
}

} // namespace

YoutubeLoader::YoutubeLoader(QObject* parent) : QObject(parent) {
    tick_.setInterval(500);
    connect(&tick_, &QTimer::timeout, this, [this] {
        if (analyzing_) {
            elapsed_ = static_cast<int>(started_.elapsed() / 1000);
            emit elapsedChanged();
        }
        if (!download_.isEmpty()) {
            download_["elapsed"] = static_cast<int>(download_started_.elapsed() / 1000);
            emit downloadChanged();
        }
    });
}

void YoutubeLoader::setUrl(const QString& url) {
    if (url != url_) {
        url_ = url;
        error_.clear();
        emit urlChanged();
        emit stateChanged();
    }
}

bool YoutubeLoader::canAddFull() const {
    return !analyzed_url_.isEmpty() && download_.isEmpty() && !analyzing_;
}

void YoutubeLoader::setProject(const QString& pid) {
    if (pid == project_) {
        return;
    }
    project_ = pid;
    ++generation_;
    jobs_.stop();
    analyzing_ = false;
    analyzed_url_.clear();
    url_.clear();
    result_.clear();
    error_.clear();
    message_.clear();
    download_.clear();
    tick_.stop();
    emit urlChanged();
    emit stateChanged();
    emit downloadChanged();
    reloadHistory();
}

void YoutubeLoader::reloadHistory() {
    const Json saved = settings_ ? settings_->load() : Json::object();
    QVariantList list;
    for (const Json& item : core::yt_normalize_history(saved.value("yt_history", Json()))) {
        list << to_variant(item);
    }
    history_ = list;
    emit historyChanged();
}

void YoutubeLoader::removeHistory(const QVariantMap& item) {
    const Json saved = settings_ ? settings_->load() : Json::object();
    const Json next = core::yt_history_without(core::yt_normalize_history(saved.value("yt_history", Json())), to_json(item));
    if (settings_) {
        (void)settings_->save({{"yt_history", next}});
    }
    reloadHistory();
}

void YoutubeLoader::load(const QString& from) {
    if (!from.isNull()) {
        setUrl(from.trimmed());
    }
    const QString url = url_.trimmed();
    if (url.isEmpty()) {
        error_ = "Pega el link de un vídeo de YouTube.";
        emit stateChanged();
        return;
    }
    error_.clear();
    result_.clear();
    analyzed_url_.clear();
    analyzing_ = true;
    elapsed_ = 0;
    started_.start();
    tick_.start();
    emit elapsedChanged();
    emit stateChanged();
    Json body = kAnalyzeOpts;
    body["url"] = url.toStdString();
    analyze_.setBaseUrl(jobs_.baseUrl());
    const int gen = ++generation_;
    analyze_.postRaw("/api/analyze", body, [this, gen, url](const QByteArray& data, const QString& error) {
        if (gen != generation_) {
            return;
        }
        analyzing_ = false;
        if (download_.isEmpty()) {
            tick_.stop();
        }
        const Json res = data.isEmpty() ? Json() : Json::parse(data.toStdString(), nullptr, false);
        if (!error.isEmpty() || !res.is_object()) {
            error_ = error.isEmpty() ? QString("No se pudo cargar el vídeo.") : error;
        } else {
            analyzed_url_ = url;
            set_result(res);
        }
        reloadHistory(); // el backend apunta el enlace en el historial
        emit stateChanged();
    });
}

void YoutubeLoader::set_result(const Json& res) {
    const Json video = res.value("video", Json::object());
    QVariantList segments;
    if (res.value("has_heatmap", false) && res.value("segments", Json()).is_array()) {
        for (const Json& s : res["segments"]) {
            segments << to_variant(s);
        }
    }
    QVariantList qualities;
    for (const Json& q : core::yt_video_qualities(res)) {
        QVariantMap row = to_variant(q);
        row["short"] = QString::fromStdString(core::yt_quality_short(q.value("quality", Json())));
        row["tag"] = QString::fromStdString(core::yt_quality_tag(q.value("quality", Json())));
        row["meta"] = QString::fromStdString(core::yt_quality_meta(q));
        qualities << row;
    }
    const Json dur = video.value("duration", Json());
    result_ = {{"title", text_of(video, "title")},
               {"id", text_of(video, "id")},
               {"duration", dur.is_number() ? dur.get<double>() : 0.0},
               {"segments", segments},
               {"qualities", qualities}};
}

void YoutubeLoader::addFull(int quality) {
    if (!canAddFull()) {
        return;
    }
    error_.clear();
    message_.clear();
    const QString title = result_.value("title").toString().isEmpty() ? QString("Vídeo") : result_.value("title").toString();
    Json body = {{"project_id", project_.toStdString()}, {"url", analyzed_url_.toStdString()}};
    body["quality"] = quality > 0 ? Json(quality) : Json(); // sin elegir, la mejor
    download_ = {{"title", title}, {"progress", 0.0}, {"message", "Preparando descarga…"}, {"elapsed", 0}};
    download_started_.start();
    tick_.start();
    emit downloadChanged();
    emit stateChanged();
    jobs_.start("/api/youtube-video", body, [this](const Json& job) { update(job); });
}

void YoutubeLoader::cancelDownload() {
    if (!download_.isEmpty()) {
        jobs_.cancel();
    }
}

void YoutubeLoader::update(const Json& job) {
    const std::string status = job.value("status", std::string());
    if (status == "done") {
        const QString title = download_.value("title").toString();
        const Json result = job.value("result", Json::object());
        const Json q = result.is_object() ? result.value("quality", Json()) : Json();
        download_.clear();
        message_ = QString("«%1»%2 está en el material.")
                       .arg(title, !q.is_null() && js_truthy(q) ? QString(" (%1)").arg(QString::fromStdString(core::yt_quality_short(q))) : QString());
        if (!analyzing_) {
            tick_.stop();
        }
        emit downloadChanged();
        emit stateChanged();
        emit videoAdded(message_);
        return;
    }
    if (status == "error") {
        download_.clear();
        const QString error = text_of(job, "error");
        error_ = error.isEmpty() ? QString("No se pudo descargar el vídeo.") : error;
        if (!analyzing_) {
            tick_.stop();
        }
        emit downloadChanged();
        emit stateChanged();
        return;
    }
    const Json progress = job.value("progress", Json());
    download_["progress"] = progress.is_number() ? progress.get<double>() : 0.0;
    const QString msg = text_of(job, "message");
    if (!msg.isEmpty()) {
        download_["message"] = msg;
    }
    emit downloadChanged();
}

} // namespace gc::studio
