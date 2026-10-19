#pragma once

// Keyframes genéricos: snapshots de propiedades + interpolación
// (espejo de backend/app/clip_keyframes.py y frontend/src/lib/clipKeyframes.js).
//
// `clip.keyframes` = {enabled, items: [{id, t, interpolation, bezier?, props}]},
// con `t` en tiempo LOCAL del clip. Cada propiedad se interpola entre los items
// que la tienen; si ninguno la tiene, manda su valor estático (transform, estilo,
// figura, máscara, volumen, efectos de audio).
//
// Es la base de la evaluación del render (F3): la pose de un clip en el instante
// t sale de clip_props_at.

#include <array>
#include <cstddef>
#include <optional>
#include <string_view>
#include <vector>

#include "gc/core/error.hpp"
#include "gc/core/ids.hpp"
#include "gc/core/json.hpp"
#include "gc/core/model.hpp"

namespace gc::core {

// --- Curvas -----------------------------------------------------------------------------

enum class Interp { Linear, EaseIn, EaseOut, EaseInOut, CubicIn, CubicOut, CubicInOut, BackOut, Bezier, Hold };

[[nodiscard]] std::string_view interp_name(Interp interp);
// Nombre → curva. "direct"/"step" = hold; cualquier otra cosa = linear.
[[nodiscard]] Interp normalize_interp(const Json& value);

using Bezier = std::array<double, 4>; // [x1, y1, x2, y2] como cubic-bezier de CSS
inline constexpr Bezier kBezierDefault{0.42, 0.0, 0.58, 1.0};
inline constexpr Bezier kBackOut{0.34, 1.56, 0.64, 1.0};

// x acotada a 0–1 (la curva avanza en el tiempo); y a −1…2 (rebotes).
[[nodiscard]] std::optional<Bezier> normalize_bezier(const Json& value);
// Progreso y de la curva en el instante u (0–1).
[[nodiscard]] double bezier_y(const std::optional<Bezier>& bezier, double u);
// Progreso 0–1 con la curva `kind` (y su bézier si es "bezier").
[[nodiscard]] double ease_t(double u, Interp kind, const Json& bezier = Json());

// --- Propiedades animables -------------------------------------------------------------------

// Pose, giro 3D, trazo, máscara (masks[0]), volumen y efectos de audio, en este orden.
inline constexpr std::array<std::string_view, 31> kKfPropKeys{
    "x",  "y",  "scale", "rotation", "opacity", "cx",       "cy",         "zoom",       "rot_x",     "rot_y",     "draw",
    "mx", "my", "mw",    "mh",       "msx",     "msy",      "mrot",       "mfeather",   "volume",    "eq",        "compressor",
    "reverb", "echo", "denoise", "distortion", "underwater", "telephone", "radio", "megaphone", "muffled"};

// Estilo de texto animable (cada propiedad con sus propios keyframes).
inline constexpr std::array<std::string_view, 23> kTextStyleKfKeys{
    "color",        "border_color",   "border_width",    "bg",           "bg_opacity",     "bg_radius",
    "bg_pad_x",     "bg_pad_y",       "bg_dx",           "bg_dy",        "shadow_color",   "shadow_opacity",
    "shadow_blur",  "shadow_distance", "shadow_angle",   "glow_color",   "glow_intensity", "glow_range",
    "glow_dx",      "glow_dy",        "curve",           "stretch_x",    "stretch_y"};

inline constexpr double kVolumeMin = 0.0;
inline constexpr double kVolumeMax = 2.0;
inline constexpr double kKfSnap = 0.06; // dos keyframes a menos de esto son el mismo

// Valores de todas las propiedades animables en un instante.
struct KfProps {
    std::array<double, kKfPropKeys.size()> values{};

    [[nodiscard]] double get(std::string_view key) const;
    void set(std::string_view key, double value);
    [[nodiscard]] Json to_json() const; // {"x": …, …} en el orden de kKfPropKeys
};

[[nodiscard]] std::optional<std::size_t> kf_prop_index(std::string_view key);

[[nodiscard]] bool keyframes_enabled(const TimelineClip& clip);
// validKfValue: un color «#rrggbb» en las propiedades de color; un número finito en las demás.
[[nodiscard]] bool kf_value_valid(std::string_view key, const Json& value);
[[nodiscard]] double clamp_volume(const Json& value); // 0–2; lo que no es número = 1
// Valores sin animar (transform/estilo/figura, máscara, volumen, audio).
[[nodiscard]] KfProps static_props(const TimelineClip& clip);
// Propiedades en `t` interpoladas entre `items` (keyframes {t, interpolation, props…},
// tal como los recorre Python). Lo que ningún item anima sale de `fallback`; un NaN
// de `fallback` vale lo mismo que en un clip vacío (`_merge` de video-yt).
[[nodiscard]] KfProps interp_items(const std::vector<Json>& items, double t, const KfProps& fallback);
// Propiedades en `local_t` (con keyframes si están activos).
[[nodiscard]] KfProps clip_props_at(const TimelineClip& clip, double local_t);
// Como clip_props_at, pero lo que no animan los keyframes sale de `base` y no de
// static_props(clip). nullopt si los keyframes están apagados o no tienen items.
[[nodiscard]] std::optional<KfProps> clip_props_over(const TimelineClip& clip, double local_t, const KfProps& base);
[[nodiscard]] double clip_volume_at(const TimelineClip& clip, double local_t);

// Estilo de texto animado en `local_t`: solo las propiedades con keyframes.
[[nodiscard]] Json text_style_at(const TimelineClip& clip, double local_t);
[[nodiscard]] bool text_style_animates(const TimelineClip& clip);
// Estilo + valores animados; las casillas del estilo mandan (sin Fondo o sin Trazo,
// sus valores animados no hacen nada).
[[nodiscard]] Json with_text_style_kf(const Json& style, const Json& animated);

// --- Edición de keyframes ----------------------------------------------------------------

// Añade o actualiza (si hay uno a menos de kKfSnap) el snapshot en `local_t`, con
// los valores actuales y `patch` encima ({prop: valor}). Activa los keyframes.
void upsert_keyframe_at(TimelineClip& clip, double local_t, const Json& patch, std::optional<Interp> interpolation,
                        IdSource& ids);

// Fundido de volumen de entrada ("in") o salida ("out") con keyframes. `duration` es
// lo que dura el clip en la timeline.
[[nodiscard]] Result<void> apply_volume_fade(TimelineClip& clip, double duration, std::string_view side,
                                             double fade_dur, IdSource& ids);

} // namespace gc::core
