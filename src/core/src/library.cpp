#include "gc/core/library.hpp"

#include <algorithm>
#include <array>
#include <cmath>

#include "edit_util.hpp"
#include "gc/core/io.hpp"
#include "gc/core/serialize.hpp"

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
    return std::ranges::any_of(kYtHints, [&](std::string_view h) { return low.contains(h); });
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
    auto target = std::filesystem::weakly_canonical(folder / path_from_utf8(filename), ec);
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
            // Con NaN, el orden de Python sale de su timsort con comparaciones incoherentes y no
            // se puede reproducir; aquí los NaN van al final, para que el orden sea válido.
            const double na = *py_number(ka);
            const double nb = *py_number(kb);
            if (std::isnan(na) || std::isnan(nb)) {
                return !std::isnan(na) && std::isnan(nb);
            }
            return na > nb;
        });
        *list = Json(v);
    }
    return Json{{"clips", clips}, {"audios", audios}, {"images", images}};
}

Result<Json> LibraryFile::update_item(std::string_view item_id, const Json& fields) {
    const std::scoped_lock lock(mutex_);
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
    const std::scoped_lock lock(mutex_);
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
        const std::scoped_lock lock(mutex_);
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

namespace {

// segments.is_shared_file: ¿otro clip del proyecto usa ese archivo (un segmento por referencia
// o su vídeo de origen)? El propio, por su index o su id, no cuenta.
bool is_shared_file(const Project& project, std::string_view filename, std::string_view exclude_ident) {
    return std::ranges::any_of(project.clips, [&](const ClipInfo& c) {
        return c.filename == filename && std::to_string(c.index) != exclude_ident &&
               (c.id ? *c.id : std::string("None")) != exclude_ident;
    });
}

} // namespace

Result<Json> LibraryFile::save_from_project(ProjectsStore& projects, std::string_view project_id,
                                            std::string_view resource_type, std::string_view ident, IdSource& ids,
                                            const std::function<std::string()>& now) {
    if (resource_type != "audio" && resource_type != "clip" && resource_type != "image") {
        return fail(ErrorCode::NotFound, "Tipo no válido.");
    }
    auto found = projects.get_project(project_id);
    if (!found) {
        return std::unexpected(found.error());
    }
    if (!*found) {
        return fail(ErrorCode::NotFound, "Proyecto no encontrado.");
    }
    const Project& project = **found;
    const bool is_audio = resource_type == "audio";
    const bool is_clip = resource_type == "clip";
    const std::string disk_kind = is_audio ? "audio" : is_clip ? "video" : "image";
    const std::string asset_kind = is_audio ? "audios" : is_clip ? "clips" : "images";
    // El material, como model_dump(): un audio o una imagen por su id, un clip por su index.
    std::optional<Json> raw;
    if (is_audio) {
        const auto it = std::ranges::find_if(project.audios, [&](const AudioInfo& a) { return a.id == ident; });
        raw = it != project.audios.end() ? std::optional<Json>(to_json(*it)) : std::nullopt;
    } else if (is_clip) {
        const auto it = std::ranges::find_if(project.clips,
                                             [&](const ClipInfo& c) { return std::to_string(c.index) == ident; });
        raw = it != project.clips.end() ? std::optional<Json>(to_json(*it)) : std::nullopt;
    } else {
        const auto it = std::ranges::find_if(project.images, [&](const ImageInfo& i) { return i.id == ident; });
        raw = it != project.images.end() ? std::optional<Json>(to_json(*it)) : std::nullopt;
    }
    if (!raw) {
        return fail(ErrorCode::NotFound, "Material no encontrado.");
    }
    const auto get = [&](const char* key) { return raw->value(key, Json()); };
    const std::string filename = raw->at("filename").get<std::string>();
    const auto src = resolve_project_media(projects.clips_dir(), project, disk_kind, filename);
    std::error_code ec;
    if (!src || !std::filesystem::exists(*src, ec)) {
        return fail(ErrorCode::NotFound, "Archivo no encontrado.");
    }

    const std::string lib_id = "lib_" + ids.hex32().substr(0, 12);
    std::string ext = detail::py_path_suffix(filename);
    if (ext.empty()) {
        ext = is_audio ? ".m4a" : is_clip ? ".mp4" : ".png";
    }
    const Json label = get("label");
    const std::string stem = safe_file_name(py_truthy(label) ? py_str(label) : detail::py_path_stem(filename));
    const std::string dest_name = stem + "_" + lib_id.substr(4) + ext;
    // library_root(): data/library con sus carpetas audio/, video/ e image/.
    const std::filesystem::path root = data_dir_ / "library";
    for (const char* sub : {"audio", "video", "image"}) {
        std::filesystem::create_directories(root / sub, ec);
    }
    const std::filesystem::path dest = root / disk_kind / path_from_utf8(dest_name);
    std::filesystem::copy_file(*src, dest, std::filesystem::copy_options::overwrite_existing, ec); // shutil.copy2
    if (ec) {
        return fail(ErrorCode::Io, "no se pudo copiar " + path_to_utf8(*src) + ": " + ec.message());
    }

    const auto [origin, source] = library_origin_source(*raw, resource_type);
    Json duration = get("duration");
    if (duration.is_null() && !get("start").is_null() && !get("end").is_null()) {
        const auto end = detail::py_float(get("end"));
        const auto start = detail::py_float(get("start"));
        if (start && end) { // en ClipInfo son reales; un material de otra forma no tiene duración
            duration = *end - *start;
        }
    }
    const Json description = get("description");
    const Json youtube_url = get("youtube_url");
    Json entry = Json::object();
    entry["id"] = lib_id;
    entry["resource_type"] = std::string(resource_type);
    entry["scope"] = "library";
    entry["source"] = source;
    entry["origin"] = origin;
    entry["filename"] = dest_name;
    entry["label"] = label;
    entry["description"] = py_truthy(description) ? description : get("text");
    entry["duration"] = duration;
    entry["created_at"] = get("created_at");
    entry["saved_at"] = now();
    entry["saved_from_project_id"] = std::string(project_id);
    for (const char* key : {"voice", "voice2", "blend", "speed", "pause", "text", "engine"}) {
        entry[key] = get(key);
    }
    entry["youtube_url"] = py_truthy(youtube_url) ? youtube_url : origin == "youtube" ? get("source_url") : Json();
    // El segmento por referencia: la copia es el vídeo entero y el tramo viaja en in/out_point.
    for (const char* key : {"youtube_id", "start", "end", "source_url", "reframe", "width", "height", "provider",
                            "external_id", "author", "license_info", "in_point", "out_point", "face_track"}) {
        entry[key] = get(key);
    }

    // Un segmento por referencia comparte archivo con su vídeo de origen y con otros segmentos,
    // que siguen en el proyecto: el archivo no se borra y la timeline se empareja solo por id.
    const bool shared = is_clip && is_shared_file(project, filename, ident);

    // El except de Python: [x for x in data["items"] if x.get("id") != lib_id], se guarda y se
    // borra la copia. Si eso falla (items no se puede recorrer o tiene algo que no es un dict),
    // Python sale con ese error y la copia se queda.
    const auto rollback = [&] {
        const std::scoped_lock lock(mutex_);
        Json data = load();
        Json& items = data["items"];
        Json kept = Json::array();
        if (items.is_array()) {
            for (const Json& x : items) {
                if (!x.is_object()) {
                    return;
                }
                if (x.value("id", Json()) != lib_id) {
                    kept.push_back(x);
                }
            }
        } else if (!(items.is_object() && items.empty()) &&
                   !(items.is_string() && items.get_ref<const std::string&>().empty())) {
            return;
        }
        items = std::move(kept);
        if (!save(data)) {
            return; // si no se puede guardar, Python sale con ese error y la copia se queda
        }
        std::error_code ec_rm;
        std::filesystem::remove(dest, ec_rm);
    };
    Result<void> step = [&] -> Result<void> {
        const std::scoped_lock lock(mutex_);
        Json data = load();
        if (!data["items"].is_array()) {
            return invalid("library.json: items debe ser una lista");
        }
        data["items"].push_back(entry);
        return save(data);
    }();
    if (step) {
        step = projects.retarget_timeline_asset(project_id, asset_kind, Json(std::string(ident)), Json(filename),
                                                Json(lib_id), Json(dest_name), !shared);
    }
    if (step) {
        auto removed = projects.remove_material(project_id, asset_kind, ident);
        if (!removed) {
            step = std::unexpected(removed.error());
        } else if (!*removed) {
            step = fail(ErrorCode::NotFound, "Material no encontrado.");
        }
    }
    if (!step) {
        rollback();
        return std::unexpected(step.error());
    }
    if (!shared) {
        std::filesystem::remove(*src, ec); // como Python: si no se puede borrar, se queda
    }
    return material_dto(entry, "library");
}

} // namespace gc::core
