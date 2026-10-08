#pragma once

// Biblioteca de materiales guardados: data/library/library.json y sus archivos en
// data/library/{video,audio,image} (espejo de library.py y storage.resolve_library_media).
//
// Cada material es un dict libre ({id, resource_type, filename, label, origin…}); como en
// Python, el tipo, el origen y la fuente se deducen si faltan. «Guardar en la biblioteca»
// desde un proyecto (save_from_project) copia el archivo y cambia el proyecto (ProjectsStore).

#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "gc/core/error.hpp"
#include "gc/core/ids.hpp"
#include "gc/core/json.hpp"
#include "gc/core/model.hpp"
#include "gc/core/projects_store.hpp"

namespace gc::core {

// "clip", "audio" o "image" (deducido si el material no lo dice).
[[nodiscard]] std::string library_resource_type(const Json& item);
// (origen, fuente): ("youtube", "external"), ("tts", "generated"), ("upload", "external")…
[[nodiscard]] std::pair<std::string, std::string> library_origin_source(const Json& item,
                                                                        std::string_view resource_type);
// El material como lo ve la interfaz: tipo, ámbito, origen, fuente y, si está en la
// biblioteca, la URL del archivo. Error si no es un dict.
[[nodiscard]] Result<Json> material_dto(const Json& item, std::string_view scope);

// Proyectos cuya timeline usa ese material de la biblioteca: [{id, name}].
[[nodiscard]] Json projects_using(const ProjectsFile& projects, std::string_view item_id);

// data/library/<kind>/<filename>, o nada si el tipo no vale o el nombre sale de la carpeta.
[[nodiscard]] std::optional<std::filesystem::path> resolve_library_media(const std::filesystem::path& data_dir,
                                                                         std::string_view kind,
                                                                         std::string_view filename);

class LibraryFile {
public:
    explicit LibraryFile(std::filesystem::path data_dir) : data_dir_(std::move(data_dir)) {}

    // {version, items}: sin archivo, o si no se puede leer, una biblioteca vacía.
    [[nodiscard]] Json load() const;
    // {clips, audios, images}, cada lista del guardado más reciente al más antiguo.
    [[nodiscard]] Result<Json> list() const;
    // Cambia el título y la descripción (lo que la IA usa como contexto).
    [[nodiscard]] Result<Json> update_item(std::string_view item_id, const Json& fields);
    // Metadata semántica del material (la usa la IA para elegir y componer).
    [[nodiscard]] Result<Json> set_semantic(std::string_view item_id, const Json& semantic);
    // Lo quita de la biblioteca y borra su archivo. Error si algún proyecto lo usa.
    [[nodiscard]] Result<Json> unsave(std::string_view item_id, const ProjectsFile& projects);
    // «Guardar en la biblioteca» (save_from_project): copia el archivo del material (`ident` es
    // el index de un clip o el id de un audio o una imagen) a data/library/<video|audio|image>/
    // <título>_<id><ext>, lo da de alta, apunta la timeline del proyecto al nuevo ítem, lo quita
    // del proyecto y borra el original, salvo un segmento que comparte archivo con otros. Si
    // falla algo después de copiar, se deshacen el alta y la copia (no lo que ya cambió en la
    // timeline, como en Python). Devuelve su DTO. Errores que ve el usuario (NotFound): «Tipo no
    // válido.», «Proyecto no encontrado.», «Material no encontrado.» y «Archivo no encontrado.».
    [[nodiscard]] Result<Json> save_from_project(ProjectsStore& projects, std::string_view project_id,
                                                 std::string_view resource_type, std::string_view ident, IdSource& ids,
                                                 const std::function<std::string()>& now = utc_now_iso);

    [[nodiscard]] std::filesystem::path file() const { return data_dir_ / "library" / "library.json"; }

private:
    [[nodiscard]] Result<void> save(const Json& data) const;

    std::filesystem::path data_dir_;
    mutable std::mutex mutex_;
};

} // namespace gc::core
