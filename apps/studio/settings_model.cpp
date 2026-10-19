#include "settings_model.hpp"

#include <algorithm>
#include <array>

namespace gc::studio {

namespace {

struct Provider {
    const char* id;
    const char* label;
    const char* use;
    const char* hint;
    bool wired;
};

// API_PROVIDERS de EdSettings.jsx.
constexpr std::array<Provider, 19> kProviders{{
    {"gemini", "Google Gemini", "IA · voz · img", "Narración TTS, guion e imágenes", true},
    {"openai", "OpenAI", "IA · voz · img", "GPT, Whisper e imágenes", true},
    {"anthropic", "Anthropic", "Guiones", "Claude para guiones", true},
    {"elevenlabs", "ElevenLabs", "Voces TTS", "Voces TTS — aún sin cablear (se usan Kokoro/Piper/Gemini/Azure)", false},
    {"openrouter", "OpenRouter", "Chat IA", "Varios modelos con una sola clave", true},
    {"groq", "Groq", "Chat IA", "Chat IA gratis y muy rápido", true},
    {"cerebras", "Cerebras", "Chat IA", "Chat IA gratis, inferencia ultrarrápida", true},
    {"mistral", "Mistral", "Chat IA", "Chat IA gratis (La Plateforme)", true},
    {"pexels", "Pexels", "Fotos y vídeo", "Vídeo e imágenes de stock (pestaña Explorar)", true},
    {"giphy", "GIPHY", "GIFs", "GIFs animados (pestaña Explorar)", true},
    {"pixabay", "Pixabay", "Fotos y vídeo", "Fotos y vídeo de stock (pestaña Explorar)", true},
    {"unsplash", "Unsplash", "Fotos", "Fotos de stock (pestaña Explorar)", true},
    {"youtube", "YouTube Data", "Metadatos", "Metadatos y búsqueda — aún sin cablear", false},
    {"replicate", "Replicate", "Imagen/vídeo IA", "Modelos de imagen y vídeo — aún sin cablear", false},
    {"fal", "Fal.ai", "Imagen IA", "Generación rápida — aún sin cablear", false},
    {"huggingface", "Hugging Face", "Chat IA", "Modelos abiertos", true},
    {"assemblyai", "AssemblyAI", "Transcripción", "Transcripción — aún sin cablear (se usa Whisper local)", false},
    {"removebg", "Remove.bg", "Quitar fondo", "Quitar fondo — aún sin cablear (se usa matte IA local)", false},
    {"stability", "Stability AI", "Imagen/vídeo IA", "Imagen y vídeo — aún sin cablear", false},
}};

QString qstr(const std::string& s) { return QString::fromStdString(s); }

} // namespace

SettingsModel::SettingsModel(core::SettingsFile& file, QObject* parent) : QObject(parent), file_(file) { reload(); }

template <class T>
bool SettingsModel::ok(const Result<T>& r) {
    error_ = r ? QString() : qstr(r.error().message);
    if (!r) {
        emit changed();
    }
    return r.has_value();
}

void SettingsModel::reload() {
    const auto view = file_.public_view();
    if (!ok(view)) {
        return;
    }
    const Json& exp = view->value("export", Json::object());
    fps_ = exp.value("fps", 30);
    quality_ = qstr(exp.value("quality", std::string("standard")));
    transcribe_ = qstr(view->value("transcribe", Json::object()).value("model", std::string("base")));
    const Json counts = view->value("api_keys_counts", Json::object());
    const auto count = [&](const std::string& id) { return counts.is_object() ? counts.value(id, 0) : 0; };
    providers_.clear();
    for (const Provider& p : kProviders) {
        providers_ << QVariantMap{{"id", p.id}, {"label", p.label}, {"use", p.use},   {"hint", p.hint},
                                  {"wired", p.wired}, {"count", count(p.id)}};
    }
    if (counts.is_object()) {
        for (const auto& [id, n] : counts.items()) {
            const bool known = std::ranges::any_of(kProviders, [&](const Provider& p) { return id == p.id; });
            if (!known && n.is_number() && n.get<int>() > 0) {
                providers_ << QVariantMap{{"id", qstr(id)}, {"label", qstr(id)}, {"use", ""}, {"hint", ""},
                                          {"wired", true}, {"count", n.get<int>()}};
            }
        }
    }
    emit changed();
}

void SettingsModel::setExport(int fps, const QString& quality) {
    if (ok(file_.save({{"export", {{"fps", fps}, {"quality", quality.toStdString()}}}}))) {
        reload();
    }
}

void SettingsModel::setTranscribeModel(const QString& model) {
    if (ok(file_.save({{"transcribe", {{"model", model.toStdString()}}}}))) {
        reload();
    }
}

void SettingsModel::addKey(const QString& provider, const QString& value) {
    if (!value.trimmed().isEmpty() && ok(file_.add_key(provider.toStdString(), value.trimmed().toStdString()))) {
        reload();
    }
}

void SettingsModel::setKey(const QString& provider, int index, const QString& value) {
    if (ok(file_.set_key(provider.toStdString(), index, value.trimmed().toStdString()))) {
        reload();
    }
}

void SettingsModel::removeKey(const QString& provider, int index) {
    if (ok(file_.remove_key(provider.toStdString(), index))) {
        reload();
    }
}

} // namespace gc::studio
