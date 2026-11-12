#pragma once

// Las tools de material del servidor MCP que no piden IA ni jobs (espejo de video-yt):
//   - replace_media_version (tools_media.py): las otras versiones de un vídeo de YouTube
//     (media_upgrade.find_versions, con dry_run) o cambiarlas en la timeline (replace_with, con la op
//     replace_media por timeline_store: un paso de deshacer). Un LookupError es resource_not_found con
//     la pista «list_media para ver los index.».
//   - delete_media (tools_media.py): quita el material y borra su archivo (no se deshace).
//   - search_sfx (tools_audio.py): la búsqueda de la biblioteca de SFX, con limit entre 1 y 300.
//   - set_clip_ai_description (tools_vision.py): la descripción de la IA, en description_ai.
// Los argumentos llegan ya validados por el servidor (validate_tool_args), con sus valores por defecto.

#include <string_view>
#include <vector>

#include "gc/core/error.hpp"
#include "gc/core/json.hpp"
#include "gc/core/mcp_tools_edit.hpp"

namespace gc::core::mcp {

// Las tools de este módulo con su acceso, en el orden de register de tools_media, tools_audio y tools_vision.
[[nodiscard]] const std::vector<EditToolInfo>& media_tools();

// La tool `tool` con los argumentos `args`: como call_edit_tool (un ValueError sale con su texto; un
// MCPError, con su `param` en `path` y su pista en `hint`). Una tool que no es de aquí, NotFound.
[[nodiscard]] Result<Json> call_media_tool(const EditToolsEnv& env, std::string_view tool, const Json& args);

} // namespace gc::core::mcp
