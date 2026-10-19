#pragma once

// Textos en el editor (makeTextClip de editorModel.js; addText, newTextStyle, changeText y
// changeStyle de VideoEditor.jsx; defaultTextStyle de textstyles.js y upsertStyleKf de
// clipKeyframes.js). Trabaja sobre JSON, como el JS.

#include <optional>
#include <string>
#include <vector>
#include <string_view>

#include "gc/core/error.hpp"
#include "gc/core/ids.hpp"
#include "gc/core/json.hpp"
#include "gc/core/model.hpp"
#include "gc/core/timeline_ui.hpp"

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
inline constexpr double kTextDefaultDuration = 3.0; // TEXT_DEFAULT_DUR
// La rama de texto de placeAsset: el texto (o un tema, `theme_id`) soltado en la pista, con el
// estilo de la pista (`track_style`, nulo si no tiene) y la duración `fit` si reemplaza a un clip.
[[nodiscard]] Json place_text_clip(const Json& payload, const Json& track_style, std::string_view track_id,
                                   double start, std::optional<double> fit, IdSource& ids);
// addText: del cabezal al final de la timeline, al menos 3 s.
[[nodiscard]] double new_text_duration(const Timeline& tl, double playhead);
// changeText: el texto y el nombre (sus 22 primeras unidades de UTF-16).
void change_text(TimelineClip& clip, std::string_view text);
// upsertStyleKf: los valores de estilo animables de `patch` en el keyframe de `local_t`.
void upsert_style_kf(TimelineClip& clip, double local_t, const Json& patch, double fps, IdSource& ids);
// changeStyle para un texto: el estilo fijo y, si esas propiedades ya están animadas (y no es
// `fixed`), su keyframe en el cabezal; la pose (x, y, escala…) sigue la regla de commitPose.
void change_text_style(TimelineClip& clip, const Json& patch, double local_t, double fps, bool fixed, IdSource& ids);

// --- El rombo por sección del estilo (Color, Trazo, Fondo, Brillo, Sombra…) -------------------------

// styleKfState: "on" (alguna de `keys` tiene keyframe en este fotograma), "empty" (animadas, sin
// keyframe aquí) u "off".
[[nodiscard]] std::string style_kf_state(const TimelineClip& clip, double local_t, const std::vector<std::string>& keys,
                                         double fps);
// removeStyleKfAt: quita `keys` del keyframe de `local_t` (y el keyframe, si se queda vacío).
void remove_style_kf_at(TimelineClip& clip, double local_t, const std::vector<std::string>& keys, double fps,
                        IdSource& ids);
// toggleStyleKf de VideoEditor.jsx: quita esas propiedades del keyframe del cabezal o pone en él el
// valor que se ve (estilo de la pista + del clip + lo animado).
void toggle_style_kf(TimelineClip& clip, const Json& track_style, double local_t, const std::vector<std::string>& keys,
                     double fps, IdSource& ids);

// --- «Mis estilos» (favorites.js): los favoritos de los ajustes -----------------------------------

// normalizeFavorites: {sfx, audios, textStyles} (los ids como texto; estilos solo con `style`).
[[nodiscard]] Json normalize_favorites(const Json& raw);
// nameTextStyleFavorite: «<tema> · <px>px», con «(2)», «(3)»… si ya existe.
[[nodiscard]] std::string name_text_style_favorite(const Json& style, const Json& existing);
// makeTextStyleFavorite: {id, name, style}.
[[nodiscard]] Json make_text_style_favorite(const Json& style, const Json& existing, IdSource& ids);

// --- Fragmentar subtítulos en cuadros (editorModel.js) -----------------------------------------

// splitClipByMaxWords: el texto en trozos de `max_words` palabras, repartiendo el tiempo por sus
// palabras reales (o a partes iguales) y llevando `origin`. Un trozo o menos: el clip tal cual.
[[nodiscard]] std::vector<Json> split_clip_by_max_words(const TimelineClip& clip, const Json& max_words,
                                                        IdSource& ids);
// splitOneTextClip: ese clip de texto partido en su sitio de la lista.
[[nodiscard]] Result<ClipList> split_one_text_clip(const ClipList& clips, std::string_view clip_id,
                                                   const Json& max_words, IdSource& ids);
// splitTrackTextByMaxWords: todos los subtítulos (no los textos libres) de la pista.
[[nodiscard]] Result<ClipList> split_track_text(const ClipList& clips, std::string_view track_id,
                                                const Json& max_words, IdSource& ids);

} // namespace gc::core
