#pragma once

// Cambiar la versión ligera de un vídeo por la de más calidad en la timeline, espejo de
// media_upgrade.py de video-yt: se edita con una versión ligera (un tramo del Clip Editor, un
// 480p…) y, con el vídeo completo en máxima calidad ya en el proyecto, se buscan las otras
// versiones del mismo vídeo de YouTube y se cambian los clips que las usan conservando cortes,
// encuadre y animaciones (la op `replace_media`, que se deshace con undo).
//
// Cada archivo empieza en un segundo del vídeo ORIGINAL (su offset): un vídeo completo en 0; un
// tramo guardado desde YouTube, en su `start`. El encuadre va normalizado, así que solo vale si
// la PROPORCIÓN coincide (±2 %).

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "gc/core/error.hpp"
#include "gc/core/json.hpp"
#include "gc/core/model.hpp"
#include "gc/core/projects_store.hpp"

namespace gc::core {

// detect.dims: ancho y alto de un vídeo; nullopt si no se pueden leer (sin medidas no se
// comprueba la proporción). Se inyecta (gc_media).
using VideoDimsProbe = std::function<std::optional<std::pair<std::int64_t, std::int64_t>>(const std::filesystem::path&)>;

// timeline_store.apply_op(pid, op, params): aplica una op con historial y guarda la timeline.
// Devuelve al menos {changed, warnings, can_undo}.
using TimelineOpApplier = std::function<Result<Json>(std::string_view pid, std::string_view op, const Json& params)>;

// El id de YouTube de un material: el suyo (provider youtube + external_id o un source_url de
// YouTube) o, si es un segmento por referencia, el del vídeo del que sale.
[[nodiscard]] std::optional<std::string> material_youtube_id(const Project& project, const ClipInfo& clip);

// Segundo del vídeo original en el que empieza el archivo del material, y su duración.
[[nodiscard]] double material_file_offset(const ClipInfo& owner);
[[nodiscard]] double material_file_duration(const ClipInfo& owner);

// find_versions: {material, versions, upgradable}. Cada versión: {index, id, label, filename,
// quality, offset, duration, timeline_uses, lower, compatible, reason}, por usos en la timeline
// (más primero) y offset. `ident` es el index o el id del vídeo completo (no un segmento).
[[nodiscard]] Result<Json> find_media_versions(const ProjectsStore& store, std::string_view pid,
                                               std::string_view ident, const VideoDimsProbe& dims);

// replace_with: pone `ident` en la timeline en lugar de sus versiones de menos calidad
// compatibles o, con `old_idents`, de esas (tienen que ser compatibles). Sus segmentos por
// referencia pasan también al archivo nuevo. {ok, replaced, changed, warnings, versions,
// segments, can_undo}.
[[nodiscard]] Result<Json> replace_media_with(ProjectsStore& store, std::string_view pid, std::string_view ident,
                                              const std::vector<std::string>& old_idents, const VideoDimsProbe& dims,
                                              const TimelineOpApplier& apply_op);

// youtube_sources: {id de YouTube: url} que mejorar: el de `url` (error si no es de YouTube) o,
// sin url, los de los vídeos que usa la timeline (su source_url de YouTube o el enlace watch?v=).
[[nodiscard]] Result<Json> youtube_upgrade_sources(const ProjectsStore& store, std::string_view pid,
                                                   std::string_view url);

// best_full_version: el vídeo COMPLETO de YouTube `vid` de más resolución del proyecto
// («Agregar vídeo completo»: sin segmento y con offset 0) y su calidad; nullopt si no hay.
struct FullVersion {
    ClipInfo clip;
    std::optional<std::int64_t> quality; // el lado corto, o nullopt sin medidas
};
[[nodiscard]] Result<std::optional<FullVersion>> best_full_youtube_version(const ProjectsStore& store,
                                                                            std::string_view pid,
                                                                            std::string_view vid,
                                                                            const VideoDimsProbe& dims);

} // namespace gc::core
