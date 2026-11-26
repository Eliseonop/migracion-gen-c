#include "live_recorder.hpp"

#include <algorithm>
#include <cmath>
#include <system_error>
#include <vector>

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUrl>

#include "gc/core/io.hpp"
#include "gc/core/json.hpp"

#include "studio_config.hpp"

namespace gc::studio {

namespace {

namespace fs = std::filesystem;

constexpr const char* kYtdlpKey = "ytdlp";

QString qstr(const std::string& s) { return QString::fromStdString(s); }
QString qpath(const std::filesystem::path& p) { return QString::fromStdString(core::path_to_utf8(p)); }
QString native(const fs::path& p) { return QDir::toNativeSeparators(qpath(p)); }

// «m:ss» o «h:mm:ss» de unos segundos, hacia abajo.
QString clock(double s) {
    const auto n = static_cast<long long>(std::floor(std::isfinite(s) ? std::max(0.0, s) : 0.0));
    const QString ss = QString::number(n % 60).rightJustified(2, '0');
    if (n >= 3600) {
        return QString("%1:%2:%3").arg(n / 3600).arg(QString::number((n % 3600) / 60).rightJustified(2, '0'), ss);
    }
    return QString("%1:%2").arg((n % 3600) / 60).arg(ss);
}

// La última línea con algo de lo que un programa dejó en stderr.
QString last_line(const QByteArray& text) {
    const QStringList lines = QString::fromUtf8(text).split('\n', Qt::SkipEmptyParts);
    for (auto it = lines.crbegin(); it != lines.crend(); ++it) {
        if (const QString line = it->trimmed(); !line.isEmpty()) {
            return line;
        }
    }
    return {};
}

bool non_empty_file(const std::filesystem::path& path) {
    std::error_code ec;
    return !path.empty() && std::filesystem::is_regular_file(path, ec) && std::filesystem::file_size(path, ec) > 0 && !ec;
}

} // namespace

LiveRecorder::LiveRecorder(QObject* parent) : QObject(parent) {
    // Python sin escribir .pyc (la venv de video-yt es de solo lectura) y con la salida en UTF-8.
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert("PYTHONDONTWRITEBYTECODE", "1");
    env.insert("PYTHONIOENCODING", "utf-8");
    env.insert("PYTHONUTF8", "1");
    resolver_.setProcessEnvironment(env);
    connect(&resolver_, &QProcess::finished, this, [this](int code) { resolved(code); });
    connect(&recorder_, &QProcess::readyReadStandardOutput, this, [this] { read_progress(); });
    connect(&recorder_, &QProcess::readyReadStandardError, this, [this] {
        stderr_ += recorder_.readAllStandardError();
        if (stderr_.size() > 8192) {
            stderr_ = stderr_.right(4096);
        }
    });
    connect(&recorder_, &QProcess::finished, this, [this](int code) { recording_ended(code); });
    connect(&remuxer_, &QProcess::finished, this, [this](int code) { remuxed(code); });
    for (QProcess* p : {&resolver_, &recorder_, &remuxer_}) {
        connect(p, &QProcess::errorOccurred, this, [this, p](QProcess::ProcessError e) {
            if (e != QProcess::FailedToStart) {
                return; // el final llega por finished
            }
            const QString what = p == &resolver_ ? QString("yt-dlp") : QString("FFmpeg");
            fail(QString("No se pudo ejecutar %1 (%2).").arg(what, p->program()));
        });
    }
    stop_timeout_.setSingleShot(true);
    stop_timeout_.setInterval(10000);
    connect(&stop_timeout_, &QTimer::timeout, this, [this] { recorder_.kill(); });
}

LiveRecorder::~LiveRecorder() {
    // Al cerrar con una grabación en marcha, el TS se queda en la carpeta del proyecto.
    for (QProcess* p : {&resolver_, &recorder_, &remuxer_}) {
        p->disconnect(this);
    }
    if (recorder_.state() != QProcess::NotRunning) {
        recorder_.write("q\n");
        if (!recorder_.waitForFinished(3000)) {
            recorder_.kill();
            recorder_.waitForFinished(1000);
        }
    }
    for (QProcess* p : {&resolver_, &remuxer_}) {
        if (p->state() != QProcess::NotRunning) {
            p->kill();
            p->waitForFinished(1000);
        }
    }
}

void LiveRecorder::setUrl(const QString& url) {
    if (url != url_) {
        url_ = url;
        emit urlChanged();
    }
}

bool LiveRecorder::busy() const {
    return status_ == "resolving" || status_ == "recording" || status_ == "saving";
}

void LiveRecorder::setBefore(double seconds) {
    const double v = std::isfinite(seconds) ? std::clamp(seconds, 0.0, 600.0) : before_;
    if (v != before_) {
        before_ = v;
        emit windowChanged();
    }
}

void LiveRecorder::setAfter(double seconds) {
    const double v = std::isfinite(seconds) ? std::clamp(seconds, 0.0, 600.0) : after_;
    if (v != after_) {
        after_ = v;
        emit windowChanged();
    }
}

void LiveRecorder::setProject(const QString& pid, const std::filesystem::path& folder) {
    project_ = pid;
    folder_ = folder;
    if (!busy()) {
        reset();
    }
}

void LiveRecorder::setTools(const QStringList& ytdlp, const QStringList& ffmpeg) {
    ytdlp_override_ = ytdlp;
    ffmpeg_override_ = ffmpeg;
    emit toolsChanged();
}

void LiveRecorder::setDataRoot(const fs::path& root) {
    if (root != data_root_) {
        data_root_ = root;
        emit toolsChanged();
    }
}

void LiveRecorder::setConfig(const fs::path& config) {
    config_ = config;
    chosen_ytdlp_.reset();
    if (const auto saved = studio_config::get(config_, kYtdlpKey)) {
        chosen_ytdlp_ = core::path_from_utf8(*saved);
    }
    emit toolsChanged();
}

void LiveRecorder::setEnvironment(const QProcessEnvironment& env) {
    env_override_ = env;
    emit toolsChanged();
}

QString LiveRecorder::findYtdlp(const YtdlpPlaces& places) {
    if (!places.env.isEmpty()) {
        return places.env;
    }
    std::error_code ec;
    if (places.chosen && fs::is_regular_file(*places.chosen, ec)) {
        return native(*places.chosen);
    }
    if (!places.path.isEmpty()) {
        if (const QString found = QStandardPaths::findExecutable("yt-dlp", places.path); !found.isEmpty()) {
            return QDir::toNativeSeparators(found);
        }
    }
    std::vector<fs::path> candidates;
    if (!places.local_app_data.empty()) {
        // winget install yt-dlp: el enlace de Links (si winget puede crearlo) o el programa en su paquete,
        // Packages/yt-dlp.yt-dlp_Microsoft.Winget.Source_<…>/yt-dlp.exe.
        const fs::path winget = places.local_app_data / "Microsoft" / "WinGet";
        candidates.push_back(winget / "Links" / "yt-dlp.exe");
        for (fs::directory_iterator it(winget / "Packages", ec), end; !ec && it != end; it.increment(ec)) {
            if (core::path_to_utf8(it->path().filename()).starts_with("yt-dlp.yt-dlp")) {
                candidates.push_back(it->path() / "yt-dlp.exe");
            }
        }
    }
    if (!places.data_root.empty()) {
        for (const auto& base : {places.data_root, places.data_root.parent_path()}) {
            candidates.push_back(base / ".venv" / "Scripts" / "yt-dlp.exe");
            candidates.push_back(base / ".venv" / "bin" / "yt-dlp");
        }
    }
    for (const auto& path : candidates) {
        if (fs::is_regular_file(path, ec)) {
            return native(path);
        }
    }
    return {};
}

QStringList LiveRecorder::ytdlp() const {
    if (!ytdlp_override_.isEmpty()) {
        return ytdlp_override_;
    }
    const QProcessEnvironment env = env_override_ ? *env_override_ : QProcessEnvironment::systemEnvironment();
    const QString found = findYtdlp({env.value("GENCLIP_YTDLP"), chosen_ytdlp_,
                                     env.value("PATH").split(QDir::listSeparator(), Qt::SkipEmptyParts),
                                     core::path_from_utf8(env.value("LOCALAPPDATA").toStdString()), data_root_});
    return found.isEmpty() ? QStringList{} : QStringList{found};
}

QString LiveRecorder::ytdlpPath() const {
    const QStringList tool = ytdlp();
    return tool.isEmpty() ? QString() : tool.front();
}

bool LiveRecorder::chooseYtdlp(const QString& file) {
    if (busy()) {
        return false;
    }
    const QUrl url(file);
    const QString local = url.isLocalFile() ? url.toLocalFile() : file.trimmed();
    const fs::path path = core::path_from_utf8(QFileInfo(local).absoluteFilePath().toStdString());
    std::error_code ec;
    if (local.isEmpty() || !fs::is_regular_file(path, ec)) {
        fail(local.isEmpty() ? QStringLiteral("Elige el programa yt-dlp.")
                             : QString("No encuentro el programa «%1».").arg(native(path)));
        return false;
    }
    chosen_ytdlp_ = path;
    QString unsaved;
    if (!config_.empty()) {
        if (const auto saved = studio_config::set(config_, kYtdlpKey, core::path_to_utf8(path)); !saved) {
            unsaved = QString(" (no se pudo recordar para la próxima vez: %1)").arg(qstr(saved.error().message));
        }
    }
    error_.clear();
    message_ = QString("Listo: los directos se buscan con «%1»%2.").arg(native(path), unsaved);
    set_status("");
    emit toolsChanged();
    return true;
}

QStringList LiveRecorder::ffmpeg() const {
    if (!ffmpeg_override_.isEmpty()) {
        return ffmpeg_override_;
    }
    if (const QString set = qEnvironmentVariable("GENCLIP_FFMPEG"); !set.isEmpty()) {
        return {set};
    }
    if (const QString found = QStandardPaths::findExecutable("ffmpeg"); !found.isEmpty()) {
        return {found};
    }
    return {};
}

void LiveRecorder::run(QProcess& process, const QStringList& tool, const std::vector<std::string>& args) {
    QStringList list = tool.mid(1);
    for (const std::string& a : args) {
        list << qstr(a);
    }
    process.start(tool.front(), list);
}

void LiveRecorder::set_status(const QString& status) {
    status_ = status;
    emit stateChanged();
}

void LiveRecorder::fail(const QString& error) {
    stop_timeout_.stop();
    for (QProcess* p : {&resolver_, &recorder_, &remuxer_}) {
        if (p->state() != QProcess::NotRunning) {
            p->kill();
        }
    }
    error_ = error;
    message_.clear();
    set_status("error");
}

void LiveRecorder::start(const QString& url) {
    if (busy()) {
        return;
    }
    if (!url.isEmpty()) {
        setUrl(url);
    }
    error_.clear();
    message_.clear();
    const auto link = core::live_link(url_.toStdString());
    if (!link) {
        fail("Pega el enlace de un directo: tiktok.com/@usuario/live o uno de YouTube.");
        return;
    }
    if (project_.isEmpty() || folder_.empty()) {
        fail("Abre un proyecto para grabar.");
        return;
    }
    emit toolsChanged(); // se vuelve a buscar: el panel enseña el que se usa
    const QStringList yt = ytdlp();
    if (yt.isEmpty()) {
        fail("No encuentro yt-dlp, el programa que busca el directo. Instálalo con «winget install yt-dlp» o, si ya "
             "lo tienes, elígelo abajo.");
        return;
    }
    if (ffmpeg().isEmpty()) {
        fail("No encuentro FFmpeg. Instálalo (por ejemplo, «winget install ffmpeg») o pon su ruta en GENCLIP_FFMPEG.");
        return;
    }
    pid_ = project_;
    ts_.clear();
    mp4_.clear();
    stderr_.clear();
    ended_ = false;
    progress_ = {};
    marks_.clear();
    info_ = {{"platform", qstr(std::string(core::live_platform_name(link->platform)))}, {"url", qstr(link->url)}};
    emit progressChanged();
    emit marksChanged();
    set_status("resolving");
    run(resolver_, yt, core::live_resolve_args(*link));
}

void LiveRecorder::resolved(int code) {
    if (status_ != "resolving") {
        return; // lo dejó
    }
    const QByteArray out = resolver_.readAllStandardOutput();
    const QByteArray err = resolver_.readAllStandardError();
    if (code != 0 || out.trimmed().isEmpty()) {
        fail(qstr(core::live_resolve_error(err.toStdString())));
        return;
    }
    const Json json = Json::parse(out.toStdString(), nullptr, false);
    const auto stream = core::live_stream_from_info(json);
    if (!stream) {
        fail(qstr(stream.error().message));
        return;
    }
    const auto link = core::live_link(url_.toStdString());
    const auto platform = link ? link->platform : core::LivePlatform::TikTok;
    info_["id"] = qstr(stream->id);
    info_["title"] = qstr(stream->title);
    info_["uploader"] = qstr(stream->uploader);
    std::error_code ec;
    std::filesystem::create_directories(folder_, ec);
    const std::string stem = core::live_file_stem(stream->title, platform,
                                                  QDateTime::currentDateTime().toString("yyyy-MM-dd HH-mm").toStdString());
    // Si ya hay una grabación con ese nombre (el mismo minuto), «… 2», «… 3»…
    for (int n = 1;; ++n) {
        const std::string name = n == 1 ? stem : stem + " " + std::to_string(n);
        ts_ = folder_ / core::path_from_utf8(name + ".ts");
        mp4_ = folder_ / core::path_from_utf8(name + ".mp4");
        if (!std::filesystem::exists(ts_, ec) && !std::filesystem::exists(mp4_, ec)) {
            break;
        }
    }
    set_status("recording");
    run(recorder_, ffmpeg(), core::live_record_args(*stream, ts_));
}

void LiveRecorder::read_progress() {
    const double seconds = progress_.seconds;
    const qint64 bytes = progress_.bytes;
    while (recorder_.canReadLine()) {
        core::live_apply_progress(progress_, recorder_.readLine().toStdString());
    }
    if (progress_.seconds != seconds || progress_.bytes != bytes) {
        emit progressChanged();
    }
}

void LiveRecorder::mark() {
    if (status_ != "recording") {
        return;
    }
    const double t = progress_.seconds;
    marks_ << QVariantMap{{"t", t}, {"label", clock(t)}};
    emit marksChanged();
}

void LiveRecorder::removeMark(int index) {
    if (index >= 0 && index < marks_.size()) {
        marks_.removeAt(index);
        emit marksChanged();
    }
}

void LiveRecorder::stop() {
    if (status_ == "resolving") {
        resolver_.kill();
        set_status("");
        return;
    }
    if (status_ != "recording") {
        return;
    }
    set_status("saving");
    recorder_.write("q\n");
    stop_timeout_.start();
}

void LiveRecorder::recording_ended(int code) {
    stop_timeout_.stop();
    read_progress();
    if (status_ == "recording") {
        ended_ = true; // el directo acabó (o se cortó) sin pararlo
        set_status("saving");
    }
    if (status_ != "saving") {
        return; // cancelado o fallado
    }
    if (!non_empty_file(ts_)) {
        const QString why = last_line(stderr_);
        fail(why.isEmpty() ? QString("No se grabó nada (FFmpeg acabó con el código %1).").arg(code)
                           : "No se pudo grabar el directo: " + why);
        return;
    }
    stderr_.clear();
    run(remuxer_, ffmpeg(), core::live_remux_args(ts_, mp4_));
}

void LiveRecorder::remuxed(int code) {
    if (status_ != "saving") {
        return;
    }
    if (code != 0 || !non_empty_file(mp4_)) {
        const QString why = last_line(remuxer_.readAllStandardError());
        fail(QString("No se pudo pasar la grabación a MP4%1. Queda en %2.")
                 .arg(why.isEmpty() ? QString() : ": " + why, qpath(ts_)));
        return;
    }
    std::error_code ec;
    std::filesystem::remove(ts_, ec);
    QVariantMap info = info_;
    info["pid"] = pid_;
    info["ended"] = ended_;
    emit recorded(qpath(mp4_), info, marks_, progress_.seconds);
}

void LiveRecorder::finish(const QString& message, const QString& error) {
    if (!error.isEmpty()) {
        fail(error);
        return;
    }
    message_ = message;
    error_.clear();
    set_status("done");
}

void LiveRecorder::remove_files() {
    std::error_code ec;
    for (const auto& f : {ts_, mp4_}) {
        if (!f.empty()) {
            std::filesystem::remove(f, ec);
        }
    }
}

void LiveRecorder::cancel() {
    const bool recording = status_ == "recording" || status_ == "saving";
    stop_timeout_.stop();
    status_.clear(); // antes de matar: los finished que lleguen no hacen nada
    for (QProcess* p : {&resolver_, &recorder_, &remuxer_}) {
        if (p->state() != QProcess::NotRunning) {
            p->kill();
            p->waitForFinished(2000);
        }
    }
    if (recording) {
        remove_files();
    }
    reset();
}

void LiveRecorder::reset() {
    if (busy()) {
        return;
    }
    error_.clear();
    message_.clear();
    info_.clear();
    marks_.clear();
    progress_ = {};
    emit progressChanged();
    emit marksChanged();
    set_status("");
}

} // namespace gc::studio
