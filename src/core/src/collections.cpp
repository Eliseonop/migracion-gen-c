#include "gc/core/collections.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <exception>
#include <format>
#include <set>
#include <system_error>
#include <tuple>
#include <utility>

#include "disk_scan.hpp"
#include "gc/core/io.hpp"
#include "json_reader.hpp"
#include "py.hpp"

namespace gc::core {

namespace fs = std::filesystem;
using detail::py_lower;
using detail::py_path_name;
using detail::py_path_stem;
using detail::py_path_str;
using detail::py_path_suffix;
using detail::py_quote;
using detail::py_split;
using detail::py_str;
using detail::py_strip;
using detail::py_truthy;

namespace {

const Json kNull;
const Json kEmptyObject = Json::object();

constexpr std::array<std::string_view, 4> kKinds{"video", "image", "gif", "audio"};
constexpr std::array<std::string_view, 10> kVideoExt{".mp4", ".mov", ".mkv", ".webm", ".avi",
                                                     ".m4v", ".mpg", ".mpeg", ".wmv", ".flv"};
constexpr std::array<std::string_view, 6> kImageExt{".png", ".jpg", ".jpeg", ".webp", ".bmp", ".gif"};
constexpr std::array<std::string_view, 7> kAudioExt{".mp3", ".wav", ".m4a", ".aac", ".ogg", ".flac", ".wma"};

// d.get(key, default) de Python sobre un objeto JSON.
const Json& get_or(const Json& obj, std::string_view key, const Json& fallback) {
    const auto it = obj.find(key); // end() si no es un objeto
    return it == obj.end() ? fallback : *it;
}

// re.sub(r'[<>:"/\\|?*\x00-\x1f]', "_", text).strip(" .") de create_collection y add_files:
// lo que Windows no admite en un nombre. Son caracteres ASCII, así que se cambian byte a
// byte sin tocar el resto del UTF-8.
std::string clean_name(std::string_view text) {
    constexpr std::string_view kBad = R"(<>:"/\|?*)";
    std::string out(text);
    for (char& c : out) {
        if (static_cast<unsigned char>(c) < 0x20 || kBad.contains(c)) {
            c = '_';
        }
    }
    const auto first = out.find_first_not_of(" .");
    if (first == std::string::npos) {
        return {};
    }
    return out.substr(first, out.find_last_not_of(" .") - first + 1);
}

// _meta(): collections_meta de los ajustes si es un dict; si no, vacío.
Json collections_meta(const Json& settings) {
    const Json& raw = get_or(settings, "collections_meta", kNull);
    return raw.is_object() ? raw : Json::object();
}

// str(path) de Python.
std::string path_str(const fs::path& p) { return py_path_str(path_to_utf8(p)); }

// _first_image_url: la URL del primer medio que es imagen o GIF.
std::string first_image_url(const std::vector<Json>& items) {
    for (const Json& it : items) {
        const Json& kind = get_or(it, "kind", kNull);
        if (kind == "image" || kind == "gif") {
            return py_str(get_or(it, "url", Json("")));
        }
    }
    return {};
}

} // namespace

std::optional<std::string_view> collection_kind(std::string_view filename) {
    const std::string ext = py_lower(py_path_suffix(filename));
    const auto in = [&](const auto& list) { return std::ranges::find(list, ext) != list.end(); };
    if (ext == ".gif") {
        return "gif";
    }
    if (in(kVideoExt)) {
        return "video";
    }
    if (in(kImageExt)) {
        return "image";
    }
    if (in(kAudioExt)) {
        return "audio";
    }
    return std::nullopt;
}

std::optional<fs::path> collections_root(const Json& settings, const fs::path& default_dir) {
    return detail::dir_from_settings(settings, "material_root", default_dir);
}

Result<std::optional<std::string>> set_collections_root(SettingsFile& settings, std::string_view path) {
    const std::string text = py_path_str(py_strip(path)); // str(Path(path.strip())): «» es «.»
    std::error_code ec;
    if (text.contains('\0') || !fs::is_directory(path_from_utf8(text), ec)) {
        return std::optional<std::string>();
    }
    if (auto saved = settings.save(Json{{"material_root", text}}); !saved) {
        return std::unexpected(saved.error());
    }
    return std::optional<std::string>(text);
}

Result<std::string> safe_collection_id(std::string_view raw) {
    const std::string text = py_strip(raw);
    std::string name = py_path_name(text);
    if (name.empty() || name == "." || name == ".." || name != text) {
        return fail(ErrorCode::InvalidArgument, "Colección no válida.");
    }
    return name;
}

Result<Json> set_collection_prefs(SettingsFile& settings, std::string_view collection_id,
                                  std::optional<bool> enabled, std::optional<bool> favorite,
                                  std::optional<std::string_view> label) {
    auto cid = safe_collection_id(collection_id);
    if (!cid) {
        return std::unexpected(cid.error());
    }
    Json meta = collections_meta(settings.load());
    const Json& prev = get_or(meta, *cid, kNull);
    if (py_truthy(prev) && !prev.is_object()) { // dict(x): Python acepta una lista de pares; aquí no
        return fail(ErrorCode::InvalidArgument, "collections_meta: las preferencias de " + *cid + " no son un objeto");
    }
    Json cur = prev.is_object() ? prev : Json::object();
    if (enabled) {
        cur["enabled"] = *enabled;
    }
    if (favorite) {
        cur["favorite"] = *favorite;
    }
    if (label) {
        if (std::string text = py_strip(*label); !text.empty()) {
            cur["label"] = std::move(text);
        } else {
            cur.erase("label");
        }
    }
    meta[*cid] = cur;
    if (auto saved = settings.save(Json{{"collections_meta", meta}}); !saved) {
        return std::unexpected(saved.error());
    }
    return cur;
}

Json collection_entry(std::string_view collection, std::string_view rel) {
    std::string relstr = py_path_str(rel); // str(rel).replace("\\", "/")
    std::ranges::replace(relstr, '\\', '/');
    const std::string ref = std::string(collection) + "/" + relstr;
    const auto kind = collection_kind(rel);
    Json out = Json::object();
    out["id"] = "col_" + ref;
    out["collection"] = std::string(collection);
    out["name"] = py_path_stem(rel);
    out["kind"] = kind ? Json(std::string(*kind)) : Json();
    out["filename"] = ref;
    out["url"] = "/api/collections/file/" + py_quote(ref);
    return out;
}

std::vector<Json> collection_scan(const fs::path& base, std::string_view collection) {
    std::vector<Json> items;
    // Con un NUL, is_dir() da False en Python; Windows cortaría la ruta en él.
    const fs::path dir = base / path_from_utf8(collection);
    std::error_code ec;
    if (collection.contains('\0') || !fs::is_directory(dir, ec)) {
        return items;
    }
    const auto found =
        detail::sorted_files(dir, [](std::string_view name) { return collection_kind(name).has_value(); });
    items.reserve(found.size());
    for (const auto& parts : found) {
        std::string rel;
        for (const auto& part : parts) {
            rel += (rel.empty() ? "" : "/") + part;
        }
        items.push_back(collection_entry(collection, rel));
    }
    return items;
}

Result<Json> list_collections(const std::optional<fs::path>& base, const Json& settings) {
    if (!base) {
        return Json{{"available", false}, {"root", nullptr}, {"collections", Json::array()}};
    }
    // sorted(p for p in base.iterdir() if p.is_dir()): los nombres, sin distinguir mayúsculas.
    struct Sub {
        std::string key;
        std::string name;
    };
    std::vector<Sub> subs;
    std::error_code ec;
    for (fs::directory_iterator it(*base, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code ec_dir;
        if (!it->is_directory(ec_dir)) { // como is_dir(): sigue los enlaces
            continue;
        }
        try {
            std::string name = path_to_utf8(it->path().filename());
            subs.push_back({py_lower(name), std::move(name)});
        } catch (const std::exception&) {
            continue; // un nombre que no es UTF-16 válido
        }
    }
    std::ranges::stable_sort(subs, {}, &Sub::key);

    const Json meta = collections_meta(settings);
    struct Listed {
        bool not_favorite;
        std::string name_key;
        Json value;
    };
    std::vector<Listed> listed;
    for (const auto& sub : subs) {
        const Json& raw = get_or(meta, sub.name, kNull);
        const Json& pref = raw.is_object() ? raw : kEmptyObject;
        const auto items = collection_scan(*base, sub.name);
        const Json& label = get_or(pref, "label", kNull);
        Json name = py_truthy(label) ? label : Json(sub.name);
        if (!name.is_string()) { // c["name"].lower() al ordenar: AttributeError
            return fail(ErrorCode::InvalidArgument, "collections_meta: la etiqueta de " + sub.name + " no es texto");
        }
        const fs::path dir = *base / path_from_utf8(sub.name);
        std::error_code ec_stick;
        Json c = Json::object();
        c["id"] = sub.name;
        c["name"] = name;
        c["path"] = path_str(dir);
        c["count"] = items.size();
        c["enabled"] = py_truthy(get_or(pref, "enabled", Json(true)));
        c["favorite"] = py_truthy(get_or(pref, "favorite", Json(false)));
        c["cover_url"] = first_image_url(items);
        // Carpeta de un personaje stick: la Biblioteca la abre agrupada por expresión.
        c["stick"] = fs::is_regular_file(dir / "stick.json", ec_stick);
        const bool favorite = c["favorite"].get<bool>();
        listed.push_back({!favorite, py_lower(name.get_ref<const std::string&>()), std::move(c)});
    }
    // Favoritas primero, luego por nombre: (not favorite, name.lower()).
    std::ranges::stable_sort(listed, [](const Listed& a, const Listed& b) {
        return std::tie(a.not_favorite, a.name_key) < std::tie(b.not_favorite, b.name_key);
    });
    Json collections = Json::array();
    for (auto& l : listed) {
        collections.push_back(std::move(l.value));
    }
    return Json{{"available", true}, {"root", path_str(*base)}, {"collections", std::move(collections)}};
}

bool collection_match(const Json& item, std::string_view q) {
    if (q.empty()) {
        return true;
    }
    const std::string hay = py_lower(py_str(get_or(item, "name", Json(""))) + " " +
                                     py_str(get_or(item, "collection", Json(""))));
    return std::ranges::all_of(py_split(py_lower(q)), [&](const std::string& tok) { return hay.contains(tok); });
}

Result<Json> collections_search(const std::optional<fs::path>& base, const Json& settings, std::string_view q,
                                std::string_view kind, std::string_view collection, std::int64_t limit) {
    auto listed = list_collections(base, settings);
    if (!listed) {
        return std::unexpected(listed.error());
    }
    Json cols = std::move((*listed)["collections"]);
    std::set<std::string> enabled; // sorted(enabled): orden de puntos de código = el de los bytes UTF-8
    std::set<std::string> favorite;
    for (const Json& c : cols) {
        const auto& id = c.at("id").get_ref<const std::string&>();
        if (c.at("enabled").get<bool>()) {
            enabled.insert(id);
        }
        if (c.at("favorite").get<bool>()) {
            favorite.insert(id);
        }
    }
    const std::string want_kind = py_lower(py_strip(kind));
    const std::string want_coll = py_strip(collection);
    const std::string ql = py_strip(q);

    Json counts = Json::object();
    for (const auto k : kKinds) {
        counts[std::string(k)] = 0;
    }
    std::vector<Json> items;
    if (base) {
        for (const auto& cid : enabled) {
            if (!want_coll.empty() && cid != want_coll) {
                continue;
            }
            for (Json& it : collection_scan(*base, cid)) {
                if (!collection_match(it, ql)) {
                    continue;
                }
                const auto& k = it.at("kind").get_ref<const std::string&>();
                counts[k] = counts.value(k, std::int64_t{0}) + 1;
                if (!want_kind.empty() && k != want_kind) {
                    continue;
                }
                it["favorite"] = favorite.contains(cid);
                items.push_back(std::move(it));
            }
        }
    }
    std::int64_t all = 0;
    for (const auto k : kKinds) {
        all += counts[std::string(k)].get<std::int64_t>();
    }
    counts["all"] = all;

    const std::size_t total = items.size();
    // items[:limit]: con un límite negativo, todos menos los |limit| últimos.
    const auto n = static_cast<std::int64_t>(total);
    items.resize(static_cast<std::size_t>(limit >= 0 ? std::min(n, limit) : std::max<std::int64_t>(0, n + limit)));

    Json out = Json::object();
    out["available"] = base.has_value();
    out["root"] = base ? Json(path_str(*base)) : Json();
    out["counts"] = std::move(counts);
    out["total"] = total;
    out["items"] = std::move(items);
    out["collections"] = std::move(cols);
    return out;
}

Result<std::string> create_collection(const std::optional<fs::path>& base, std::string_view name) {
    if (!base) {
        return fail(ErrorCode::InvalidArgument, "No hay carpeta raíz de material.");
    }
    const std::string clean = clean_name(name);
    if (clean.empty()) {
        return fail(ErrorCode::InvalidArgument, "Ponle un nombre a la carpeta.");
    }
    std::string cid = clean;
    std::error_code ec;
    for (int n = 2; fs::exists(*base / path_from_utf8(cid), ec); ++n) {
        cid = std::format("{} {}", clean, n);
    }
    if (!fs::create_directories(*base / path_from_utf8(cid), ec) || ec) {
        return fail(ErrorCode::Io, "no se pudo crear la carpeta " + cid + ": " + ec.message());
    }
    return cid;
}

Result<Json> add_collection_files(const std::optional<fs::path>& base, std::string_view collection,
                                  const std::vector<IncomingFile>& files) {
    auto cid = safe_collection_id(collection);
    if (!cid) {
        return std::unexpected(cid.error());
    }
    std::error_code ec;
    // safe_collection_id deja pasar un NUL; is_dir() da False con él (Windows cortaría la ruta).
    if (!base || cid->contains('\0') || !fs::is_directory(*base / path_from_utf8(*cid), ec)) {
        return fail(ErrorCode::NotFound, "Carpeta no encontrada.");
    }
    const fs::path folder = *base / path_from_utf8(*cid);
    Json saved = Json::array();
    Json errors = Json::array();
    for (const IncomingFile& f : files) {
        // Path(str(raw_name or "")).name, saneado.
        const std::string name = clean_name(py_path_name(py_truthy(f.name) ? py_str(f.name) : std::string()));
        if (name.empty() || !collection_kind(name)) {
            errors.push_back(Json{{"file", f.name}, {"error", "Tipo de archivo no soportado."}});
            continue;
        }
        const std::string stem = py_path_stem(name);
        const std::string suffix = py_path_suffix(name);
        std::string target = name;
        for (int n = 2; fs::exists(folder / path_from_utf8(target), ec); ++n) {
            target = std::format("{}_{}{}", stem, n, suffix);
        }
        if (!f.write) {
            return fail(ErrorCode::InvalidArgument, "falta el contenido de " + target);
        }
        if (auto written = f.write(folder / path_from_utf8(target)); !written) {
            return std::unexpected(written.error());
        }
        saved.push_back(collection_entry(*cid, target));
    }
    return Json{{"saved", std::move(saved)}, {"errors", std::move(errors)}};
}

std::optional<fs::path> collection_resolve(const std::optional<fs::path>& base, std::string_view ref) {
    if (!base || ref.empty()) {
        return std::nullopt;
    }
    std::string rel(ref); // str(ref).replace("\\", "/"): en Linux también es un separador
    std::ranges::replace(rel, '\\', '/');
    auto target = detail::resolve_inside(*base, rel);
    std::error_code ec;
    if (!target || !fs::is_regular_file(*target, ec)) {
        return std::nullopt;
    }
    return target;
}

} // namespace gc::core
