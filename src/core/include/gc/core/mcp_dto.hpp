#pragma once

// El DTO semántico del servidor MCP (espejo de backend/app/mcp_server/dto.py de video-yt): lo
// que la IA ve de un proyecto, su timeline, su material y sus clips, sin el schema crudo ni los
// datos pesados (keyframes, words, segmentos): resúmenes y listas breves. Más los verbos del
// editor (`CAPABILITIES`) y el resumen de un análisis de YouTube y de un job.

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "gc/core/error.hpp"
#include "gc/core/json.hpp"
#include "gc/core/model.hpp"

namespace gc::core::mcp {

// dto.CAPABILITIES: los verbos que el editor soporta hoy, en su orden.
[[nodiscard]] std::span<const std::string_view> editor_capabilities();

// timeline_ops.aspect_of: el id de ASPECTS (16:9, 9:16, 1:1, 4:3, 3:4, 4:5) a menos del 1 % de
// w:h, el primero que lo esté; nada si no hay o si w o h no son positivos.
[[nodiscard]] std::optional<std::string_view> aspect_of(std::int64_t width, std::int64_t height);
// dto.aspect_ratio: el de aspect_of o w:h reducido por su mcd («1234:567»); «?» si w o h no son positivos.
[[nodiscard]] std::string aspect_ratio(std::int64_t width, std::int64_t height);

// dto._clip_timeline_duration: (out − in), como poco 0, entre la velocidad (no en textos ni
// imágenes; una velocidad 0 cuenta como 1 y una que no es positiva no divide).
[[nodiscard]] double clip_timeline_duration(const TimelineClip& clip);

// dto._timeline_dto: pistas, número de clips, duración y clips por tipo. nullptr: sin timeline.
[[nodiscard]] Json timeline_dto(const Timeline* tl);
// dto.timeline_detail: formato, pistas, el resumen de cada clip (sin words ni keyframes),
// marcadores y duración. Error donde Python lanza un TypeError: unos beats cuyo `times` es un
// número o un bool (`len()` falla).
[[nodiscard]] Result<Json> timeline_detail(const Timeline* tl);
// dto.clip_detail: el model_dump() del clip con su `timeline_duration`.
[[nodiscard]] Json clip_detail(const TimelineClip& clip);
// dto.media_list: el inventario del material (clips, audios, imágenes y transcripciones).
[[nodiscard]] Json media_list(const Project& project);
// dto.analyze_dto sobre el model_dump(mode="json") de un AnalyzeResponse.
[[nodiscard]] Json analyze_dto(const Json& response);
// dto.job_dto sobre el model_dump(mode="json") de un Job: con la cancelación pedida y sin
// acabar en «done», «cancelled»; y el resumen del resultado según su tipo.
[[nodiscard]] Json job_dto(const Json& job);
// dto.project_context: proyecto, formato, material, timeline e historial. `history` es el
// resumen de timeline_history ({can_undo, can_redo, checkpoints}) o null.
[[nodiscard]] Json project_context(const Project& project, const Json& history = nullptr);

} // namespace gc::core::mcp
