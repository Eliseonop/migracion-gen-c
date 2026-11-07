#pragma once

// Subtítulos del editor a partir de una transcripción (editorModel.js): qué clips se pueden transcribir
// (canCaptionClip), la referencia al material transcrito (sourceRefOf) y los clips de texto del tramo del
// clip fuente (textClipsFromTranscript). Es lo de captions.hpp (fragment.py) con lo que añade el editor: la
// referencia al material en `origin` y los null de JS (un max_words o un índice nulo toman su defecto).

#include <string_view>
#include <vector>

#include "gc/core/ids.hpp"
#include "gc/core/json.hpp"
#include "gc/core/model.hpp"

namespace gc::core {

// canCaptionClip: un audio o un SFX; un vídeo del proyecto (no de la biblioteca ni de una colección) con su
// material. Un texto, nunca.
[[nodiscard]] bool can_caption_clip(const TimelineClip& clip);

// sourceRefOf: {kind, asset_id, filename y, si tiene, name} del material transcrito.
[[nodiscard]] Json source_ref_of(const TimelineClip& clip);

// textClipsFromTranscript: un clip de texto (text_role «caption») por segmento que cae en el tramo del clip
// fuente, con sus palabras reales relativas al clip y `origin` hacia la transcripción (y `source_ref`, si
// lo hay), partido en cuadros de `style.max_words` palabras (8 si no tiene). Como se guardan, por el modelo.
[[nodiscard]] std::vector<TimelineClip> caption_text_clips(const TimelineClip& src, const Json& segments,
                                                           std::string_view track_id, const Json& style,
                                                           const Json& transcript, const Json& source_ref,
                                                           IdSource& ids);

} // namespace gc::core
