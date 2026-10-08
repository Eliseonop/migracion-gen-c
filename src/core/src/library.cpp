#include "gc/core/library.hpp"

#include <algorithm>
#include <array>

#include "edit_util.hpp"
#include "gc/core/io.hpp"

namespace gc::core {

using detail::field;
using detail::invalid;
using detail::py_iter;
using detail::py_number;
using detail::py_str;
using detail::py_strip;
using detail::py_truthy;

namespace {

constexpr std::array<std::string_view, 2> kYtHints{"youtube.com", "youtu.be"};

std::string ascii_lower(std::string s) {
    detail::ascii_lower_inplace(s);
    return s;
}

bool is_youtube(const std::string& url) {
    const std::string low = ascii_lower(url);
    return std::ranges::any_of(kYtHints, [&](std::string_view h) { return low.find(h) != std::string::npos; });
}

// La carpeta de la URL del material según su tipo guardado.
const char* disk_kind(std::string_view resource_type) {
    return resource_type == "clip" ? "video" : resource_type == "image" ? "image" : "audio";
}

Json empty_library() { return Json{{"version", 1}, {"items", Json::array()}}; }

// El primer material con ese id (como next() de Python: lo de después no se mira).
Result<std::optional<std::size_t>> find_item(const Json& data, std::string_view item_id) {
    const Json& items = field(data, "items");
    if (!items.is_array()) {
        return invalid("library.json: items debe ser una lista");
    }
    for (std::size_t i = 0; i < items.size(); ++i) {
        if (!items[i].is_object()) {
            return invalid("library.json: cada material debe ser un objeto");
        }
        if (field(items[i], "id") == item_id) {
            return std::optional<std::size_t>{i};
        }
    }
    return std::optional<std::size_t>{};
}

} // namespace

std::string library_resource_type(const Json& item) {
    const Json& rt = field(item, "resource_type");
    if (rt == "audio" || rt == "clip" || rt == "image") {
        return rt.get<std::string>();
    }
    if (item.is_object() && item.contains("index")) {
        return "clip";
    }
    const Json& fn = field(item, "filename");
    const std::string name = ascii_lower(py_truthy(fn) ? py_str(fn) : std::string());
    const bool image_name = std::ranges::any_of(std::array<std::string_view, 6>{".png", ".jpg", ".jpeg", ".webp", ".gif", ".bmp"},
                                                [&](std::string_view ext) { return name.ends_with(ext); });
    if (!field(item, "width").is_null() || image_name) {
        if (!item.contains("voice") && !item.contains("end")) {
            return "image";
        }
    }
    return "audio";
}

std::pair<std::string, std::string> library_origin_source(const Json& item, std::string_view resource_type) {
    const Json& origin = field(item, "origin");
    const Json& source = field(item, "source");
    if (py_truthy(origin) && py_truthy(source)) {
        return {py_str(origin), py_str(source)};
    }
    if (py_truthy(origin)) {
        const std::string o = py_str(origin);
        return {o, o == "youtube" ? "external" : "generated"};
    }
    auto text = [&](const char* key) {
        const Json& v = field(item, key);
        return py_truthy(v) ? py_str(v) : std::string();
    };
    if (resource_type == "audio") {
        if (is_youtube(text("youtube_url")) || py_truthy(field(item, "youtube_id"))) {
            return {"youtube", "external"};
        }
        return {"tts", "generated"};
    }
    const Json& su = field(item, "source_url");
    const std::string url = py_truthy(su) ? py_str(su) : text("youtube_url");
    if (is_youtube(url)) {
        return {"youtube", "external"};
    }
    if (resource_type == "image") {
        return {"upload", "external"};
    }
    return {"compose", "generated"};
}

Result<Json> material_dto(const Json& item, std::string_view scope) {
    if (!item.is_object()) {
        return invalid("cada material debe ser un objeto");
    }
    const std::string rt = library_resource_type(item);
    auto [origin, source] = library_origin_source(item, rt);
    Json out = item;
    out["resource_type"] = rt;
    out["scope"] = scope;
    out["is_saved"] = scope == "library";
    out["origin"] = origin;
    out["source"] = source;
    if (scope == "library") {
        const Json& fn = field(item, "filename");
        if (py_truthy(fn) && !fn.is_string()) {
            return invalid("filename debe ser texto"); // quote() no admite otra cosa
        }
        out["url"] = "/api/library/media/" + std::string(disk_kind(rt)) + "/" +
                     detail::py_quote(py_truthy(fn) ? fn.get<std::string>() : std::string());
    }
    return out;
}

Json projects_using(const ProjectsFile& projects, std::string_view item_id) {
    Json used = Json::array();
    for (const auto& p : projects.projects) {
        if (!p.timeline) {
            continue;
        }
        for (const auto& c : p.timeline->clips) {
            // asset_scope vacío = "project": no cuenta.
            if (c->asset_scope == "library" && c->asset_id == item_id) {
                used.push_back(Json{{"id", p.id}, {"name", p.name}});
                break;
            }
        }
    }
    return used;
}

std::optional<std::filesystem::path> resolve_library_media(const std::filesystem::path& data_dir, std::string_view kind,
                                                           std::string_view filename) {
    if ((kind != "video" && kind != "audio" && kind != "image") || filename.empty()) {
        return std::nullopt;
    }
    std::error_code ec;
    const auto folder = std::filesystem::weakly_canonical(data_dir / "library" / path_from_utf8(kind), ec);
    const auto target = std::filesystem::weakly_canonical(folder / path_from_utf8(filename), ec);
    if (ec || target.parent_path() != folder) {
        return std::nullopt;
    }
    return target;
}

Json LibraryFile::load() const {
    std::error_code ec;
    if (!std::filesystem::exists(file(), ec)) {
        return empty_library();
    }
    auto raw = read_json_file_py(file());
    if (!raw || !raw->is_object()) {
        return empty_library();
    }
    Json data = std::move(*raw);
    if (!data.contains("version")) {
        data["version"] = 1;
    }
    if (!data.contains("items")) {
        data["items"] = Json::array();
    }
    return data;
}

Result<void> LibraryFile::save(const Json& data) const {
    std::error_code ec;
    std::filesystem::create_directories(file().parent_path(), ec);
    return write_json_file(file(), data, 2);
}

Result<Json> LibraryFile::list() const {
    const Json data = load();
    const auto items = py_iter(field(data, "items"));
    if (!items) {
        return invalid("library.json: items debe ser una lista");
    }
    Json clips = Json::array();
    Json audios = Json::array();
    Json images = Json::array();
    for (const Json& it : *items) {
        GC_TRY(dto, material_dto(it, "library"));
        const std::string& rt = dto["resource_type"].get_ref<const std::string&>();
        (rt == "clip" ? clips : rt == "image" ? images : audios).push_back(std::move(dto));
    }
    // Del guardado más reciente al más antiguo: sort estable con reverse=True y la clave
    // `saved_at or ""`. Python compara texto con texto y números con números; si con dos
    // o más materiales hay de las dos familias (o un dict o una lista), falla con TypeError.
    // (Dos listas sí se podrían comparar en Python: aquí también fallan.)
    for (Json* list : {&clips, &audios, &images}) {
        static const Json kEmpty = "";
        auto key = [](const Json& x) -> const Json& {
            const Json& s = field(x, "saved_at");
            return py_truthy(s) ? s : kEmpty;
        };
        bool texts = false;
        bool numbers = false;
        for (const Json& x : *list) {
            const Json& k = key(x);
            texts = texts || k.is_string();
            numbers = numbers || k.is_number() || k.is_boolean();
            if (list->size() > 1 && !k.is_string() && !k.is_number() && !k.is_boolean()) {
                return invalid("saved_at: no se puede ordenar");
            }
        }
        if (list->size() > 1 && texts && numbers) {
            return invalid("saved_at: no se puede ordenar");
        }
        std::vector<Json> v(list->begin(), list->end());
        std::ranges::stable_sort(v, [&](const Json& a, const Json& b) {
            const Json& ka = key(a);
            const Json& kb = key(b);
            if (ka.is_string()) {
                return ka.get_ref<const std::string&>() > kb.get_ref<const std::string&>();
            }
            return *py_number(ka) > *py_number(kb);
        });
        *list = Json(v);
    }
    return Json{{"clips", clips}, {"audios", audios}, {"images", images}};
}

Result<Json> LibraryFile::update_item(std::string_view item_id, const Json& fields) {
    const std::lock_guard lock(mutex_);
    Json data = load();
    GC_TRY(idx, find_item(data, item_id));
    if (!idx) {
        return fail(ErrorCode::NotFound, "Recurso no encontrado.");
    }
    if (!fields.is_object()) {
        return invalid("fields debe ser un objeto");
    }
    Json& item = data["items"][*idx];
    for (const char* k : {"label", "description"}) {
        const Json& v = field(fields, k);
        if (!v.is_null()) {
            item[k] = py_strip(py_str(v));
        }
    }
    if (auto w = save(data); !w) {
        return std::unexpected(w.error());
    }
    return material_dto(item, "library");
}

Result<Json> LibraryFile::set_semantic(std::string_view item_id, const Json& semantic) {
    const std::lock_guard lock(mutex_);
    Json data = load();
    GC_TRY(idx, find_item(data, item_id));
    if (!idx) {
        return fail(ErrorCode::NotFound, "Recurso no encontrado.");
    }
    Json& item = data["items"][*idx];
    item["semantic"] = py_truthy(semantic) ? semantic : Json::object();
    if (auto w = save(data); !w) {
        return std::unexpected(w.error());
    }
    return material_dto(item, "library");
}

Result<Json> LibraryFile::unsave(std::string_view item_id, const ProjectsFile& projects) {
    const Json used = projects_using(projects, item_id);
    if (!used.empty()) {
        // LibraryInUseError: ", ".join(name or id or "") or "otro proyecto". La API añade
        // la lista de proyectos (projects_using).
        std::string names;
        for (std::size_t i = 0; i < used.size(); ++i) {
            const std::string& name = used[i].at("name").get_ref<const std::string&>();
            names += (i > 0 ? ", " : "") + (name.empty() ? used[i].at("id").get<std::string>() : name);
        }
        return fail(ErrorCode::InUse, "En uso en: " + (names.empty() ? std::string("otro proyecto") : names));
    }
    Json item;
    {
        const std::lock_guard lock(mutex_);
        Json data = load();
        GC_TRY(idx, find_item(data, item_id));
        if (!idx) {
            return fail(ErrorCode::NotFound, "Recurso no encontrado.");
        }
        item = data["items"][*idx];
        Json kept = Json::array();
        for (const Json& x : data["items"]) {
            if (!x.is_object()) {
                return invalid("library.json: cada material debe ser un objeto");
            }
            if (field(x, "id") != item_id) {
                kept.push_back(x);
            }
        }
        data["items"] = std::move(kept);
        if (auto w = save(data); !w) {
            return std::unexpected(w.error());
        }
    }
    const Json& rt = field(item, "resource_type");
    const char* kind = rt == "clip" ? "video" : rt == "image" ? "image" : "audio";
    const Json& fn = field(item, "filename");
    if (py_truthy(fn) && !fn.is_string()) {
        return invalid("filename debe ser texto");
    }
    if (const auto path = resolve_library_media(data_dir_, kind, py_truthy(fn) ? fn.get<std::string>() : "")) {
        // Path.unlink(): una carpeta no se borra (falla y Python la deja); si no se puede, se deja.
        std::error_code ec;
        if (!std::filesystem::is_directory(std::filesystem::symlink_status(*path, ec))) {
            std::filesystem::remove(*path, ec);
        }
    }
    return Json{{"deleted", item_id}};
}

} // namespace gc::core
