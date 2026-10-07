#pragma once

// Ajustes de color del clip (Exposición, Blancos, Temperatura, Tono) y máscara de
// ajuste (espejo de backend/app/clip_adjust.py y frontend/src/lib/clipAdjust.js).
//
// Los cuatro ajustes viven en `clip.effects` (junto a brillo, contraste y saturación)
// y se reducen a UNA matriz 3×3 sobre RGB en gamma (sRGB), sin términos constantes:
//
//     M = Tono · diag(temperatura) · k        k = exposición · blancos
//
// Máscara de ajuste: una máscara con `target == "adjust"` limita los ajustes de color
// del clip a su interior. Se resuelve dibujando el clip en dos pasadas: la base sin
// ajustes de color y encima una copia «fantasma» con los ajustes, recortada por esas
// máscaras. (`colorchannelmixer`, la versión para FFmpeg, no se porta.)

#include <array>
#include <optional>
#include <string_view>
#include <vector>

#include "gc/core/cow.hpp"
#include "gc/core/json.hpp"
#include "gc/core/model.hpp"

namespace gc::core {

inline constexpr std::array<std::string_view, 4> kAdjustKeys{"exposure", "whites", "temperature", "hue"};
// Todo lo que limita la máscara de ajuste: los ajustes de color del clip.
inline constexpr std::array<std::string_view, 7> kColorKeys{"brightness", "contrast", "saturation", "exposure",
                                                            "whites",     "temperature", "hue"};
inline constexpr std::string_view kGhostSuffix = "__adj";

// Valor de un ajuste acotado a su rango (exposure, whites y temperature en −1…1; hue
// en −180…180). Lo que no es un número finito cuenta como 0.
[[nodiscard]] double adjust_value(const Json& effects, std::string_view key);

using ColorMatrix = std::array<std::array<double, 3>, 3>;

// Matriz de los ajustes, o nullopt si no hay ninguno activo. `effects` es el dict de
// efectos del clip (lo que no es un dict cuenta como vacío).
[[nodiscard]] std::optional<ColorMatrix> color_matrix(const Json& effects);
[[nodiscard]] std::optional<ColorMatrix> color_matrix(const TimelineClip& clip);

// ¿Algún ajuste de color (también brillo, contraste o saturación) distinto de 0?
[[nodiscard]] bool has_color_adjust(const Json& effects);
[[nodiscard]] bool has_color_adjust(const TimelineClip& clip);

// Los efectos sin los ajustes de color.
[[nodiscard]] Json strip_color(const Json& effects);

struct ClipPass {
    Cow<TimelineClip> clip;
    bool ghost = false;
};

// Pasadas de dibujo de un clip. Con máscara de ajuste y algún ajuste de color (en
// vídeo, imagen o figura): la base sin ajustes y el fantasma («<id>__adj», mudo) con
// los ajustes y todas sus máscaras como máscaras de recorte. Si no, el clip tal cual.
[[nodiscard]] std::vector<ClipPass> adjust_passes(const Cow<TimelineClip>& clip);

} // namespace gc::core
