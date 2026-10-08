#include "gc/core/projects_store.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <format>
#include <span>
#include <system_error>
#include <utility>
#include <vector>

#include "disk_scan.hpp"
#include "gc/core/io.hpp"
#include "gc/core/serialize.hpp"
#include "json_reader.hpp"
#include "py.hpp"

namespace gc::core {

namespace fs = std::filesystem;
using detail::py_equal;
using detail::py_lower;
using detail::py_split;
using detail::py_str;
using detail::py_strip;
using detail::py_truthy;

namespace {

const Json kNull;
const Json kEmptyArray = Json::array();

// Un projects.json con otra forma: Python lanza KeyError, TypeError o AttributeError.
std::unexpected<Error> shape_error(std::string what) {
    return fail(ErrorCode::Validation, "projects.json: " + std::move(what));
}

std::unexpected<Error> empty_name() { return fail(ErrorCode::InvalidArgument, "El nombre no puede estar vacío."); }

// data[key] de Python: error si no es un dict o no tiene la clave.
Result<Json*> at_key(Json& data, const char* key) {
    if (!data.is_object()) {
        return shape_error(std::string("se esperaba un objeto con «") + key + "»");
    }
    const auto it = data.find(key);
    if (it == data.end()) {
        return shape_error(std::string("falta «") + key + "»");
    }
    return &*it;
}

// d.get(key, default) de Python: error si no es un dict. Devuelve la dirección de `fallback`
// si falta la clave, así que tiene que vivir más que el resultado (un temporal no compila).
Result<const Json*> get_or(const Json& d, const char* key, const Json& fallback) {
    if (!d.is_object()) {
        return shape_error(std::string("se esperaba un objeto al leer «") + key + "»");
    }
    const auto it = d.find(key);
    return it == d.end() ? &fallback : &*it;
}
Result<const Json*> get_or(const Json& d, const char* key, Json&& fallback) = delete;

// Lo que recorre `for x in seq` cuando cada x se usa como dict: los elementos de una lista.
// Un dict o un texto vacíos no dan nada; con algo dentro dan textos, y x["…"] o x.get fallan.
Result<std::span<Json>> dict_items(Json& seq, const char* what) {
    if (seq.is_array()) {
        return std::span<Json>(seq.get_ref<Json::array_t&>());
    }
    if ((seq.is_object() && seq.empty()) || (seq.is_string() && seq.get_ref<const std::string&>().empty())) {
        return std::span<Json>();
    }
    return shape_error(std::string(what) + " no es una lista de objetos");
}

// x["id"] == pid de Python: error si x no es un dict con «id».
Result<bool> id_is(const Json& x, std::string_view pid) {
    if (!x.is_object()) {
        return shape_error("un elemento no es un objeto");
    }
    const auto it = x.find("id");
    if (it == x.end()) {
        return shape_error("un elemento no tiene «id»");
    }
    return py_equal(*it, Json(std::string(pid)));
}

// Los proyectos de data["projects"] para recorrerlos.
Result<std::span<Json>> projects_of(Json& data) {
    auto projects = at_key(data, "projects");
    if (!projects) {
        return std::unexpected(projects.error());
    }
    return dict_items(**projects, "«projects»");
}

// El primer proyecto con ese id, o nullptr.
Result<Json*> find_project(Json& data, std::string_view pid) {
    auto projects = projects_of(data);
    if (!projects) {
        return std::unexpected(projects.error());
    }
    for (Json& p : *projects) {
        auto same = id_is(p, pid);
        if (!same) {
            return std::unexpected(same.error());
        }
        if (*same) {
            return &p;
        }
    }
    return nullptr;
}

// any(g["id"] == gid for g in data.get("groups", [])).
Result<bool> group_exists(const Json& data, std::string_view gid) {
    auto groups = get_or(data, "groups", kEmptyArray);
    if (!groups) {
        return std::unexpected(groups.error());
    }
    Json copy = **groups; // dict_items necesita poder recorrerlo; no se modifica
    auto items = dict_items(copy, "«groups»");
    if (!items) {
        return std::unexpected(items.error());
    }
    for (const Json& g : *items) {
        auto same = id_is(g, gid);
        if (!same) {
            return std::unexpected(same.error());
        }
        if (*same) {
            return true;
        }
    }
    return false;
}

// _match de projects.py: str(item.get("index", item.get("id"))) == str(ident).
Result<bool> material_matches(const Json& item, std::string_view ident) {
    if (!item.is_object()) {
        return shape_error("un material no es un objeto");
    }
    const auto index = item.find("index");
    const Json& key = index != item.end() ? *index : item.value("id", Json());
    return py_str(key) == ident;
}

// shutil.copytree(src, dst, dirs_exist_ok=True), sin lo que se llama «exports» (en cualquier
// nivel y sin distinguir mayúsculas, como fnmatch en Windows) si `skip_exports`.
Result<void> copy_tree(const fs::path& src, const fs::path& dst, bool skip_exports) {
    std::error_code ec;
    if (!fs::is_directory(src, ec)) {
        return fail(ErrorCode::Io, "no es una carpeta: " + path_to_utf8(src));
    }
    fs::create_directories(dst, ec);
    if (ec) {
        return fail(ErrorCode::Io, "no se pudo crear " + path_to_utf8(dst) + ": " + ec.message());
    }
    fs::recursive_directory_iterator it(src, ec);
    for (const fs::recursive_directory_iterator end; !ec && it != end; it.increment(ec)) {
        if (skip_exports && py_lower(path_to_utf8(it->path().filename())) == "exports") {
            it.disable_recursion_pending();
            continue;
        }
        const fs::path target = dst / fs::relative(it->path(), src, ec);
        std::error_code ec_item;
        if (it->is_directory(ec_item)) {
            fs::create_directories(target, ec_item);
        } else {
            fs::copy_file(it->path(), target, fs::copy_options::overwrite_existing, ec_item);
        }
        if (ec_item) {
            return fail(ErrorCode::Io, "no se pudo copiar " + path_to_utf8(it->path()) + ": " + ec_item.message());
        }
    }
    if (ec) {
        return fail(ErrorCode::Io, "no se pudo recorrer " + path_to_utf8(src) + ": " + ec.message());
    }
    return {};
}

std::string replace_all(std::string s, std::string_view from, std::string_view to) {
    for (auto pos = s.find(from); pos != std::string::npos; pos = s.find(from, pos + to.size())) {
        s.replace(pos, from.size(), to);
    }
    return s;
}

} // namespace

fs::path project_default_base(const fs::path& clips_dir, std::string_view project_id) {
    return clips_dir / path_from_utf8(project_id);
}

fs::path project_base(const fs::path& clips_dir, const Project& project) {
    if (project.folder && !project.folder->empty()) {
        return path_from_utf8(*project.folder);
    }
    return project_default_base(clips_dir, project.id);
}

Result<void> ensure_project_dirs(const fs::path& base) {
    for (const char* sub : {"video", "audio", "image"}) {
        std::error_code ec;
        fs::create_directories(base / sub, ec);
        if (ec) {
            return fail(ErrorCode::Io, "no se pudo crear " + path_to_utf8(base / sub) + ": " + ec.message());
        }
    }
    return {};
}

std::optional<fs::path> resolve_project_media(const fs::path& clips_dir, const Project& project,
                                              std::string_view kind, std::string_view filename) {
    if (kind != "video" && kind != "audio" && kind != "image") {
        return std::nullopt;
    }
    const fs::path base = project_base(clips_dir, project);
    auto target = detail::resolve_inside(base, std::string(kind) + "/" + std::string(filename));
    if (!target) {
        return std::nullopt;
    }
    // `base not in target.parents`: la propia carpeta tampoco vale.
    std::error_code ec;
    if (*target == fs::weakly_canonical(fs::absolute(base, ec), ec)) {
        return std::nullopt;
    }
    return target;
}

std::string safe_file_name(std::string_view title, std::size_t maxlen) {
    // re.sub(r'[<>:"/\\|?*\n\r\t]+', "", title).strip(): son ASCII, se quitan byte a byte.
    constexpr std::string_view kBad = "<>:\"/\\|?*\n\r\t";
    std::string kept;
    for (const char c : title) {
        if (!kBad.contains(c)) {
            kept += c;
        }
    }
    // re.sub(r"\s+", " ", s) sobre un texto ya sin espacios en los extremos = " ".join(s.split()).
    std::string joined;
    for (const auto& word : py_split(py_strip(kept))) {
        joined += (joined.empty() ? "" : " ") + word;
    }
    const std::string out = py_strip(detail::py_slice(joined, maxlen));
    return out.empty() ? "video" : out;
}

std::string utc_now_iso() {
    const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
    return std::format("{:%Y-%m-%dT%H:%M:%S}+00:00", now);
}

ProjectsStore::ProjectsStore(fs::path file, fs::path clips_dir, Clock now)
    : file_(std::move(file)), clips_dir_(std::move(clips_dir)), now_(std::move(now)) {}

Json ProjectsStore::load() const {
    std::error_code ec;
    if (!fs::exists(file_, ec)) {
        return Json{{"projects", Json::array()}};
    }
    auto data = read_json_file_py(file_);
    return data ? std::move(*data) : Json{{"projects", Json::array()}};
}

Result<void> ProjectsStore::save(const Json& data) const {
    std::error_code ec;
    fs::create_directory(file_.parent_path(), ec); // _FILE.parent.mkdir(exist_ok=True)
    return write_json_file(file_, data, 2);
}

Result<std::vector<Project>> ProjectsStore::list_projects() const {
    Json data = load();
    auto projects = projects_of(data);
    if (!projects) {
        return std::unexpected(projects.error());
    }
    std::vector<Project> out;
    out.reserve(projects->size());
    for (const Json& p : *projects) {
        auto project = project_from_json(p);
        if (!project) {
            return std::unexpected(project.error());
        }
        out.push_back(std::move(*project));
    }
    return out;
}

Result<std::optional<Project>> ProjectsStore::get_project(std::string_view pid) const {
    Json data = load();
    auto p = find_project(data, pid);
    if (!p) {
        return std::unexpected(p.error());
    }
    if (*p == nullptr) {
        return std::optional<Project>();
    }
    auto project = project_from_json(**p);
    if (!project) {
        return std::unexpected(project.error());
    }
    return std::optional<Project>(std::move(*project));
}

Result<Project> ProjectsStore::create_project(std::string_view name, std::optional<std::string_view> group_id,
                                              IdSource& ids) {
    Project proj;
    proj.id = ids.hex32().substr(0, 8);
    proj.name = py_strip(name);
    if (proj.name.empty()) {
        proj.name = "Sin título";
    }
    proj.created_at = now_();
    const std::scoped_lock lock(mutex_);
    Json data = load();
    // Una carpeta que ya no existe no es error: el proyecto queda sin carpeta.
    if (group_id && !group_id->empty()) {
        auto exists = group_exists(data, *group_id);
        if (!exists) {
            return std::unexpected(exists.error());
        }
        if (*exists) {
            proj.group_id = std::string(*group_id);
        }
    }
    auto projects = at_key(data, "projects");
    if (!projects) {
        return std::unexpected(projects.error());
    }
    if (!(*projects)->is_array()) {
        return shape_error("«projects» no es una lista");
    }
    (*projects)->insert((*projects)->begin(), to_json(proj));
    if (auto saved = save(data); !saved) {
        return std::unexpected(saved.error());
    }
    return proj;
}

Result<std::vector<ProjectGroup>> ProjectsStore::list_groups() const {
    const Json data = load();
    auto groups = get_or(data, "groups", kEmptyArray);
    if (!groups) {
        return std::unexpected(groups.error());
    }
    Json copy = **groups;
    auto items = dict_items(copy, "«groups»");
    if (!items) {
        return std::unexpected(items.error());
    }
    std::vector<ProjectGroup> out;
    for (const Json& g : *items) {
        auto group = project_group_from_json(g);
        if (!group) {
            return std::unexpected(group.error());
        }
        out.push_back(std::move(*group));
    }
    return out;
}

Result<ProjectGroup> ProjectsStore::create_group(std::string_view name, IdSource& ids) {
    ProjectGroup group;
    group.id = ids.hex32().substr(0, 8); // el id se pide antes de mirar el nombre, como en Python
    group.name = py_strip(name);
    if (group.name.empty()) {
        return empty_name();
    }
    group.created_at = now_();
    const std::scoped_lock lock(mutex_);
    Json data = load();
    if (!data.is_object()) {
        return shape_error("se esperaba un objeto");
    }
    if (!data.contains("groups")) {
        data["groups"] = Json::array();
    }
    Json& groups = data["groups"];
    if (!groups.is_array()) {
        return shape_error("«groups» no es una lista");
    }
    groups.push_back(to_json(group));
    if (auto saved = save(data); !saved) {
        return std::unexpected(saved.error());
    }
    return group;
}

Result<std::optional<ProjectGroup>> ProjectsStore::rename_group(std::string_view gid, std::string_view name) {
    const std::string new_name = py_strip(name);
    if (new_name.empty()) {
        return empty_name();
    }
    const std::scoped_lock lock(mutex_);
    Json data = load();
    if (!data.is_object()) {
        return shape_error("se esperaba un objeto");
    }
    if (!data.contains("groups")) {
        return std::optional<ProjectGroup>();
    }
    auto items = dict_items(data["groups"], "«groups»");
    if (!items) {
        return std::unexpected(items.error());
    }
    for (Json& g : *items) {
        auto same = id_is(g, gid);
        if (!same) {
            return std::unexpected(same.error());
        }
        if (*same) {
            g["name"] = new_name;
            if (auto saved = save(data); !saved) {
                return std::unexpected(saved.error());
            }
            auto group = project_group_from_json(g);
            if (!group) {
                return std::unexpected(group.error());
            }
            return std::optional<ProjectGroup>(std::move(*group));
        }
    }
    return std::optional<ProjectGroup>();
}

Result<bool> ProjectsStore::delete_group(std::string_view gid) {
    const std::scoped_lock lock(mutex_);
    Json data = load();
    if (!data.is_object()) {
        return shape_error("se esperaba un objeto");
    }
    if (!data.contains("groups")) {
        return false;
    }
    auto groups = dict_items(data["groups"], "«groups»");
    if (!groups) {
        return std::unexpected(groups.error());
    }
    Json kept = Json::array();
    for (const Json& g : *groups) {
        auto same = id_is(g, gid);
        if (!same) {
            return std::unexpected(same.error());
        }
        if (!*same) {
            kept.push_back(g);
        }
    }
    if (kept.size() == groups->size()) {
        return false;
    }
    data["groups"] = std::move(kept);
    auto projects = projects_of(data);
    if (!projects) {
        return std::unexpected(projects.error());
    }
    for (Json& p : *projects) {
        auto group = get_or(p, "group_id", kNull);
        if (!group) {
            return std::unexpected(group.error());
        }
        if (py_equal(**group, Json(std::string(gid)))) {
            p["group_id"] = nullptr;
        }
    }
    if (auto saved = save(data); !saved) {
        return std::unexpected(saved.error());
    }
    return true;
}

Result<std::optional<Project>> ProjectsStore::move_project(std::string_view pid,
                                                           std::optional<std::string_view> group_id) {
    const std::scoped_lock lock(mutex_);
    Json data = load();
    const bool to_group = group_id && !group_id->empty();
    if (to_group) {
        auto exists = group_exists(data, *group_id);
        if (!exists) {
            return std::unexpected(exists.error());
        }
        if (!*exists) {
            return fail(ErrorCode::InvalidArgument, "La carpeta no existe.");
        }
    }
    auto p = find_project(data, pid);
    if (!p) {
        return std::unexpected(p.error());
    }
    if (*p == nullptr) {
        return std::optional<Project>();
    }
    (**p)["group_id"] = to_group ? Json(std::string(*group_id)) : Json();
    if (auto saved = save(data); !saved) {
        return std::unexpected(saved.error());
    }
    auto project = project_from_json(**p);
    if (!project) {
        return std::unexpected(project.error());
    }
    return std::optional<Project>(std::move(*project));
}

Result<std::optional<Project>> ProjectsStore::rename_project(std::string_view pid, std::string_view name) {
    const std::string new_name = py_strip(name);
    if (new_name.empty()) {
        return empty_name();
    }
    const std::scoped_lock lock(mutex_);
    Json data = load();
    auto p = find_project(data, pid);
    if (!p) {
        return std::unexpected(p.error());
    }
    if (*p == nullptr) {
        return std::optional<Project>();
    }
    (**p)["name"] = new_name;
    if (auto saved = save(data); !saved) {
        return std::unexpected(saved.error());
    }
    auto project = project_from_json(**p);
    if (!project) {
        return std::unexpected(project.error());
    }
    return std::optional<Project>(std::move(*project));
}

Result<std::optional<Project>> ProjectsStore::duplicate_project(std::string_view pid, IdSource& ids) {
    auto src = get_project(pid);
    if (!src || !*src) {
        return src;
    }
    const std::string new_id = ids.hex32().substr(0, 8);
    // Los archivos van a la carpeta por defecto del nuevo, aunque el original tenga carpeta
    // propia, así que la copia es independiente. Los renders (exports/) no se copian.
    const fs::path dest_base = project_default_base(clips_dir_, new_id);
    const fs::path src_base = project_base(clips_dir_, **src);
    std::error_code ec;
    if (fs::exists(src_base, ec)) {
        if (auto copied = copy_tree(src_base, dest_base, true); !copied) {
            return std::unexpected(copied.error());
        }
    }
    // Motion Studio guarda siempre bajo la carpeta por defecto, aunque haya carpeta propia.
    const fs::path motion = project_default_base(clips_dir_, pid) / "motion";
    if (fs::exists(motion, ec) && !fs::exists(dest_base / "motion", ec)) {
        if (auto copied = copy_tree(motion, dest_base / "motion", false); !copied) {
            return std::unexpected(copied.error());
        }
    }
    if (auto made = ensure_project_dirs(dest_base); !made) {
        return std::unexpected(made.error());
    }

    const std::scoped_lock lock(mutex_);
    Json data = load();
    auto raw = find_project(data, pid);
    if (!raw) {
        return std::unexpected(raw.error());
    }
    if (*raw == nullptr) {
        return std::optional<Project>();
    }
    // Como Python: las URLs se cambian en el texto JSON del proyecto (claves incluidas), con
    // json.dumps y json.loads, que conservan los NaN y los infinitos (el dump de nlohmann no).
    auto dumped = py_json_dumps(**raw);
    if (!dumped) {
        return std::unexpected(dumped.error());
    }
    std::string text = std::move(*dumped);
    for (const std::string_view prefix : {"/api/media/", "/api/projects/"}) {
        text = replace_all(std::move(text), std::format("{}{}/", prefix, pid), std::format("{}{}/", prefix, new_id));
    }
    auto parsed = py_json_loads(text);
    if (!parsed) {
        return std::unexpected(parsed.error());
    }
    Json copy = std::move(*parsed);
    copy["id"] = new_id;
    copy["name"] = (*src)->name + " (copia)";
    copy["created_at"] = now_();
    copy["folder"] = nullptr;
    auto& list = data["projects"].get_ref<Json::array_t&>();
    const auto at = static_cast<std::ptrdiff_t>(*raw - list.data());
    list.insert(list.begin() + at, copy);
    if (auto saved = save(data); !saved) {
        return std::unexpected(saved.error());
    }
    auto project = project_from_json(copy);
    if (!project) {
        return std::unexpected(project.error());
    }
    return std::optional<Project>(std::move(*project));
}

Result<std::optional<Project>> ProjectsStore::set_folder(std::string_view pid, const Json& path) {
    const std::scoped_lock lock(mutex_);
    Json data = load();
    auto p = find_project(data, pid);
    if (!p) {
        return std::unexpected(p.error());
    }
    if (*p == nullptr) {
        return std::optional<Project>();
    }
    (**p)["folder"] = path;
    if (auto saved = save(data); !saved) {
        return std::unexpected(saved.error());
    }
    auto project = project_from_json_as_is(**p);
    if (!project) {
        return std::unexpected(project.error());
    }
    return std::optional<Project>(std::move(*project));
}

Result<bool> ProjectsStore::delete_project(std::string_view pid) {
    const std::scoped_lock lock(mutex_);
    Json data = load();
    auto projects = projects_of(data);
    if (!projects) {
        return std::unexpected(projects.error());
    }
    const std::size_t before = projects->size();
    Json kept = Json::array();
    for (const Json& p : *projects) {
        auto same = id_is(p, pid);
        if (!same) {
            return std::unexpected(same.error());
        }
        if (!*same) {
            kept.push_back(p);
        }
    }
    const std::size_t after = kept.size();
    data["projects"] = std::move(kept);
    if (auto saved = save(data); !saved) {
        return std::unexpected(saved.error());
    }
    return after < before;
}

Result<void> ProjectsStore::add_clips(std::string_view pid, const std::vector<ClipInfo>& clips, IdSource& ids) {
    const std::string stamp = now_();
    const std::scoped_lock lock(mutex_);
    Json data = load();
    auto p = find_project(data, pid);
    if (!p) {
        return std::unexpected(p.error());
    }
    if (*p != nullptr) {
        Json& project = **p;
        if (!project.contains("clips")) {
            project["clips"] = Json::array();
        }
        Json& list = project["clips"];
        for (const ClipInfo& c : clips) {
            Json d = to_json(c);
            d["created_at"] = stamp;
            // Upsert por índice: regenerar un tramo actualiza su clip en vez de duplicarlo
            // (mismo index → mismo archivo en disco).
            auto items = dict_items(list, "«clips»");
            if (!items) {
                return std::unexpected(items.error());
            }
            std::optional<std::size_t> existing;
            for (std::size_t i = 0; i < items->size(); ++i) {
                auto index = get_or((*items)[i], "index", kNull);
                if (!index) {
                    return std::unexpected(index.error());
                }
                if (py_equal(**index, d["index"])) {
                    existing = i;
                    break;
                }
            }
            if (existing) {
                const Json& prev = list[*existing];
                const Json& prev_id = prev.value("id", Json());
                if (py_truthy(prev_id) && !py_truthy(d.value("id", Json()))) {
                    d["id"] = prev_id;
                } else if (!py_truthy(d.value("id", Json()))) {
                    const Json& index = d.value("index", Json());
                    d["id"] = py_truthy(index) ? py_str(index) : ids.hex32().substr(0, 12);
                }
                list[*existing] = std::move(d);
            } else {
                if (!py_truthy(d.value("id", Json()))) {
                    d["id"] = ids.hex32().substr(0, 12); // antes del append, que falla si no es lista
                }
                if (!list.is_array()) {
                    return shape_error("«clips» no es una lista");
                }
                list.push_back(std::move(d));
            }
        }
    }
    return save(data);
}

Result<void> ProjectsStore::append_to(std::string_view pid, const char* key, Json value) {
    const std::scoped_lock lock(mutex_);
    Json data = load();
    auto p = find_project(data, pid);
    if (!p) {
        return std::unexpected(p.error());
    }
    if (*p != nullptr) {
        Json& project = **p;
        if (!project.contains(key)) {
            project[key] = Json::array();
        }
        if (!project[key].is_array()) {
            return shape_error(std::string("«") + key + "» no es una lista");
        }
        project[key].push_back(std::move(value));
    }
    return save(data);
}

Result<void> ProjectsStore::add_transcript(std::string_view pid, const Transcript& transcript) {
    return append_to(pid, "transcripts", to_json(transcript));
}

Result<void> ProjectsStore::add_audio(std::string_view pid, const AudioInfo& audio) {
    return append_to(pid, "audios", to_json(audio));
}

Result<void> ProjectsStore::add_image(std::string_view pid, const ImageInfo& image) {
    return append_to(pid, "images", to_json(image));
}

Result<std::optional<Json>> ProjectsStore::update_material(std::string_view pid, std::string_view kind,
                                                          std::string_view ident, const Json& fields) {
    const std::scoped_lock lock(mutex_);
    Json data = load();
    auto projects = projects_of(data);
    if (!projects) {
        return std::unexpected(projects.error());
    }
    const std::string key(kind);
    for (Json& p : *projects) {
        auto same = id_is(p, pid);
        if (!same) {
            return std::unexpected(same.error());
        }
        if (!*same || !p.contains(key)) { // p.get(kind, []): sin la lista no hay nada que tocar
            continue;
        }
        auto items = dict_items(p[key], "la lista de materiales");
        if (!items) {
            return std::unexpected(items.error());
        }
        for (Json& item : *items) {
            auto match = material_matches(item, ident);
            if (!match) {
                return std::unexpected(match.error());
            }
            if (!*match) {
                continue;
            }
            if (!fields.is_object()) {
                return shape_error("los campos no son un objeto");
            }
            for (auto it = fields.begin(); it != fields.end(); ++it) {
                if (!it->is_null()) {
                    item[it.key()] = *it;
                }
            }
            if (auto saved = save(data); !saved) {
                return std::unexpected(saved.error());
            }
            return std::optional<Json>(item);
        }
    }
    return std::optional<Json>();
}

Result<std::optional<Json>> ProjectsStore::remove_material(std::string_view pid, std::string_view kind,
                                                          std::string_view ident) {
    const std::scoped_lock lock(mutex_);
    Json data = load();
    auto projects = projects_of(data);
    if (!projects) {
        return std::unexpected(projects.error());
    }
    const std::string key(kind);
    for (Json& p : *projects) {
        auto same = id_is(p, pid);
        if (!same) {
            return std::unexpected(same.error());
        }
        if (!*same || !p.contains(key)) {
            continue;
        }
        auto items = dict_items(p[key], "la lista de materiales");
        if (!items) {
            return std::unexpected(items.error());
        }
        for (std::size_t i = 0; i < items->size(); ++i) {
            auto match = material_matches((*items)[i], ident);
            if (!match) {
                return std::unexpected(match.error());
            }
            if (*match) {
                Json removed = std::move((*items)[i]);
                p[key].erase(i);
                if (auto saved = save(data); !saved) {
                    return std::unexpected(saved.error());
                }
                return std::optional<Json>(std::move(removed));
            }
        }
    }
    return std::optional<Json>();
}

Result<std::optional<Project>> ProjectsStore::apply_manifest(std::string_view pid, const Json& manifest) {
    // by_file: los materiales del manifest por su `file` (una clave igual sustituye al anterior).
    std::vector<std::pair<Json, const Json*>> by_file;
    auto materials = get_or(manifest, "materials", kEmptyArray);
    if (!materials) {
        return std::unexpected(materials.error());
    }
    Json list = py_truthy(**materials) ? **materials : Json::array();
    auto entries = dict_items(list, "«materials»");
    if (!entries) {
        return std::unexpected(entries.error());
    }
    const auto lookup = [&](const Json& key) -> Result<const Json*> {
        if (key.is_array() || key.is_object()) {
            return shape_error("un nombre de archivo no es texto"); // unhashable type
        }
        for (const auto& [k, m] : by_file) {
            if (py_equal(k, key)) {
                return m;
            }
        }
        return nullptr;
    };
    for (const Json& m : *entries) {
        auto f = get_or(m, "file", kNull);
        if (!f) {
            return std::unexpected(f.error());
        }
        if (!py_truthy(**f)) {
            continue;
        }
        auto found = lookup(**f);
        if (!found) {
            return std::unexpected(found.error());
        }
        if (*found != nullptr) {
            std::ranges::find_if(by_file, [&](const auto& e) { return py_equal(e.first, **f); })->second = &m;
        } else {
            by_file.emplace_back(**f, &m);
        }
    }
    auto project = get_or(manifest, "project", kNull);
    if (!project) {
        return std::unexpected(project.error());
    }
    std::string new_name;
    if (py_truthy(**project)) {
        auto name = get_or(**project, "name", kNull);
        if (!name) {
            return std::unexpected(name.error());
        }
        if (py_truthy(**name)) {
            if (!(*name)->is_string()) {
                return shape_error("el nombre del proyecto no es texto");
            }
            new_name = py_strip((*name)->get_ref<const std::string&>());
        }
    }

    const std::scoped_lock lock(mutex_);
    Json data = load();
    auto projects = projects_of(data);
    if (!projects) {
        return std::unexpected(projects.error());
    }
    // (lista, campos editables) en el orden de Python.
    const std::array<std::pair<const char*, std::vector<const char*>>, 3> kEditable{{
        {"clips", {"label", "description", "start", "end", "source_url"}},
        {"audios", {"label", "description", "voice"}},
        {"images", {"label", "description"}},
    }};
    for (Json& p : *projects) {
        auto same = id_is(p, pid);
        if (!same) {
            return std::unexpected(same.error());
        }
        if (!*same) {
            continue;
        }
        if (!new_name.empty()) {
            p["name"] = new_name;
        }
        for (const auto& [list_key, editable] : kEditable) {
            if (!p.contains(list_key)) {
                continue;
            }
            auto items = dict_items(p[list_key], "la lista de materiales");
            if (!items) {
                return std::unexpected(items.error());
            }
            for (Json& item : *items) {
                auto filename = get_or(item, "filename", kNull);
                if (!filename) {
                    return std::unexpected(filename.error());
                }
                auto m = lookup(**filename);
                if (!m) {
                    return std::unexpected(m.error());
                }
                if (*m == nullptr) {
                    continue;
                }
                for (const char* k : editable) {
                    if (const auto it = (*m)->find(k); it != (*m)->end()) {
                        item[k] = *it;
                    }
                }
            }
        }
        if (auto saved = save(data); !saved) {
            return std::unexpected(saved.error());
        }
        auto out = project_from_json_as_is(p);
        if (!out) {
            return std::unexpected(out.error());
        }
        return std::optional<Project>(std::move(*out));
    }
    return std::optional<Project>();
}

Result<std::optional<Project>> ProjectsStore::save_timeline(std::string_view pid, const Json& timeline) {
    const std::scoped_lock lock(mutex_);
    Json data = load();
    auto p = find_project(data, pid);
    if (!p) {
        return std::unexpected(p.error());
    }
    if (*p == nullptr) {
        return std::optional<Project>();
    }
    (**p)["timeline"] = timeline;
    if (auto saved = save(data); !saved) {
        return std::unexpected(saved.error());
    }
    auto project = project_from_json_as_is(**p);
    if (!project) {
        return std::unexpected(project.error());
    }
    return std::optional<Project>(std::move(*project));
}

Result<std::optional<Project>> ProjectsStore::save_motion_composition(std::string_view pid, const Json& comp) {
    auto cid = get_or(comp, "id", kNull);
    if (!cid) {
        return std::unexpected(cid.error());
    }
    if (!py_truthy(**cid)) {
        return std::optional<Project>();
    }
    const std::scoped_lock lock(mutex_);
    Json data = load();
    auto p = find_project(data, pid);
    if (!p) {
        return std::unexpected(p.error());
    }
    if (*p == nullptr) {
        return std::optional<Project>();
    }
    Json& project = **p;
    if (!project.contains("motion_compositions")) {
        project["motion_compositions"] = Json::array();
    }
    Json& comps = project["motion_compositions"];
    auto items = dict_items(comps, "«motion_compositions»");
    if (!items) {
        return std::unexpected(items.error());
    }
    bool replaced = false;
    for (Json& c : *items) {
        auto id = get_or(c, "id", kNull);
        if (!id) {
            return std::unexpected(id.error());
        }
        if (py_equal(**id, **cid)) {
            c = comp;
            replaced = true;
            break;
        }
    }
    if (!replaced) {
        if (!comps.is_array()) {
            return shape_error("«motion_compositions» no es una lista");
        }
        comps.push_back(comp);
    }
    if (auto saved = save(data); !saved) {
        return std::unexpected(saved.error());
    }
    auto out = project_from_json(project);
    if (!out) {
        return std::unexpected(out.error());
    }
    return std::optional<Project>(std::move(*out));
}

Result<bool> ProjectsStore::delete_motion_composition(std::string_view pid, std::string_view cid) {
    const std::scoped_lock lock(mutex_);
    Json data = load();
    auto projects = projects_of(data);
    if (!projects) {
        return std::unexpected(projects.error());
    }
    const Json wanted = std::string(cid);
    for (Json& p : *projects) {
        auto same = id_is(p, pid);
        if (!same) {
            return std::unexpected(same.error());
        }
        if (!*same) {
            continue;
        }
        const Json& raw = p.value("motion_compositions", Json());
        Json comps = py_truthy(raw) ? raw : Json::array();
        auto items = dict_items(comps, "«motion_compositions»");
        if (!items) {
            return std::unexpected(items.error());
        }
        Json kept = Json::array();
        for (const Json& c : *items) {
            auto id = get_or(c, "id", kNull);
            if (!id) {
                return std::unexpected(id.error());
            }
            if (!py_equal(**id, wanted)) {
                kept.push_back(c);
            }
        }
        if (kept.size() != items->size()) {
            p["motion_compositions"] = std::move(kept);
            if (auto saved = save(data); !saved) {
                return std::unexpected(saved.error());
            }
            return true;
        }
    }
    return false;
}

Result<bool> ProjectsStore::set_key(std::string_view pid, const char* key, const Json& doc) {
    const std::scoped_lock lock(mutex_);
    Json data = load();
    auto p = find_project(data, pid);
    if (!p) {
        return std::unexpected(p.error());
    }
    if (*p == nullptr) {
        return false;
    }
    (**p)[key] = doc;
    if (auto saved = save(data); !saved) {
        return std::unexpected(saved.error());
    }
    return true;
}

Result<bool> ProjectsStore::save_scene_directions(std::string_view pid, const Json& doc) {
    return set_key(pid, "scene_directions", doc);
}

Result<bool> ProjectsStore::save_visual_blueprint(std::string_view pid, const Json& doc) {
    return set_key(pid, "visual_blueprint", doc);
}

Result<void> ProjectsStore::retarget_timeline_asset(std::string_view pid, std::string_view asset_kind,
                                                    const Json& old_id, const Json& old_filename,
                                                    const Json& new_id, const Json& new_filename,
                                                    bool match_filename) {
    const std::scoped_lock lock(mutex_);
    Json data = load();
    auto p = find_project(data, pid);
    if (!p) {
        return std::unexpected(p.error());
    }
    if (*p == nullptr) {
        return {};
    }
    Json& project = **p;
    if (const auto tl = project.find("timeline"); tl != project.end() && tl->is_object()) {
        const Json& raw = tl->value("clips", Json());
        if (py_truthy(raw)) {
            auto clips = dict_items((*tl)["clips"], "«clips» de la timeline");
            if (!clips) {
                return std::unexpected(clips.error());
            }
            const Json kind = std::string(asset_kind);
            const std::string old = py_str(old_id);
            for (Json& c : *clips) {
                auto c_kind = get_or(c, "asset_kind", kNull);
                if (!c_kind) {
                    return std::unexpected(c_kind.error());
                }
                if (!py_equal(**c_kind, kind)) {
                    continue;
                }
                if (py_str(c.value("asset_id", Json())) == old ||
                    (match_filename && py_equal(c.value("filename", Json()), old_filename))) {
                    c["asset_scope"] = "library";
                    c["asset_id"] = new_id;
                    c["filename"] = new_filename;
                }
            }
        }
    }
    return save(data);
}

} // namespace gc::core
