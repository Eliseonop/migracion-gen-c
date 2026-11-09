#include "narrator.hpp"

#include <algorithm>
#include <string>

#include "qt_json.hpp"

namespace gc::studio {

namespace {

QString qstr(const std::string& text) { return QString::fromStdString(text); }

// El texto de una clave de un job o de los ajustes; «» si falta o es otra cosa.
std::string text_of(const Json& object, const char* key) {
    const auto it = object.find(key);
    return it != object.end() && it->is_string() ? it->get<std::string>() : std::string();
}

// `s.tts.blend ?? 0.5`: el número guardado o el valor por defecto.
double number_of(const Json& object, const char* key, double fallback) {
    const auto it = object.find(key);
    return it != object.end() && it->is_number() ? it->get<double>() : fallback;
}

} // namespace

Narrator::Narrator(QObject* parent) : QObject(parent) {
    reset_form();
    tick_.setInterval(500);
    connect(&tick_, &QTimer::timeout, this, [this] {
        const int seconds = static_cast<int>(started_.elapsed() / 1000);
        if (seconds != elapsed_) {
            elapsed_ = seconds;
            emit elapsedChanged();
        }
    });
}

void Narrator::reset_form() {
    form_ = {{"engine", "kokoro"}, {"voice", "ef_dora"}, {"voice2", ""},    {"blend", 0.5},
             {"speed", 1.0},       {"pause", 0.4},       {"style", "documentary"}, {"name", ""},
             {"text", ""},         {"url", ""},          {"geminiKey", ""}, {"azureKey", ""},
             {"azureRegion", ""}};
}

QVariantMap Narrator::current() const {
    const auto it = std::ranges::find_if(engines_, [&](const QVariant& e) { return e.toMap()["id"] == form_["engine"]; });
    return it != engines_.end() ? it->toMap() : QVariantMap();
}

bool Narrator::busy() const {
    const QString status = job_["status"].toString();
    return status == "pending" || status == "running";
}

void Narrator::setProject(const QString& pid) {
    jobs_.stop();
    stop_clock();
    project_ = pid;
    settings_loaded_ = false;
    job_.clear();
    kind_.clear();
    extracted_.clear();
    requested_url_.clear();
    reset_form();
    emit jobChanged();
    emit formChanged();
    set_error({});
}

void Narrator::load() {
    if (!settings_loaded_) {
        settings_loaded_ = true;
        apply_settings();
    }
    reload_engines();
}

void Narrator::apply_settings() {
    if (!settings_) {
        return;
    }
    const Json saved = settings_->load();
    const auto tts = saved.find("tts");
    if (tts == saved.end() || !tts->is_object()) {
        return;
    }
    if (const std::string engine = text_of(*tts, "engine"); !engine.empty()) {
        form_["engine"] = qstr(engine);
    }
    const std::string voice = text_of(*tts, "voice");
    form_["voice"] = voice.empty() ? QString("ef_dora") : qstr(voice);
    form_["voice2"] = qstr(text_of(*tts, "voice2"));
    form_["blend"] = number_of(*tts, "blend", 0.5);
    form_["speed"] = number_of(*tts, "speed", 1.0);
    form_["pause"] = number_of(*tts, "pause", 0.4);
    if (const std::string style = text_of(*tts, "style"); !style.empty()) {
        form_["style"] = qstr(style);
    }
    normalize_voice();
    emit formChanged();
}

void Narrator::reload_engines() {
    jobs_.get("/api/voices", [this](const Json& voices) { engines_loaded(voices); });
}

void Narrator::engines_loaded(const Json& voices) {
    if (!voices.is_object()) {
        return; // backend no listo
    }
    const auto listed = voices.find("engines");
    const Json engines =
        listed != voices.end() && listed->is_array()
            ? *listed
            : Json::array({{{"id", "kokoro"}, {"label", "Kokoro"}, {"available", voices.value("available", Json())},
                            {"voices", voices.value("voices", Json::array())}}});
    engines_ = to_qvariant(engines).toList();
    normalize_voice();
    // La región guardada de Azure, que no es secreta.
    for (const QVariant& e : engines_) {
        const QVariantMap engine = e.toMap();
        if (engine["id"] == "azure" && !engine["region"].toString().isEmpty() && form_["azureRegion"].toString().isEmpty()) {
            form_["azureRegion"] = engine["region"];
        }
    }
    emit enginesChanged();
    emit formChanged();
}

// El efecto de AudioTab al cambiar de motor: una voz de ese motor y, fuera de Kokoro, sin mezcla.
void Narrator::normalize_voice() {
    const QVariantList voices = current()["voices"].toList();
    if (voices.isEmpty()) {
        return;
    }
    const bool known = std::ranges::any_of(voices, [&](const QVariant& v) { return v.toMap()["id"] == form_["voice"]; });
    if (!known) {
        form_["voice"] = voices.front().toMap()["id"];
    }
    if (form_["engine"] != "kokoro") {
        form_["voice2"] = "";
    }
}

void Narrator::set(const QString& field, const QVariant& value) {
    form_[field] = value;
    if (field == "engine") {
        normalize_voice();
    }
    emit formChanged();
}

void Narrator::set_error(const QString& message) {
    if (error_ != message) {
        error_ = message;
        emit errorChanged();
    }
}

void Narrator::generate() {
    set_error({});
    const QString text = form_["text"].toString();
    if (text.trimmed().isEmpty()) {
        set_error("Escribe o pega el texto a narrar.");
        return;
    }
    const std::string engine = form_["engine"].toString().toStdString();
    const std::string voice = form_["voice"].toString().toStdString();
    const std::string voice2 = form_["voice2"].toString().toStdString();
    const std::string style = form_["style"].toString().toStdString();
    const double blend = form_["blend"].toDouble();
    const double speed = form_["speed"].toDouble();
    const double pause = form_["pause"].toDouble();
    if (settings_) { // putSettings(...).catch(() => {}): si no se guarda, se genera igual
        (void)settings_->save({{"tts",
                                {{"engine", engine},
                                 {"voice", voice},
                                 {"voice2", voice2.empty() ? Json() : Json(voice2)},
                                 {"blend", blend},
                                 {"speed", speed},
                                 {"pause", pause},
                                 {"style", style}}}});
    }
    Json body = {{"project_id", project_.toStdString()}, {"text", text.toStdString()}, {"engine", engine}, {"voice", voice}};
    if (engine == "kokoro" && !voice2.empty()) {
        body["voice2"] = voice2;
    }
    body["blend"] = blend;
    body["speed"] = speed;
    body["pause"] = pause;
    if (const QString name = form_["name"].toString().trimmed(); !name.isEmpty()) {
        body["name"] = name.toStdString();
    }
    if (engine == "gemini") {
        body["style"] = style;
    }
    start("tts", "/api/tts", body);
}

void Narrator::extractYoutube() {
    set_error({});
    const QString link = form_["url"].toString().trimmed();
    if (link.isEmpty()) {
        set_error("Pega un enlace de YouTube.");
        return;
    }
    if (link == extracted_) {
        return;
    }
    requested_url_ = link;
    start("youtube", "/api/youtube-audio", {{"project_id", project_.toStdString()}, {"url", link.toStdString()}});
}

void Narrator::start(const QString& kind, const QString& path, const Json& body) {
    kind_ = kind;
    job_ = {{"status", "pending"}, {"progress", 0.0}, {"message", ""}, {"error", ""}};
    elapsed_ = 0;
    started_.start();
    tick_.start();
    emit elapsedChanged();
    emit jobChanged();
    jobs_.start(path, body, [this](const Json& job) { update(job); });
}

void Narrator::stop_clock() {
    tick_.stop();
    if (elapsed_ != 0) {
        elapsed_ = 0;
        emit elapsedChanged();
    }
}

void Narrator::update(const Json& job) {
    const std::string status = text_of(job, "status");
    if (!job.contains("id")) { // el POST ha fallado: el setError de React
        job_.clear();
        stop_clock();
        emit jobChanged();
        set_error(qstr(text_of(job, "error")));
        return;
    }
    const auto progress = job.find("progress");
    job_ = {{"status", qstr(status)},
            {"progress", progress != job.end() && progress->is_number() ? progress->get<double>() : 0.0},
            {"message", qstr(text_of(job, "message"))},
            {"error", qstr(text_of(job, "error"))}};
    if (status != "done" && status != "error") {
        emit jobChanged();
        return;
    }
    stop_clock();
    const bool youtube = kind_ == "youtube";
    if (status == "done" && youtube) {
        extracted_ = requested_url_;
    }
    emit jobChanged();
    if (status == "done") {
        emit audioAdded(youtube ? QString("Audio extraído y guardado.") : QString("Audio generado y guardado."));
    }
}

void Narrator::saveGeminiKey() {
    const QString key = form_["geminiKey"].toString().trimmed();
    if (key.isEmpty()) {
        set_error("Pega la API key de Gemini.");
        return;
    }
    set_error({});
    if (!settings_) {
        return;
    }
    const auto saved = settings_->save({{"gemini_api_key", key.toStdString()}});
    if (!saved) {
        set_error(qstr(saved.error().message));
        return;
    }
    form_["geminiKey"] = "";
    emit formChanged();
    reload_engines();
    emit notice("success", "API key de Gemini guardada.");
}

void Narrator::saveAzure() {
    const QString key = form_["azureKey"].toString().trimmed();
    const QString region = form_["azureRegion"].toString().trimmed();
    if (key.isEmpty() || region.isEmpty()) {
        set_error("Pega la clave y la región de Azure (ej. eastus).");
        return;
    }
    set_error({});
    if (!settings_) {
        return;
    }
    const auto saved = settings_->save({{"azure_region", region.toStdString()}, {"api_keys", {{"azure", key.toStdString()}}}});
    if (!saved) {
        set_error(qstr(saved.error().message));
        return;
    }
    form_["azureKey"] = "";
    emit formChanged();
    reload_engines();
    emit notice("success", "Credenciales de Azure guardadas.");
}

} // namespace gc::studio
