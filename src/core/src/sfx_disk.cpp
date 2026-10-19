#include "gc/core/sfx_disk.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <ctime>
#include <format>
#include <fstream>
#include <system_error>
#include <utility>

#include "disk_scan.hpp"
#include "gc/core/io.hpp"
#include "json_reader.hpp"
#include "py.hpp"

namespace gc::core {

namespace fs = std::filesystem;
using detail::py_int;
using detail::py_iter;
using detail::py_lower;
using detail::py_path_name;
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
    return detail::dir_from_settings(settings, "sfx_folder", default_dir);
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
    const auto found = detail::sorted_files(base, is_sfx_audio);
    std::vector<Json> items;
    items.reserve(found.size());
    for (const auto& parts : found) {
        std::string folder;
        for (std::size_t k = 0; k + 1 < parts.size(); ++k) {
            folder += (k > 0 ? "/" : "") + parts[k];
        }
        if (auto entry = sfx_entry(folder, parts.back())) {
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
    out["base"] = detail::py_native_path_str(path_to_utf8(*base));
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
            if (!where.contains(cat)) {
                continue;
            }
        }
        if (!tokens.empty()) {
            const std::string hay = py_lower(
                py_str(get_or(it, "name", kEmptyText)) + " " + py_str(get_or(it, "category", kEmptyText)) + " " +
                py_str(get_or(it, "uso", kEmptyText)) + " " + py_str(get_or(it, "folder", kEmptyText)));
            const bool all = std::ranges::all_of(tokens, [&](const std::string& t) {
                return hay.contains(t);
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
    if (!base) {
        return std::nullopt;
    }
    auto target = detail::resolve_inside(*base, relpath);
    std::error_code ec;
    if (!target || !fs::exists(*target, ec)) {
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

// --- Escribir: add_sound, update_sound y create_category ----------------------------------

namespace {

const std::string kNoBase = "No hay biblioteca de SFX.";

std::unexpected<Error> invalid(std::string message) { return fail(ErrorCode::InvalidArgument, std::move(message)); }

// Lo que en Python es un TypeError o un AttributeError por un índice con tipos raros.
std::unexpected<Error> bad_index(std::string_view what) {
    return fail(ErrorCode::InvalidArgument, "sfx_library.json no tiene la forma esperada: " + std::string(what));
}

// date.today().isoformat()
std::string today() {
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    return std::format("{:04}-{:02}-{:02}", tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday);
}

// _slug: lo que no es [A-Za-z0-9] pasa a «_», sin «_» en los extremos, en mayúsculas y como
// mucho 40 caracteres; «SFX» si no queda nada.
std::string slug(std::string_view label) {
    std::string out;
    for (const char ch : py_strip(label)) {
        const auto c = static_cast<unsigned char>(ch);
        if (c < 128 && std::isalnum(c)) {
            out += static_cast<char>(std::toupper(c));
        } else if (out.empty() || out.back() != '_') {
            out += '_';
        }
    }
    const auto b = out.find_first_not_of('_');
    out = b == std::string::npos ? std::string() : out.substr(b, out.find_last_not_of('_') - b + 1).substr(0, 40);
    return out.empty() ? "SFX" : out;
}

// _next_cat_num: el mayor «NN_» de las categorías y de las carpetas de la biblioteca, más uno.
std::int64_t next_cat_num(const fs::path& base, const Json& cats) {
    std::vector<std::string> keys;
    for (const auto& [key, meta] : cats.items()) {
        keys.push_back(key);
    }
    std::error_code ec;
    for (fs::directory_iterator it(base, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code dir_ec;
        if (it->is_directory(dir_ec)) {
            keys.push_back(path_to_utf8(it->path().filename()));
        }
    }
    std::int64_t n = 0;
    for (const std::string& key : keys) {
        const auto digits = std::ranges::find_if_not(key, [](char c) { return c >= '0' && c <= '9'; }) - key.begin();
        std::int64_t v = 0;
        if (digits > 0 && static_cast<std::size_t>(digits) < key.size() && key[static_cast<std::size_t>(digits)] == '_' &&
            std::from_chars(key.data(), key.data() + digits, v).ec == std::errc()) {
            n = std::max(n, v);
        }
    }
    return n + 1;
}

// _unique_filename: «nombre_2.ext», «nombre_3.ext»… si ya existe.
std::string unique_filename(const fs::path& folder, const std::string& filename) {
    const std::string stem = py_path_stem(filename);
    const std::string ext = py_path_suffix(filename);
    std::string candidate = filename;
    std::error_code ec;
    for (int n = 2; fs::exists(folder / path_from_utf8(candidate), ec); ++n) {
        candidate = stem + "_" + std::to_string(n) + ext;
    }
    return candidate;
}

Json library_from_scan(const fs::path& base) {
    Json cats = Json::object();
    Json sounds = Json::array();
    for (const Json& it : sfx_scan(base)) {
        const std::string folder = py_str(get_or(it, "folder", kEmptyText));
        if (!folder.empty()) {
            if (!cats.contains(folder)) {
                cats[folder] = Json{{"etiqueta", folder}, {"uso_tipico", ""}, {"cantidad", 0}};
            }
            cats[folder]["cantidad"] = py_int(cats[folder]["cantidad"]).value_or(0) + 1;
        }
        sounds.push_back(Json{{"sonido", get_or(it, "name", kEmptyText)},
                              {"archivo", get_or(it, "file", kEmptyText)},
                              {"carpeta", get_or(it, "folder", kEmptyText)},
                              {"categoria", get_or(it, "category", kEmptyText)},
                              {"uso_tipico", get_or(it, "uso", kEmptyText)}});
    }
    Json out = Json::object();
    out["generado"] = today();
    out["total"] = sounds.size();
    out["categorias"] = std::move(cats);
    out["sonidos"] = std::move(sounds);
    return out;
}

// _read_library: sfx_library.json si es un objeto (con «categorias» y «sonidos» por defecto); si
// no, lo que dice el escaneo.
Json read_library(const fs::path& base) {
    const fs::path file = base / fs::path(kSfxIndexFile);
    std::error_code ec;
    if (fs::exists(file, ec)) {
        if (auto data = read_json_file_py(file); data && data->is_object()) {
            if (!data->contains("categorias")) {
                (*data)["categorias"] = Json::object();
            }
            if (!data->contains("sonidos")) {
                (*data)["sonidos"] = Json::array();
            }
            return *data;
        }
    }
    return library_from_scan(base);
}

Result<void> write_library(const fs::path& base, Json& lib) {
    const Json& sounds = lib["sonidos"];
    if (py_truthy(sounds) && !sounds.is_array() && !sounds.is_object() && !sounds.is_string()) {
        return bad_index("len() de los sonidos");
    }
    lib["total"] = py_truthy(sounds) ? (sounds.is_string() ? detail::py_len(sounds.get_ref<const std::string&>())
                                                           : sounds.size())
                                     : 0;
    return write_json_file(base / fs::path(kSfxIndexFile), lib, 2);
}

Result<std::string> safe_folder_id(std::string_view raw) {
    const std::string text = py_strip(raw);
    const std::string folder = py_path_name(text);
    if (folder.empty() || folder == "." || folder == ".." || folder != text) {
        return invalid("Categoría no válida.");
    }
    return folder;
}

Result<fs::path> ensure_dir(const fs::path& base, const std::string& folder) {
    const auto dir = detail::resolve_inside(base, {folder}, false);
    if (!dir) {
        return invalid("Categoría no válida.");
    }
    std::error_code ec;
    fs::create_directories(*dir, ec);
    if (ec) {
        return fail(ErrorCode::Io, "no se pudo crear la carpeta " + folder + ": " + ec.message());
    }
    return *dir;
}

// _pick_category: la categoría nueva (si hay etiqueta) o la existente; su carpeta y su etiqueta.
// `cats` tiene que ser un objeto.
Result<std::pair<std::string, Json>> pick_category(const fs::path& base, Json& cats, std::string_view category_id,
                                                   std::string_view new_category) {
    const std::string new_label = py_strip(new_category);
    if (!new_label.empty()) {
        const std::string folder = std::format("{:02d}_{}", next_cat_num(base, cats), slug(new_label));
        if (!cats.contains(folder)) {
            cats[folder] = Json{{"etiqueta", new_label}, {"uso_tipico", ""}, {"cantidad", 0}};
        }
        return std::pair{folder, Json(new_label)};
    }
    const auto folder = safe_folder_id(category_id);
    if (!folder) {
        return std::unexpected(folder.error());
    }
    if (!cats.contains(*folder)) {
        cats[*folder] = Json{{"etiqueta", *folder}, {"uso_tipico", ""}, {"cantidad", 0}};
    }
    const Json& meta = cats[*folder];
    if (!meta.is_object()) {
        return bad_index("la categoría " + *folder);
    }
    const Json& label = get_or(meta, "etiqueta", kNull);
    return std::pair{*folder, py_truthy(label) ? label : Json(*folder)};
}

Result<Json> cat_dto(const std::string& folder, const Json& meta) {
    if (py_truthy(meta) && !meta.is_object()) {
        return bad_index("la categoría " + folder);
    }
    const Json& label = meta.is_object() ? get_or(meta, "etiqueta", kNull) : kNull;
    const Json& count = meta.is_object() ? get_or(meta, "cantidad", kNull) : kNull;
    const auto n = py_truthy(count) ? py_int(count) : std::optional<std::int64_t>(0);
    if (!n) {
        return bad_index("int() de la cantidad");
    }
    return Json{{"id", folder}, {"label", py_truthy(label) ? label : Json(folder)}, {"count", *n}};
}

// cats[folder]["cantidad"] = int(cats[folder].get("cantidad") or 0) + delta (sin bajar de 0).
Result<void> add_count(Json& meta, std::int64_t delta) {
    if (!meta.is_object()) {
        return bad_index("la cantidad de una categoría");
    }
    const Json& count = get_or(meta, "cantidad", kNull);
    const auto n = py_truthy(count) ? py_int(count) : std::optional<std::int64_t>(0);
    if (!n) {
        return bad_index("int() de la cantidad");
    }
    meta["cantidad"] = delta < 0 ? std::max<std::int64_t>(0, *n + delta) : *n + delta;
    return {};
}

std::string replace_backslashes(std::string text) {
    std::ranges::replace(text, '\\', '/');
    return text;
}

// _retarget_favorite: la estrella del id viejo pasa al nuevo.
Result<void> retarget_favorite(SettingsFile& settings, const std::string& old_id, const std::string& new_id) {
    if (old_id.empty() || old_id == new_id) {
        return {};
    }
    const Json cur = settings.load();
    const Json& fav_raw = get_or(cur, "favorites", kNull);
    Json fav = py_truthy(fav_raw) ? fav_raw : Json::object();
    Json ids = Json::array();
    const Json& sfx = fav.is_object() ? get_or(fav, "sfx", kNull) : kNull;
    if (const auto seq = py_truthy(sfx) ? py_iter(sfx) : std::optional<std::vector<Json>>(std::vector<Json>{})) {
        for (const Json& x : *seq) {
            const std::string id = py_str(x);
            ids.push_back(id == old_id ? new_id : id);
        }
    }
    fav["sfx"] = std::move(ids);
    if (auto saved = settings.save(Json{{"favorites", std::move(fav)}}); !saved) {
        return std::unexpected(saved.error());
    }
    return {};
}

} // namespace

Result<Json> sfx_create_category(const std::optional<fs::path>& base, std::string_view label) {
    if (!base) {
        return invalid(kNoBase);
    }
    const std::string name = py_strip(label);
    if (name.empty()) {
        return invalid("Ponle nombre a la categoría.");
    }
    const std::string want = py_lower(name);
    Json lib = read_library(*base);
    Json& cats = lib["categorias"];
    if (!cats.is_object()) {
        return bad_index("las categorías");
    }
    for (auto it = cats.begin(); it != cats.end(); ++it) {
        if (py_truthy(*it) && !it->is_object()) {
            return bad_index("la categoría " + it.key());
        }
        const Json& shown = it->is_object() ? get_or(*it, "etiqueta", kNull) : kNull;
        const std::string etiqueta = py_strip(py_str(py_truthy(shown) ? shown : Json(it.key())));
        if (py_lower(etiqueta) == want || py_lower(it.key()) == want) {
            if (auto dir = ensure_dir(*base, it.key()); !dir) {
                return std::unexpected(dir.error());
            }
            return cat_dto(it.key(), *it);
        }
    }
    const auto picked = pick_category(*base, cats, "", name);
    if (!picked) {
        return std::unexpected(picked.error());
    }
    if (auto dir = ensure_dir(*base, picked->first); !dir) {
        return std::unexpected(dir.error());
    }
    if (auto saved = write_library(*base, lib); !saved) {
        return std::unexpected(saved.error());
    }
    return cat_dto(picked->first, lib["categorias"][picked->first]);
}

Result<Json> sfx_add_sound(const std::optional<fs::path>& base, std::string_view filename, std::string_view data,
                           const SfxSoundEdit& edit) {
    if (!base) {
        return invalid(kNoBase);
    }
    const std::string raw_name = py_path_name(filename);
    if (raw_name.empty() || raw_name == "." || raw_name == "..") {
        return invalid("Nombre de archivo no válido.");
    }
    if (!is_sfx_audio(raw_name)) {
        return invalid("Formato de audio no válido.");
    }
    if (data.empty()) {
        return invalid("Archivo vacío.");
    }
    const std::string given = py_strip(edit.name.value_or(""));
    const std::string display = given.empty() ? py_path_stem(raw_name) : given;

    Json lib = read_library(*base);
    Json& cats = lib["categorias"];
    if (!cats.is_object()) {
        return bad_index("las categorías");
    }
    const auto picked = pick_category(*base, cats, edit.category_id, edit.new_category);
    if (!picked) {
        return std::unexpected(picked.error());
    }
    const auto& [folder, cat_label] = *picked;
    const Json& meta = cats[folder];
    if (!meta.is_object()) {
        return bad_index("la categoría " + folder);
    }
    const std::string given_uso = py_strip(edit.uso.value_or(""));
    const Json& meta_uso = get_or(meta, "uso_tipico", kNull);
    const Json uso = !given_uso.empty() ? Json(given_uso) : py_truthy(meta_uso) ? meta_uso : Json("");
    const auto dir = ensure_dir(*base, folder);
    if (!dir) {
        return std::unexpected(dir.error());
    }
    const std::string safe_file = unique_filename(*dir, raw_name);
    {
        std::ofstream out(*dir / path_from_utf8(safe_file), std::ios::binary);
        out.write(data.data(), static_cast<std::streamsize>(data.size()));
        if (!out) {
            return fail(ErrorCode::Io, "no se pudo escribir " + safe_file);
        }
    }
    Json& sounds = lib["sonidos"];
    if (!sounds.is_array()) {
        return bad_index("los sonidos"); // sonidos.append, ya con el archivo escrito, como en Python
    }
    sounds.push_back(Json{{"sonido", display}, {"archivo", safe_file}, {"carpeta", folder}, {"categoria", cat_label},
                          {"uso_tipico", uso}});
    if (auto counted = add_count(cats[folder], 1); !counted) {
        return std::unexpected(counted.error());
    }
    if (auto saved = write_library(*base, lib); !saved) {
        return std::unexpected(saved.error());
    }
    return sfx_entry(folder, safe_file, cat_label, uso, display);
}

Result<Json> sfx_update_sound(const std::optional<fs::path>& base, SettingsFile& settings, std::string_view relpath,
                              const SfxSoundEdit& edit) {
    if (!base) {
        return invalid(kNoBase);
    }
    const std::string rel = py_strip(replace_backslashes(std::string(relpath)));
    Json lib = read_library(*base);
    Json& cats = lib["categorias"];
    Json& sounds = lib["sonidos"];
    if (py_truthy(sounds) && !sounds.is_array()) {
        return bad_index("los sonidos"); // un dict o un texto dan claves o letras sin .get
    }
    const auto text_of = [](const Json& v) { return py_truthy(v) ? py_str(v) : std::string(); };
    std::optional<std::size_t> idx;
    for (std::size_t i = 0; py_truthy(sounds) && i < sounds.size() && !idx; ++i) {
        const Json& s = sounds[i];
        if (!s.is_object()) {
            return bad_index("un sonido");
        }
        const std::string folder = replace_backslashes(text_of(get_or(s, "carpeta", kNull)));
        const std::string file = text_of(get_or(s, "archivo", kNull));
        if ((folder.empty() ? file : folder + "/" + file) == rel) {
            idx = i;
        }
    }
    if (!idx) {
        return invalid("Efecto de sonido no encontrado.");
    }
    Json& row = sounds[*idx];
    const std::string old_folder = replace_backslashes(text_of(get_or(row, "carpeta", kNull)));
    const std::string old_file = text_of(get_or(row, "archivo", kNull));
    const std::string old_id = old_folder.empty() ? old_file : old_folder + "/" + old_file;

    const Json& row_name = get_or(row, "sonido", kNull);
    const Json name = edit.name ? Json(*edit.name) : py_truthy(row_name) ? row_name : Json("");
    if (!name.is_string()) {
        return bad_index("el nombre de un sonido"); // .strip()
    }
    const std::string stripped = py_strip(name.get_ref<const std::string&>());
    const std::string display = stripped.empty() ? py_path_stem(old_file) : stripped;
    const Json& row_uso = get_or(row, "uso_tipico", kNull);
    const Json uso = edit.uso ? Json(py_strip(*edit.uso)) : py_truthy(row_uso) ? row_uso : Json("");

    std::string folder = old_folder;
    const Json& row_cat = get_or(row, "categoria", kNull);
    Json cat_label = py_truthy(row_cat) ? row_cat : Json(old_folder);
    const std::string want_id = py_strip(edit.category_id);
    if (!py_strip(edit.new_category).empty() || (!want_id.empty() && want_id != old_folder)) {
        if (!cats.is_object()) {
            return bad_index("las categorías");
        }
        auto picked = pick_category(*base, cats, edit.category_id, edit.new_category);
        if (!picked) {
            return std::unexpected(picked.error());
        }
        folder = picked->first;
        cat_label = std::move(picked->second);
    }

    std::string dest_file = old_file;
    if (folder != old_folder) {
        const fs::path src = old_folder.empty() ? *base / path_from_utf8(old_file)
                                                : *base / path_from_utf8(old_folder) / path_from_utf8(old_file);
        const auto dir = ensure_dir(*base, folder);
        if (!dir) {
            return std::unexpected(dir.error());
        }
        dest_file = unique_filename(*dir, old_file);
        std::error_code ec;
        if (fs::exists(src, ec)) {
            fs::rename(src, *dir / path_from_utf8(dest_file), ec);
            if (ec) { // otra unidad: shutil.move copia y borra
                ec.clear();
                fs::copy_file(src, *dir / path_from_utf8(dest_file), ec);
                if (ec || !fs::remove(src, ec)) {
                    return fail(ErrorCode::Io, "no se pudo mover " + old_id + ": " + ec.message());
                }
            }
        }
        if (const auto old = cats.find(old_folder); old != cats.end() && !old->is_null()) {
            if (auto counted = add_count(*old, -1); !counted) {
                return std::unexpected(counted.error());
            }
        }
        if (auto counted = add_count(cats[folder], 1); !counted) {
            return std::unexpected(counted.error());
        }
    }

    row["sonido"] = display;
    row["archivo"] = dest_file;
    row["carpeta"] = folder;
    row["categoria"] = cat_label;
    row["uso_tipico"] = uso;
    if (auto saved = write_library(*base, lib); !saved) {
        return std::unexpected(saved.error());
    }
    const std::string new_id = folder.empty() ? dest_file : folder + "/" + dest_file;
    if (auto moved = retarget_favorite(settings, old_id, new_id); !moved) {
        return std::unexpected(moved.error());
    }
    return sfx_entry(folder, dest_file, cat_label, uso, display);
}

} // namespace gc::core
