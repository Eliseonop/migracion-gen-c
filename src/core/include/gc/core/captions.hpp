#pragma once

// Subtítulos: segmentos de una transcripción → clips de texto
// (espejo de backend/app/fragment.py y de editorModel.js / textKaraoke.js).
//
// Cada segmento de Whisper que cae dentro del clip fuente da un clip de texto con
// sus palabras reales (tiempos relativos al clip) y su `origin` hacia la
// transcripción; si tiene más palabras que `style.max_words`, se parte en varios.

#include <optional>
#include <string>
#include <vector>

#include "gc/core/error.hpp"
#include "gc/core/ids.hpp"
#include "gc/core/json.hpp"

namespace gc::core {

// Palabras del texto (separadas por cualquier espacio).
[[nodiscard]] std::vector<std::string> split_caption_words(const std::string& text);
// Trozos de como mucho `max_words` palabras (sin límite válido: un solo trozo).
[[nodiscard]] std::vector<std::string> chunk_caption_text(const std::string& text, const Json& max_words);

// Clips de texto (dicts, aún sin validar) para los segmentos que caen en el tramo
// [in_point, out_point] del clip fuente colocado en `start`.
[[nodiscard]] Result<std::vector<Json>> text_clips_from_transcript(double src_start, double src_in, double src_out,
                                                                   const Json& segments, const std::string& track_id,
                                                                   const Json& style, const Json& transcript_id,
                                                                   IdSource& ids);

} // namespace gc::core
