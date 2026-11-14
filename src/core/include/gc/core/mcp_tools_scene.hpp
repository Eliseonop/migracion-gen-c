#pragma once

// Las tools de dirección de escena del servidor MCP que no piden IA (espejo de
// backend/app/mcp_server/tools_scene.py de video-yt), envolturas de scene_direction.hpp:
//   - scene_get_blueprint: la dirección visual global, el catálogo de direcciones y el vocabulario.
//   - scene_set_blueprint: guarda la dirección global (una `direction` fuera del catálogo es un error).
//   - scene_direction_get: la escaleta, el guion en frases, los modos y el catálogo de materiales
//     (del proyecto y de la biblioteca, data_dir/library/library.json).
//   - scene_direction_status: el mapa del montaje (status_summary).
//   - scene_direction_pack: el pack de contexto de un tramo (build_pack), su texto para la IA, los
//     tokens estimados, los beats con el ritmo `pace` (PACES de motion/scene.py) y el brief inicial.
//   - scene_rank_materials: los materiales ordenados por los términos clave de un texto.
//   - scene_direction_auto_split: la escaleta propuesta, sin guardar.
//   - scene_direction_update_segment: los campos de un tramo (solo los de _PATCH_KEYS).
//   - scene_direction_set: guarda la escaleta entera.
//   - scene_set_material_meta: la metadata semántica de un clip o una imagen, del proyecto o de la
//     biblioteca.
// Los MCPError de Python llevan su código en Error::mcp_code (con retryable y param), porque no
// siempre coincide con el que se deduce del texto («Pasa 'patch'…» es invalid_parameter). La hora de
// `updated_at` es EditToolsEnv::unix_now (sin él, la del sistema).

#include <string_view>
#include <vector>

#include "gc/core/error.hpp"
#include "gc/core/json.hpp"
#include "gc/core/mcp_tools_edit.hpp"

namespace gc::core::mcp {

// Las tools de este módulo con su acceso, en el orden de tools_scene.register.
[[nodiscard]] const std::vector<EditToolInfo>& scene_tools();

// La tool `tool` con los argumentos `args` (como call_media_tool). Una tool que no es de aquí, NotFound.
[[nodiscard]] Result<Json> call_scene_tool(const EditToolsEnv& env, std::string_view tool, const Json& args);

} // namespace gc::core::mcp
