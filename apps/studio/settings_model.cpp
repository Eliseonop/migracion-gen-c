#include "settings_model.hpp"

#include <QDir>
#include <QFile>
#include <QStringList>
#include <QUrl>
#include <algorithm>
#include <array>
#include <system_error>

#include "gc/core/io.hpp"

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

namespace fs = std::filesystem;

} // namespace

Result<std::vector<std::string>> import_keys_from(core::SettingsFile& file, const fs::path& folder) {
    std::error_code ec;
    std::vector<fs::path> bases{folder};
    if (fs::is_directory(folder / "backend", ec)) {
        bases.push_back(folder / "backend"); // la carpeta de video-yt: lo suyo está en backend/
    }
    Json source;
    for (const fs::path& base : bases) {
        if (const fs::path settings = base / "data" / "settings.json"; source.is_null() && fs::is_regular_file(settings, ec)) {
            auto read = core::read_json_file_py(settings);
            if (!read) {
                return std::unexpected(read.error());
            }
            source = std::move(*read);
        }
    }
    // Los proveedores de Ajustes y los de Azure que usa video-yt.
    std::vector<std::string> providers{"azure", "azure_vision", "azure_foundry"};
    for (const Provider& p : kProviders) {
        providers.emplace_back(p.id);
    }
    Json env = Json::object();
    bool any_env = false;
    for (const fs::path& base : bases) {
        if (const fs::path dotenv = base / ".env"; fs::is_regular_file(dotenv, ec)) {
            QFile f(qstr(core::path_to_utf8(dotenv)));
            if (!f.open(QIODevice::ReadOnly)) {
                return std::unexpected(Error{ErrorCode::Io, "No se pudo leer " + core::path_to_utf8(dotenv)});
            }
            const QByteArray text = f.readAll();
            const auto keys = core::env_api_keys(std::string_view(text.constData(), static_cast<std::size_t>(text.size())),
                                                 providers);
            for (const auto& [k, v] : keys.items()) {
                env[k] = v;
            }
            any_env = true;
        }
    }
    if (source.is_null() && !any_env) {
        return std::unexpected(Error{ErrorCode::NotFound,
                                     "No hay un settings.json (en data/ o backend/data/) ni un .env en «" +
                                         QDir::toNativeSeparators(qstr(core::path_to_utf8(folder))).toStdString() + "»."});
    }
    return file.import_keys(source, env);
}

QString imported_keys_message(const std::vector<std::string>& providers) {
    if (providers.empty()) {
        return QStringLiteral("No faltaba ninguna: ya tenías claves de esos proveedores.");
    }
    QStringList names;
    for (const std::string& p : providers) {
        names << qstr(p);
    }
    const QString list = names.size() == 1 ? names.front()
                                           : names.mid(0, names.size() - 1).join(", ") + " y " + names.back();
    return providers.size() == 1 ? QString("Traídas las claves de 1 proveedor: %1.").arg(list)
                                 : QString("Traídas las claves de %1 proveedores: %2.").arg(providers.size()).arg(list);
}

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
    message_.clear(); // lo que llegó al traer las claves vale hasta el cambio siguiente (o la otra carpeta)
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

bool SettingsModel::importKeys(const QString& folder) {
    const QUrl url(folder);
    const QString local = url.isLocalFile() ? url.toLocalFile() : folder.trimmed();
    message_.clear();
    if (local.isEmpty()) {
        error_ = QStringLiteral("Elige la carpeta de video-yt.");
        emit changed();
        return false;
    }
    const auto arrived =
        import_keys_from(file_, core::path_from_utf8(QDir::cleanPath(QDir(local).absolutePath()).toStdString()));
    if (!ok(arrived)) {
        return false;
    }
    reload();
    message_ = imported_keys_message(*arrived);
    emit changed();
    return true;
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
