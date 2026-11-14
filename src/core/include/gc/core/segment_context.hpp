#pragma once

// Lo de backend/app/motion/segment_context.py que usa la dirección de escena: los términos clave de
// un texto (`key_terms`), qué ocupa ya la timeline en un tramo (`timeline_context`) y el formato y el
// estilo de los subtítulos (`style_context`). El resto del contexto de un tramo para Motion (el guion
// alrededor, el material relevante y build_segment_context) llega con Motion.

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "gc/core/json.hpp"
#include "gc/core/model.hpp"

namespace gc::core {

inline constexpr std::size_t kMaxKeyTerms = 6;  // MAX_KEY_TERMS
inline constexpr std::size_t kMaxElements = 20; // MAX_ELEMENTS

// key_terms: los términos clave baratos de un texto, en el orden en que aparecen y sin repetir
// (sin tildes ni mayúsculas): nombres propios a mitad de frase (de 4 letras o más), palabras de
// 6 letras o más y palabras con cifras, sin las muletillas de _STOPWORDS. Se para al llegar a
// `limit`.
[[nodiscard]] std::vector<std::string> key_terms(std::string_view text, std::size_t limit = kMaxKeyTerms);

// resolve_text_role(c) == "caption" (text_role.py): un clip de texto con text_role «caption», o sin
// «caption» ni «free» pero con origen o con palabras con tiempo.
[[nodiscard]] bool is_caption_clip(const TimelineClip& clip);

// timeline_context(proj, start, end, playhead, clip_id):
//   - activeClip: el clip `clip_id` si existe; si no, el de vídeo, imagen, figura o motion de una pista
//     visible que está en pantalla en `playhead` (o en `start`), el de la pista de vídeo más alta.
//   - existingElements: lo que ya ocupa [start, end] (sin audio ni subtítulos), por orden de inicio y
//     20 como mucho, con su pista, nombre, tiempos, descripción del material y nota.
//   - motionInRange: los motion del tramo con la fracción que solapan (`overlap`).
[[nodiscard]] Json timeline_context(const Project& project, double start, double end,
                                    std::optional<double> playhead = std::nullopt, std::string_view clip_id = {});

// style_context(proj, start, end): el formato de salida (width, height, fps) y, del subtítulo que
// empieza más cerca de `start`, la fuente, el color, el acento y su `y` con la banda que no conviene
// tapar (avoidY, ±0,07). Python no mira `end`.
[[nodiscard]] Json style_context(const Project& project, double start, double end);

} // namespace gc::core
