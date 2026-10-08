#include "gc/core/sfx_disk.hpp"

#include <algorithm>
#include <exception>
#include <system_error>
#include <utility>

#include "gc/core/io.hpp"
#include "json_reader.hpp"
#include "py.hpp"

namespace gc::core {

namespace fs = std::filesystem;
using detail::py_iter;
using detail::py_lower;
using detail::py_path_stem;
using detail::py_path_str;
using detail::py_path_suffix;
using detail::py_quote;
using detail::py_round;
using detail::py_split;
using detail::py_str;
using detail::py_strip;
using detail::py_truthy;

namespace {

const Json kNull;
const Json kEmptyText = "";

// d.get(key, default) de Python sobre un objeto JSON.
const Json& get_or(const Json& obj, std::string_view key, const Json& fallback) {
    const auto it = obj.find(key);
    return it == obj.end() ? fallback : *it;
}

// Las partes de una ruta en minúsculas, para compararlas como WindowsPath (sin las vacías
// de una barra final).
std::vector<std::string> lowered_parts(const fs::path& p) {
    std::vector<std::string> out;
    for (const fs::path& part : p) {
        std::string s = path_to_utf8(part);
        if (!s.empty()) {
            out.push_back(py_lower(s));
        }
    }
    return out;
}

// Las categorías y los sonidos de sfx_library.json. false donde Python lanzaría: las
// categorías leídas hasta entonces se quedan y los sonidos los pone el escaneo.
bool read_index(const fs::path& file, std::vector<Json>& categories, std::vector<Json>& items) {
    const auto data = read_json_file_py(file);
    if (!data || !data->is_object()) {
        return false; // no se puede leer, no es JSON o data.get falla
    }
    const Json& cats = get_or(*data, "categorias", kNull);
    if (py_truthy(cats)) {
        if (!cats.is_object()) {
            return false; // .items() de algo que no es un dict
        }
        for (const auto& [k, v] : cats.items()) {
            if (py_truthy(v) && !v.is_object()) {
                return false; // (v or {}).get
            }
            const Json id = k;
            Json cat = Json::object();
            cat["id"] = id;
            cat["label"] = v.is_object() ? get_or(v, "etiqueta", id) : id;
            cat["count"] = v.is_object() ? get_or(v, "cantidad", Json(0)) : Json(0);
            categories.push_back(std::move(cat));
        }
    }
    const Json& sounds = get_or(*data, "sonidos", kNull);
    if (py_truthy(sounds)) {
        const auto seq = py_iter(sounds);
        if (!seq) {
            return false;
        }
        for (const Json& s : *seq) {
            if (!s.is_object()) {
                return false; // s.get
            }
            auto entry = sfx_entry(get_or(s, "carpeta", kEmptyText), get_or(s, "archivo", kEmptyText),
                                   get_or(s, "categoria", kEmptyText), get_or(s, "uso_tipico", kEmptyText),
                                   get_or(s, "sonido", kNull));
            if (!entry) {
                return false;
            }
            items.push_back(std::move(*entry));
        }
    }
    return true;
}

bool is_favorite(const Json& item, const std::set<std::string>& favorites) {
    const Json& id = get_or(item, "id", kNull);
    return id.is_string() && favorites.contains(id.get_ref<const std::string&>());
}

} // namespace

bool is_sfx_audio(std::string_view filename) {
    const std::string ext = py_lower(py_path_suffix(filename));
    return ext == ".mp3" || ext == ".wav" || ext == ".ogg" || ext == ".m4a" || ext == ".aac" || ext == ".flac";
}

std::optional<fs::path> sfx_base(const Json& settings, const fs::path& default_dir) {
    std::error_code ec;
    const Json& custom = settings.is_object() ? get_or(settings, "sfx_folder", kNull) : kNull;
    if (custom.is_string()) {
        const auto& text = custom.get_ref<const std::string&>();
        // Con un NUL, Path.exists() da False; Windows cortaría la ruta en él.
        if (!text.empty() && text.find('\0') == std::string::npos) {
            fs::path p = path_from_utf8(text);
            if (fs::is_directory(p, ec)) {
                return p;
            }
        }
    }
    if (fs::is_directory(default_dir, ec)) {
        return default_dir;
    }
    return std::nullopt;
}

Result<Json> sfx_entry(const Json& folder, const Json& filename, const Json& category, const Json& uso,
                       const Json& name) {
    const Json rel = py_truthy(folder) ? Json(py_str(folder) + "/" + py_str(filename)) : filename;
    Json shown;
    if (py_truthy(name)) {
        shown = name;
    } else if (filename.is_string()) {
        shown = py_path_stem(filename.get_ref<const std::string&>());
    } else {
        return fail(ErrorCode::InvalidArgument, "el archivo del sonido debe ser texto"); // Path(x): TypeError
    }
    if (!rel.is_string()) {
        return fail(ErrorCode::InvalidArgument, "el archivo del sonido debe ser texto"); // quote(x): TypeError
    }
    Json out = Json::object();
    out["id"] = rel;
    out["name"] = std::move(shown);
    out["file"] = filename;
    out["folder"] = folder;
    out["category"] = py_truthy(category) ? category : folder;
    out["uso"] = uso;
    out["url"] = "/api/sfx/file/" + py_quote(rel.get_ref<const std::string&>());
    return out;
}

std::vector<Json> sfx_scan(const fs::path& base) {
    struct Found {
        std::vector<std::string> key; // partes en minúsculas: el orden de sorted() de WindowsPath
        std::vector<std::string> parts;
    };
    std::vector<Found> found;
    std::error_code ec;
    fs::recursive_directory_iterator it(base, fs::directory_options::skip_permission_denied, ec);
    for (const fs::recursive_directory_iterator end{}; !ec && it != end; it.increment(ec)) {
        std::error_code ec_file;
        if (!it->is_regular_file(ec_file)) { // como is_file(): sigue los enlaces
            continue;
        }
        try {
            std::vector<std::string> all;
            for (const fs::path& part : it->path()) {
                all.push_back(path_to_utf8(part));
            }
            // Las últimas depth() + 1 partes son la ruta relativa a la carpeta.
            const auto depth = static_cast<std::size_t>(it.depth()) + 1;
            if (all.size() < depth || !is_sfx_audio(all.back())) {
                continue;
            }
            Found f;
            f.parts.assign(all.end() - static_cast<std::ptrdiff_t>(depth), all.end());
            for (const auto& p : f.parts) {
                f.key.push_back(py_lower(p));
            }
            found.push_back(std::move(f));
        } catch (const std::exception&) {
            continue; // un nombre que no es UTF-16 válido
        }
    }
    std::ranges::stable_sort(found, {}, &Found::key);
    std::vector<Json> items;
    items.reserve(found.size());
    for (const auto& f : found) {
        std::string folder;
        for (std::size_t k = 0; k + 1 < f.parts.size(); ++k) {
            folder += (k > 0 ? "/" : "") + f.parts[k];
        }
        if (auto entry = sfx_entry(folder, f.parts.back())) {
            items.push_back(std::move(*entry));
        }
    }
    return items;
}

Json sfx_load_index(const std::optional<fs::path>& base) {
    if (!base) {
        return Json{{"available", false}, {"base", nullptr}, {"categories", Json::array()}, {"items", Json::array()}};
    }
    std::vector<Json> categories;
    std::vector<Json> items;
    std::error_code ec;
    const fs::path file = *base / fs::path(kSfxIndexFile);
    if (!fs::exists(file, ec) || !read_index(file, categories, items)) {
        items = sfx_scan(*base);
    }
    Json out = Json::object();
    out["available"] = true;
    out["base"] = py_path_str(path_to_utf8(*base));
    out["categories"] = std::move(categories);
    out["items"] = std::move(items);
    return out;
}

std::set<std::string> sfx_favorite_ids(const Json& settings) {
    std::set<std::string> out;
    const Json& favorites = settings.is_object() ? get_or(settings, "favorites", kNull) : kNull;
    if (!favorites.is_object()) {
        return out; // tras settings.load() siempre es un dict con listas de texto
    }
    const Json& sfx = get_or(favorites, "sfx", kNull);
    if (const auto seq = py_truthy(sfx) ? py_iter(sfx) : std::nullopt) {
        for (const Json& x : *seq) {
            out.insert(py_str(x));
        }
    }
    return out;
}

std::vector<Json> filter_sfx_items(const std::vector<Json>& items, std::string_view q, std::string_view category,
                                   const std::set<std::string>& favorites) {
    std::vector<Json> out;
    const std::string cat = py_lower(py_strip(category));
    const auto tokens = py_split(py_lower(py_strip(q)));
    for (const Json& it : items) {
        if (cat == kSfxFavorites) {
            if (!is_favorite(it, favorites)) {
                continue;
            }
        } else if (!cat.empty()) {
            const std::string where = py_lower(py_str(get_or(it, "folder", kEmptyText))) + " " +
                                      py_lower(py_str(get_or(it, "category", kEmptyText)));
            if (where.find(cat) == std::string::npos) {
                continue;
            }
        }
        if (!tokens.empty()) {
            const std::string hay = py_lower(
                py_str(get_or(it, "name", kEmptyText)) + " " + py_str(get_or(it, "category", kEmptyText)) + " " +
                py_str(get_or(it, "uso", kEmptyText)) + " " + py_str(get_or(it, "folder", kEmptyText)));
            const bool all = std::ranges::all_of(tokens, [&](const std::string& t) {
                return hay.find(t) != std::string::npos;
            });
            if (!all) {
                continue;
            }
        }
        out.push_back(it);
    }
    return out;
}

std::vector<Json> with_favorites_category(const std::vector<Json>& categories, const std::vector<Json>& items,
                                          const std::set<std::string>& favorites) {
    const auto count = std::ranges::count_if(items, [&](const Json& it) { return is_favorite(it, favorites); });
    std::vector<Json> out;
    out.push_back(Json{{"id", std::string(kSfxFavorites)}, {"label", "Favoritos"}, {"count", count}});
    for (const Json& c : categories) {
        const Json& id = get_or(c, "id", kNull);
        if (!(id.is_string() && id.get_ref<const std::string&>() == kSfxFavorites)) {
            out.push_back(c);
        }
    }
    return out;
}

Json sfx_search(const std::optional<fs::path>& base, const Json& settings, std::string_view q,
                std::string_view category, std::size_t limit) {
    Json idx = sfx_load_index(base);
    const auto favorites = sfx_favorite_ids(settings);
    const auto all = idx.at("items").get<std::vector<Json>>();
    auto items = filter_sfx_items(all, q, category, favorites);
    const std::size_t total = items.size();
    if (items.size() > limit) {
        items.resize(limit);
    }
    Json out = Json::object();
    out["available"] = idx.at("available");
    out["base"] = idx.at("base");
    out["categories"] = with_favorites_category(idx.at("categories").get<std::vector<Json>>(), all, favorites);
    out["total"] = total;
    out["items"] = std::move(items);
    return out;
}

std::optional<fs::path> sfx_resolve(const std::optional<fs::path>& base, std::string_view relpath) {
    if (!base || relpath.find('\0') != std::string_view::npos) {
        return std::nullopt;
    }
    std::error_code ec;
    const fs::path root = fs::weakly_canonical(fs::absolute(*base, ec), ec);
    if (ec) {
        return std::nullopt;
    }
    const fs::path target = fs::weakly_canonical(root / path_from_utf8(relpath), ec);
    if (ec) {
        return std::nullopt;
    }
    // base == target o base en target.parents, sin distinguir mayúsculas (WindowsPath).
    const auto r = lowered_parts(root);
    const auto t = lowered_parts(target);
    if (t.size() < r.size() || !std::equal(r.begin(), r.end(), t.begin())) {
        return std::nullopt;
    }
    if (!fs::exists(target, ec)) {
        return std::nullopt;
    }
    return target;
}

SfxLibrary load_sfx_library(const std::optional<fs::path>& base, const Json& settings, const SfxProbe& probe) {
    constexpr std::size_t kAll = 100000; // sfx.search("", limit=100000)
    const Json lib = sfx_search(base, settings, "", "", kAll);
    SfxLibrary out;
    for (const Json& item : lib.at("items")) {
        out.items.push_back(item);
        const std::string id = py_str(item.at("id"));
        if (const auto path = sfx_resolve(base, id)) {
            const auto seconds = probe ? probe(*path) : std::nullopt;
            out.durations[id] = seconds ? py_round(*seconds, 3) : 0.0;
        }
    }
    return out;
}

} // namespace gc::core
