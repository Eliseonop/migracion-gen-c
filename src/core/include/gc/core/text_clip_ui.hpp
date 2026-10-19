#pragma once

// Textos en el editor (makeTextClip de editorModel.js; addText, newTextStyle, changeText y
// changeStyle de VideoEditor.jsx; defaultTextStyle de textstyles.js y upsertStyleKf de
// clipKeyframes.js). Trabaja sobre JSON, como el JS.

#include <string>
#include <string_view>

#include "gc/core/ids.hpp"
#include "gc/core/json.hpp"
#include "gc/core/model.hpp"

namespace gc::core {

inline constexpr std::string_view kDefaultText = "Texto predeterminado"; // DEFAULT_TEXT

[[nodiscard]] Json default_text_style();
// subtitleStyle: el tema «classic» de los subtítulos (el estilo de una pista de texto nueva).
[[nodiscard]] Json subtitle_style();
// newTextStyle: hereda el estilo de la pista; sin él, la caja de los subtítulos (tema «classic»).
[[nodiscard]] Json new_text_style(const Json& track_style);
[[nodiscard]] Json make_text_clip_json(std::string_view track_id, double start, double duration, std::string_view text,
                                       const Json& style, IdSource& ids);
// addText: del cabezal al final de la timeline, al menos 3 s.
[[nodiscard]] double new_text_duration(const Timeline& tl, double playhead);
// changeText: el texto y el nombre (sus 22 primeras unidades de UTF-16).
void change_text(TimelineClip& clip, std::string_view text);
// upsertStyleKf: los valores de estilo animables de `patch` en el keyframe de `local_t`.
void upsert_style_kf(TimelineClip& clip, double local_t, const Json& patch, double fps, IdSource& ids);
// changeStyle para un texto: el estilo fijo y, si esas propiedades ya están animadas (y no es
// `fixed`), su keyframe en el cabezal; la pose (x, y, escala…) sigue la regla de commitPose.
void change_text_style(TimelineClip& clip, const Json& patch, double local_t, double fps, bool fixed, IdSource& ids);

} // namespace gc::core
