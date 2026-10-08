#pragma once

// El almacén de proyectos (espejo de backend/app/projects.py): data/projects.json con los
// proyectos y las carpetas del inicio (grupos), y lo que se hace con ellos desde el inicio y
// la API: crear, renombrar, mover, duplicar (con sus archivos), borrar, añadir y editar
// materiales, guiones, composiciones de Motion y la timeline.
//
// Como en Python, se trabaja sobre el JSON crudo (se conservan las claves que no son del
// modelo y su orden) y lo que se devuelve está validado. Un projects.json que no se puede
// leer (no existe, BOM, JSON roto) vale {"projects": []}; uno con otra forma (sin lista
// «projects», proyectos sin «id»…) falla donde fallaría Python. Los errores que ve el usuario
// llevan el texto de Python: «El nombre no puede estar vacío.» y «La carpeta no existe.»
// (InvalidArgument).
//
// Dónde viven los archivos de un proyecto (storage.py): en su carpeta propia (`folder`) o en
// <clips>/<id>, con las subcarpetas video/, audio/ e image/.

#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "gc/core/error.hpp"
#include "gc/core/ids.hpp"
#include "gc/core/json.hpp"
#include "gc/core/model.hpp"

namespace gc::core {

// storage.default_base: <clips>/<id>.
[[nodiscard]] std::filesystem::path project_default_base(const std::filesystem::path& clips_dir,
                                                         std::string_view project_id);
// storage.project_base: la carpeta propia del proyecto o la de por defecto.
[[nodiscard]] std::filesystem::path project_base(const std::filesystem::path& clips_dir, const Project& project);
// storage.ensure_dirs: la carpeta y sus subcarpetas video/, audio/ e image/.
[[nodiscard]] Result<void> ensure_project_dirs(const std::filesystem::path& base);
// storage.resolve_media: <carpeta del proyecto>/<kind>/<filename> si `kind` es video, audio o
// image y la ruta queda DENTRO de la carpeta del proyecto (no tiene por qué existir).
[[nodiscard]] std::optional<std::filesystem::path> resolve_project_media(const std::filesystem::path& clips_dir,
                                                                         const Project& project,
                                                                         std::string_view kind,
                                                                         std::string_view filename);
// storage.safe_name: un título como nombre de archivo, sin <>:"/\|?*, saltos ni tabuladores,
// con los espacios seguidos en uno y como mucho `maxlen` caracteres; «video» si no queda nada.
[[nodiscard]] std::string safe_file_name(std::string_view title, std::size_t maxlen = 60);

// _now() de projects.py: la hora UTC en ISO 8601 con segundos («2026-10-08T12:00:00+00:00»).
[[nodiscard]] std::string utc_now_iso();

class ProjectsStore {
public:
    using Clock = std::function<std::string()>;

    // `file`: data/projects.json. `clips_dir`: donde van los proyectos sin carpeta propia
    // (config.OUTPUT_DIR). `now`: la hora de created_at (inyectable para los tests).
    ProjectsStore(std::filesystem::path file, std::filesystem::path clips_dir, Clock now = utc_now_iso);

    // _load: el JSON tal cual, o {"projects": []} si no existe o no se puede leer.
    [[nodiscard]] Json load() const;

    [[nodiscard]] Result<std::vector<Project>> list_projects() const;
    [[nodiscard]] Result<std::optional<Project>> get_project(std::string_view pid) const;
    // El nombre sin espacios alrededor o «Sin título». Un grupo que no existe se ignora.
    [[nodiscard]] Result<Project> create_project(std::string_view name, std::optional<std::string_view> group_id,
                                                 IdSource& ids);

    // Carpetas del inicio: solo organizan la lista (group_id de cada proyecto).
    [[nodiscard]] Result<std::vector<ProjectGroup>> list_groups() const;
    [[nodiscard]] Result<ProjectGroup> create_group(std::string_view name, IdSource& ids);
    [[nodiscard]] Result<std::optional<ProjectGroup>> rename_group(std::string_view gid, std::string_view name);
    // Borra la carpeta; sus proyectos no se borran, vuelven a la raíz. false si no existía.
    [[nodiscard]] Result<bool> delete_group(std::string_view gid);
    // Mete el proyecto en una carpeta (nullopt o vacío = a la raíz). Error si la carpeta no existe.
    [[nodiscard]] Result<std::optional<Project>> move_project(std::string_view pid,
                                                              std::optional<std::string_view> group_id);

    [[nodiscard]] Result<std::optional<Project>> rename_project(std::string_view pid, std::string_view name);
    // Copia el proyecto con id nuevo y nombre «<nombre> (copia)», justo delante del original, y
    // sus archivos (sin exports/) a la carpeta por defecto del nuevo; las URLs /api/media/<pid>/
    // y /api/projects/<pid>/ guardadas pasan al id nuevo.
    [[nodiscard]] Result<std::optional<Project>> duplicate_project(std::string_view pid, IdSource& ids);
    [[nodiscard]] Result<std::optional<Project>> set_folder(std::string_view pid, const Json& path);
    // false si no existía (el archivo se reescribe igual, como en Python).
    [[nodiscard]] Result<bool> delete_project(std::string_view pid);

    // Materiales. add_clips hace upsert por `index` (regenerar un tramo actualiza su clip).
    [[nodiscard]] Result<void> add_clips(std::string_view pid, const std::vector<ClipInfo>& clips, IdSource& ids);
    [[nodiscard]] Result<void> add_transcript(std::string_view pid, const Transcript& transcript);
    [[nodiscard]] Result<void> add_audio(std::string_view pid, const AudioInfo& audio);
    [[nodiscard]] Result<void> add_image(std::string_view pid, const ImageInfo& image);
    // `kind` es la lista («clips», «audios», «images»); `ident`, su index o su id. Cambia los
    // campos de `fields` que no son null y devuelve el material.
    [[nodiscard]] Result<std::optional<Json>> update_material(std::string_view pid, std::string_view kind,
                                                              std::string_view ident, const Json& fields);
    // Lo quita del proyecto y lo devuelve (para borrar su archivo).
    [[nodiscard]] Result<std::optional<Json>> remove_material(std::string_view pid, std::string_view kind,
                                                              std::string_view ident);
    // Aplica un manifest editado a mano: los materiales se emparejan por `file` y solo se
    // tocan los campos editables (y el nombre del proyecto).
    [[nodiscard]] Result<std::optional<Project>> apply_manifest(std::string_view pid, const Json& manifest);

    [[nodiscard]] Result<std::optional<Project>> save_timeline(std::string_view pid, const Json& timeline);
    // Upsert por id de una composición de Motion Studio; nada si no tiene id.
    [[nodiscard]] Result<std::optional<Project>> save_motion_composition(std::string_view pid, const Json& comp);
    [[nodiscard]] Result<bool> delete_motion_composition(std::string_view pid, std::string_view cid);
    [[nodiscard]] Result<bool> save_scene_directions(std::string_view pid, const Json& doc);
    [[nodiscard]] Result<bool> save_visual_blueprint(std::string_view pid, const Json& doc);
    // Los clips de la timeline de `asset_kind` que apuntan a `old_id` (o a `old_filename`, si
    // `match_filename`) pasan a apuntar al ítem de biblioteca `new_id`/`new_filename`.
    [[nodiscard]] Result<void> retarget_timeline_asset(std::string_view pid, std::string_view asset_kind,
                                                       const Json& old_id, const Json& old_filename,
                                                       const Json& new_id, const Json& new_filename,
                                                       bool match_filename = true);

    [[nodiscard]] const std::filesystem::path& file() const noexcept { return file_; }
    [[nodiscard]] const std::filesystem::path& clips_dir() const noexcept { return clips_dir_; }

private:
    [[nodiscard]] Result<void> save(const Json& data) const;
    // Añade `value` a la lista `key` del proyecto `pid` (add_transcript, add_audio, add_image).
    [[nodiscard]] Result<void> append_to(std::string_view pid, const char* key, Json value);
    // Pone `doc` en la clave `key` del proyecto `pid`; false si no está.
    [[nodiscard]] Result<bool> set_key(std::string_view pid, const char* key, const Json& doc);

    std::filesystem::path file_;
    std::filesystem::path clips_dir_;
    Clock now_;
    mutable std::mutex mutex_;
};

} // namespace gc::core
