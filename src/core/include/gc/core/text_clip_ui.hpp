#pragma once

// Textos en el editor (makeTextClip de editorModel.js; addText, newTextStyle, changeText y
// changeStyle de VideoEditor.jsx; defaultTextStyle de textstyles.js y upsertStyleKf de
// clipKeyframes.js). Trabaja sobre JSON, como el JS.

#include <optional>
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
// --- Temas de subtítulos (subtitleThemes.js) y estilos preestablecidos «Aa» (textstyles.js) ---
// Las tablas salen del JS con tools/gen/gen_text_themes.mjs.

[[nodiscard]] const Json& subtitle_themes();
[[nodiscard]] const Json* theme_by_id(std::string_view id);
// applyThemeToStyle: el tema, conservando la caja (x, y, w), max_words y la opacidad.
[[nodiscard]] Json apply_theme_to_style(const Json& current, const Json& theme);
// clearTextTheme: el texto común, conservando caja, tamaño, opacidad y max_words.
[[nodiscard]] Json clear_text_theme(const Json& current);
// selectedSubtitleThemeId: el tema puesto (nada si se le quitó el karaoke).
[[nodiscard]] std::optional<std::string> selected_subtitle_theme_id(const Json& style);
// applyOrClearTheme: un clic aplica el tema y otro clic en el mismo lo quita (`theme` nulo = quitar).
[[nodiscard]] Json apply_or_clear_theme(const Json& current, const Json* theme);
[[nodiscard]] const Json& text_looks();
// lookPatch: el parche de un estilo preestablecido (`look` nulo = «Ninguno»).
[[nodiscard]] Json look_patch(const Json* look);
// activeLookId: "none", el id del estilo que coincide o nada.
[[nodiscard]] std::optional<std::string> active_look_id(const Json& style);

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
