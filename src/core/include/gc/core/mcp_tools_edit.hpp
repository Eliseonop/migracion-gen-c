#pragma once

// Las tools de edición del servidor MCP (espejo de backend/app/mcp_server/tools_edit.py de
// video-yt) y el adaptador con estado que tienen debajo (timeline_store.py): cada tool traduce los
// argumentos a una op de edit.hpp y la aplica de forma transaccional sobre el proyecto, con su
// historial en <data>/history/<pid>.json. El resultado es siempre
// {ok, changed, warnings, can_undo, can_redo, checkpoints, timeline: <timeline_detail>}.
//
// Los argumentos llegan como en una llamada del MCP: un objeto con sus nombres, sin los opcionales
// si no se pasan. La comprobación de tipos del servidor (la que en Python hace Pydantic antes de
// entrar en la tool) irá con el transporte: aquí un argumento de otro tipo falla en la op.
//
// Lo que necesita leer el material va en callbacks, porque gc_core no decodifica: la duración de
// un SFX y la envolvente del audio que sigue animate_clip. Sin callback, un SFX dura 3 s (como sin
// ffprobe) y seguir un audio no está disponible.
//
// Faltan cuatro tools, porque piden gc_media o los jobs: freeze_frame, detect_beats, track_object y
// sound_design (kPendingEditTools).

#include <array>
#include <filesystem>
#include <functional>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "gc/core/error.hpp"
#include "gc/core/ids.hpp"
#include "gc/core/json.hpp"
#include "gc/core/media_upgrade.hpp"
#include "gc/core/model.hpp"
#include "gc/core/projects_store.hpp"

namespace gc::core::mcp {

struct EditToolsEnv {
    using ProbeDuration = std::function<double(const std::filesystem::path&)>;
    // clip_motion.sample_rms_envelope sobre el material del clip: [(t, amp)]. nullopt: no se
    // encuentra el archivo (compose._clip_path).
    using AudioEnvelope = std::function<std::optional<std::vector<std::pair<double, double>>>(
        const Project&, const TimelineClip& audio, double source_start, double duration)>;

    ProjectsStore& store;
    std::filesystem::path data_dir; // history/<pid>.json y settings.json (el fps de una timeline nueva)
    IdSource& ids;
    std::optional<std::filesystem::path> sfx_base = std::nullopt; // sfx.get_base()
    ProbeDuration probe_duration = nullptr;                       // _probe_duration
    AudioEnvelope audio_envelope = nullptr;
    // detect.dims de media_upgrade (replace_media_version): sin él, ningún vídeo tiene medidas y no se
    // comprueba la proporción.
    VideoDimsProbe video_dims = nullptr;
};

inline constexpr std::array<std::string_view, 4> kPendingEditTools{"freeze_frame", "detect_beats", "track_object",
                                                                   "sound_design"};

struct EditToolInfo {
    std::string_view name;
    std::string_view access; // read | write | destructive
};

// Las tools de tools_edit.register, en su orden, sin kPendingEditTools.
[[nodiscard]] const std::vector<EditToolInfo>& edit_tools();

// La tool `tool` con los argumentos `args`. Un ValueError de Python sale con su texto
// (NotFound o InvalidArgument; `path` es el `param` de un MCPError) y un crash, como
// ErrorCode::Internal. Una tool que no existe, NotFound.
[[nodiscard]] Result<Json> call_edit_tool(const EditToolsEnv& env, std::string_view tool, const Json& args);

// timeline_store.apply_op para las tools de otros módulos: la op con sus reglas, el historial y la
// timeline guardados. {changed, warnings, can_undo}.
[[nodiscard]] Result<Json> timeline_store_apply_op(const EditToolsEnv& env, std::string_view pid, std::string_view op,
                                                   const Json& params);

} // namespace gc::core::mcp
