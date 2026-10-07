#include "gc/core/settings.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <set>

#include "edit_util.hpp"
#include "gc/core/bg.hpp"
#include "gc/core/io.hpp"

namespace gc::core {

using detail::field;
using detail::invalid;
using detail::py_float;
using detail::py_int;
using detail::py_iter;
using detail::py_str;
using detail::py_strip;
using detail::py_truthy;

namespace {

constexpr std::array<std::string_view, 6> kKeyMasks{"",     "true",          "false", "********",
                                                    "••••", "••••••••"};
constexpr std::array<std::int64_t, 5> kFpsChoices{24, 25, 30, 50, 60};
constexpr std::array<std::string_view, 3> kQualities{"draft", "standard", "high"};
constexpr std::array<std::string_view, 5> kModels{"tiny", "base", "small", "medium", "large-v3"};

Json defaults() {
    Json d = Json::object();
    d["tts"] = Json{{"voice", "ef_dora"}, {"speed", 1.0}};
    d["favorites"] = Json{{"sfx", Json::array()}, {"audios", Json::array()}, {"textStyles", Json::array()}};
    d["yt_history"] = Json::array();
    d["api_keys"] = Json::object();
    d["export"] = Json{{"fps", 30}, {"quality", "standard"}};
    d["transcribe"] = Json{{"model", "base"}};
    // Eliminar fondo: device de ONNX Runtime (auto | cuda | dml | cpu) y motor por defecto.
    d["bg_removal"] = Json{{"device", "auto"}, {"provider", "u2net"}};
    // Modelos de IA: minutos sin uso tras los que se descargan de la RAM (0 = nunca).
    d["models"] = Json{{"idle_minutes", 5}};
    return d;
}

std::string ascii_lower(std::string s) {
    std::ranges::transform(s, s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

bool is_mask(const std::string& text) {
    const std::string low = ascii_lower(text);
    return std::ranges::find(kKeyMasks, low) != kKeyMasks.end() ||
           std::ranges::find(kKeyMasks, text) != kKeyMasks.end();
}

// _secret: una clave de verdad, o "" si es una máscara, un booleano o nada.
std::string secret(const Json& value) {
    if (value.is_boolean() || value.is_null()) {
        return {};
    }
    std::string text = py_strip(py_str(value));
    return is_mask(text) ? std::string() : text;
}

// [str(x) for x in (v or [])]; nullopt si no se puede recorrer.
std::optional<Json> str_list(const Json& v, bool to_text) {
    Json out = Json::array();
    if (!py_truthy(v)) {
        return out;
    }
    const auto items = py_iter(v);
    if (!items) {
        return std::nullopt;
    }
    for (const Json& x : *items) {
        out.push_back(to_text ? Json(py_str(x)) : x);
    }
    return out;
}

std::optional<Json> merge_favorites(const Json& raw) {
    Json base{{"sfx", Json::array()}, {"audios", Json::array()}, {"textStyles", Json::array()}};
    if (!raw.is_object()) {
        return base;
    }
    auto sfx = str_list(field(raw, "sfx"), true);
    auto audios = str_list(field(raw, "audios"), true);
    auto styles = str_list(field(raw, "textStyles"), false);
    if (!sfx || !audios || !styles) {
        return std::nullopt;
    }
    return Json{{"sfx", *sfx}, {"audios", *audios}, {"textStyles", *styles}};
}

Json api_keys_map(const Json& raw) {
    Json out = Json::object();
    const Json& keys = field(raw, "api_keys");
    if (keys.is_object()) {
        for (const auto& [k, v] : keys.items()) {
            const std::string kid = py_strip(k);
            const std::string val = secret(v);
            if (!kid.empty() && !val.empty()) {
                out[kid] = val;
            }
        }
    }
    if (raw.is_object()) {
        const std::string legacy = secret(field(raw, "gemini_api_key"));
        if (!legacy.empty() && !out.contains("gemini")) {
            out["gemini"] = legacy;
        }
    }
    return out;
}

// `x or {}` antes de `{**x}`: lo que no es un dict (y es verdadero) es un error.
Result<Json> mapping(const Json& v, std::string_view what) {
    if (!py_truthy(v)) {
        return Json::object();
    }
    if (!v.is_object()) {
        return invalid(std::string(what) + " debe ser un objeto");
    }
    return v;
}

// {**(a or {}), **(b or {})}
Result<Json> spread(const Json& a, const Json& b, std::string_view what) {
    GC_TRY(out, mapping(a, what));
    GC_TRY(more, mapping(b, what));
    for (const auto& [k, v] : more.items()) {
        out[k] = v;
    }
    return out;
}

Result<std::vector<std::string>> keys_list_from(const Json& d, std::string_view provider) {
    if (!d.is_object()) {
        return invalid("settings.json no es un objeto");
    }
    GC_TRY(ak, mapping(field(d, "api_keys"), "api_keys"));
    std::vector<std::string> out;
    const Json& prim_raw = field(ak, provider);
    const std::string prim = py_strip(py_truthy(prim_raw) ? py_str(prim_raw) : std::string());
    if (!prim.empty()) {
        out.push_back(prim);
    }
    GC_TRY(extra, mapping(field(d, "api_keys_extra"), "api_keys_extra"));
    const Json& list = field(extra, provider);
    if (py_truthy(list)) {
        const auto items = py_iter(list);
        if (!items) {
            return invalid("api_keys_extra debe tener listas");
        }
        for (const Json& k : *items) {
            const std::string s = py_strip(py_truthy(k) ? py_str(k) : std::string());
            if (!s.empty()) {
                out.push_back(s);
            }
        }
    }
    return out;
}

Result<void> set_keys_list(Json& d, const std::string& provider, const std::vector<std::string>& raw_keys) {
    if (!d.is_object()) {
        return invalid("settings.json no es un objeto");
    }
    if (!d.contains("api_keys")) {
        d["api_keys"] = Json::object();
    }
    if (!d.contains("api_keys_extra")) {
        d["api_keys_extra"] = Json::object();
    }
    Json& ak = d["api_keys"];
    Json& ex = d["api_keys_extra"];
    if (!ak.is_object() || !ex.is_object()) {
        return invalid("api_keys y api_keys_extra deben ser objetos");
    }
    std::vector<std::string> keys;
    for (const auto& k : raw_keys) {
        if (auto s = py_strip(k); !s.empty()) {
            keys.push_back(std::move(s));
        }
    }
    if (!keys.empty()) {
        ak[provider] = keys.front();
        if (keys.size() > 1) {
            ex[provider] = std::vector<std::string>(keys.begin() + 1, keys.end());
        } else {
            ex.erase(provider);
        }
    } else {
        ak.erase(provider);
        ex.erase(provider);
    }
    if (provider == "gemini") {
        if (py_truthy(field(ak, "gemini"))) {
            d["gemini_api_key"] = ak["gemini"];
        } else {
            d.erase("gemini_api_key");
        }
    }
    return {};
}

Json transcribe_public(const Json& cfg_raw) {
    Json cfg = normalize_transcribe_settings(cfg_raw);
    static const std::array<std::array<std::string_view, 3>, 5> kInfo{{
        {"tiny", "Tiny", "Más rápido, menos preciso (~75 MB)"},
        {"base", "Base", "Equilibrio velocidad / calidad (~140 MB)"},
        {"small", "Small", "Mejor precisión, un poco más lento (~460 MB)"},
        {"medium", "Medium", "Alta precisión, más lento (~1.5 GB)"},
        {"large-v3", "Large v3", "Máxima precisión, batched pipeline (~3 GB)"},
    }};
    Json models = Json::array();
    for (const auto& [id, label, hint] : kInfo) {
        models.push_back(Json{{"id", id}, {"label", label}, {"hint", hint}});
    }
    cfg["models"] = std::move(models);
    return cfg;
}

} // namespace

Result<Json> normalize_export_settings(const Json& raw) {
    static const Json kEmpty = Json::object();
    const Json& data = raw.is_object() ? raw : kEmpty;
    const Json& fps_raw = field(data, "fps");
    const auto fps_n = py_int(py_truthy(fps_raw) ? fps_raw : Json(30));
    std::int64_t fps = fps_n.value_or(30);
    if (std::ranges::find(kFpsChoices, fps) == kFpsChoices.end()) {
        fps = 30;
    }
    const Json& q_raw = field(data, "quality");
    const Json quality = py_truthy(q_raw) ? q_raw : Json("standard");
    if (quality.is_array() || quality.is_object()) {
        return invalid("export.quality no es válido"); // `[...] in dict`: no se puede buscar
    }
    const bool known = quality.is_string() &&
                       std::ranges::find(kQualities, quality.get_ref<const std::string&>()) != kQualities.end();
    return Json{{"fps", fps}, {"quality", known ? quality : Json("standard")}};
}

Json normalize_transcribe_settings(const Json& raw) {
    static const Json kEmpty = Json::object();
    const Json& data = raw.is_object() ? raw : kEmpty;
    const Json& m_raw = field(data, "model");
    const Json model = py_truthy(m_raw) ? m_raw : Json("base");
    const bool known =
        model.is_string() && std::ranges::find(kModels, model.get_ref<const std::string&>()) != kModels.end();
    return Json{{"model", known ? model : Json("base")}};
}

Json SettingsFile::read_raw() const {
    std::error_code ec;
    if (!std::filesystem::exists(file_, ec)) {
        return Json::object();
    }
    auto raw = read_json_file(file_);
    return raw ? *raw : Json::object();
}

Json SettingsFile::load_unlocked() const {
    std::error_code ec;
    if (!std::filesystem::exists(file_, ec)) {
        return defaults();
    }
    auto raw = read_json_file(file_);
    if (!raw || !raw->is_object()) {
        return defaults();
    }
    Json out = defaults(); // {**DEFAULTS, **data}
    for (const auto& [k, v] : raw->items()) {
        out[k] = v;
    }
    auto fav = merge_favorites(field(out, "favorites"));
    if (!fav) {
        return defaults();
    }
    out["favorites"] = std::move(*fav);
    out["api_keys"] = api_keys_map(out);
    return out;
}

Json SettingsFile::load() const { return load_unlocked(); }

Result<Json> SettingsFile::save(const Json& patch) {
    const std::lock_guard lock(mutex_);
    Json current = load_unlocked();
    GC_TRY(incoming, mapping(patch, "ajustes"));
    incoming.erase("gemini_api_key_set");

    Json merged = field(current, "api_keys").is_object() ? current["api_keys"] : Json::object();
    Json keys_patch;
    if (incoming.contains("api_keys")) {
        keys_patch = incoming["api_keys"];
        incoming.erase("api_keys");
    }
    if (incoming.contains("gemini_api_key")) {
        const std::string text = secret(incoming["gemini_api_key"]);
        incoming.erase("gemini_api_key");
        if (!text.empty()) {
            merged["gemini"] = text;
        }
    }
    if (keys_patch.is_object()) {
        for (const auto& [k, v] : keys_patch.items()) {
            const std::string kid = py_strip(k);
            if (kid.empty() || v.is_boolean()) {
                continue;
            }
            const std::string val = v.is_null() ? std::string() : py_strip(py_str(v));
            if (is_mask(val)) {
                if (val.empty()) {
                    merged.erase(kid); // un texto vacío borra la clave; una máscara la deja
                }
                continue;
            }
            merged[kid] = val;
        }
    }
    if (incoming.contains("export")) {
        GC_TRY(raw, spread(field(current, "export"), incoming["export"], "export"));
        GC_TRY(norm, normalize_export_settings(raw));
        incoming["export"] = std::move(norm);
    }
    if (incoming.contains("transcribe")) {
        GC_TRY(raw, spread(field(current, "transcribe"), incoming["transcribe"], "transcribe"));
        incoming["transcribe"] = normalize_transcribe_settings(raw);
    }
    if (incoming.contains("bg_removal")) {
        GC_TRY(raw, spread(field(current, "bg_removal"), incoming["bg_removal"], "bg_removal"));
        const Json& d = field(raw, "device");
        const Json& p = field(raw, "provider");
        const std::string dev = py_truthy(d) ? py_str(d) : "auto";
        const std::string prov = py_truthy(p) ? py_str(p) : "u2net";
        const bool dev_ok = dev == "auto" || dev == "cuda" || dev == "dml" || dev == "cpu";
        incoming["bg_removal"] =
            Json{{"device", dev_ok ? dev : "auto"}, {"provider", is_bg_provider(prov) ? prov : "u2net"}};
    }
    if (incoming.contains("models")) {
        GC_TRY(raw, spread(field(current, "models"), incoming["models"], "models"));
        const auto f = py_float(raw.contains("idle_minutes") ? raw.at("idle_minutes") : Json(5));
        const double idle = f ? std::min(240.0, std::max(0.0, *f)) : 5.0;
        incoming["models"] = Json{{"idle_minutes", idle}};
    }

    for (const auto& [k, v] : incoming.items()) {
        current[k] = v;
    }
    current["api_keys"] = merged;
    if (py_truthy(field(merged, "gemini"))) {
        current["gemini_api_key"] = merged["gemini"];
    } else {
        current.erase("gemini_api_key");
    }
    std::error_code ec;
    std::filesystem::create_directories(file_.parent_path(), ec);
    if (auto w = write_json_file(file_, current, 2); !w) {
        return std::unexpected(w.error());
    }
    return current;
}

Result<Json> SettingsFile::public_view() const {
    Json data = load();
    const Json keys = api_keys_map(data);
    GC_TRY(extra, mapping(field(data, "api_keys_extra"), "api_keys_extra"));
    std::set<std::string> providers; // ordenados: en Python, el orden de un set
    for (const auto& [k, v] : keys.items()) {
        providers.insert(k);
    }
    for (const auto& [k, v] : extra.items()) {
        providers.insert(k);
    }
    Json counts = Json::object();
    for (const auto& p : providers) {
        GC_TRY(list, keys_list_from(data, p));
        if (!list.empty()) {
            counts[p] = list.size();
        }
    }
    data.erase("gemini_api_key");
    data.erase("api_keys_extra"); // nunca a la interfaz
    data["gemini_api_key_set"] = py_truthy(field(keys, "gemini"));
    Json flags = Json::object();
    for (const auto& [k, v] : keys.items()) {
        flags[k] = true;
    }
    data["api_keys"] = std::move(flags);
    data["api_keys_counts"] = std::move(counts);
    GC_TRY(exp, normalize_export_settings(field(data, "export")));
    data["export"] = std::move(exp);
    const Json& tx = field(data, "transcribe");
    // public_view(None) usa transcribe_settings.load(): lo guardado en «transcribe».
    data["transcribe"] = transcribe_public(tx.is_null() ? normalize_transcribe_settings(field(load(), "transcribe")) : tx);
    return data;
}

std::string SettingsFile::api_key(std::string_view provider) const {
    const std::string kid = py_strip(provider);
    if (kid.empty()) {
        return {};
    }
    const Json keys = api_keys_map(load());
    const Json& v = field(keys, kid);
    return py_strip(py_truthy(v) ? py_str(v) : std::string());
}

Result<std::vector<std::string>> SettingsFile::keys_for(std::string_view provider) const {
    const std::string kid = py_strip(provider);
    if (kid.empty()) {
        return std::vector<std::string>{};
    }
    return keys_list_from(load(), kid);
}

Result<std::size_t> SettingsFile::add_key(std::string_view provider, std::string_view value) {
    const std::string kid = py_strip(provider);
    const std::string val = py_strip(value);
    if (kid.empty()) {
        return invalid("Proveedor vacío.");
    }
    if (val.empty()) {
        return invalid("La API key está vacía.");
    }
    const std::lock_guard lock(mutex_);
    Json d = read_raw();
    GC_TRY(keys, keys_list_from(d, kid));
    keys.push_back(val);
    if (auto r = set_keys_list(d, kid, keys); !r) {
        return std::unexpected(r.error());
    }
    if (auto w = write_json_file(file_, d, 2); !w) {
        return std::unexpected(w.error());
    }
    return keys.size();
}

Result<std::size_t> SettingsFile::set_key(std::string_view provider, std::int64_t index, std::string_view value) {
    const std::string kid = py_strip(provider);
    const std::string val = py_strip(value);
    if (val.empty()) {
        return invalid("La API key está vacía.");
    }
    const std::lock_guard lock(mutex_);
    Json d = read_raw();
    GC_TRY(keys, keys_list_from(d, kid));
    if (index >= 0 && static_cast<std::size_t>(index) < keys.size()) {
        keys[static_cast<std::size_t>(index)] = val;
    } else {
        keys.push_back(val);
    }
    if (auto r = set_keys_list(d, kid, keys); !r) {
        return std::unexpected(r.error());
    }
    if (auto w = write_json_file(file_, d, 2); !w) {
        return std::unexpected(w.error());
    }
    return keys.size();
}

Result<std::size_t> SettingsFile::remove_key(std::string_view provider, std::int64_t index) {
    const std::string kid = py_strip(provider);
    const std::lock_guard lock(mutex_);
    Json d = read_raw();
    GC_TRY(keys, keys_list_from(d, kid));
    if (index >= 0 && static_cast<std::size_t>(index) < keys.size()) {
        keys.erase(keys.begin() + index);
    }
    if (auto r = set_keys_list(d, kid, keys); !r) {
        return std::unexpected(r.error());
    }
    if (auto w = write_json_file(file_, d, 2); !w) {
        return std::unexpected(w.error());
    }
    return keys.size();
}

} // namespace gc::core
