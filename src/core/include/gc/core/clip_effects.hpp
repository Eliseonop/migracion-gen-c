#pragma once

// Efectos de clip (`clip.effects`) con la semántica del export: lo que effects_ffmpeg de
// backend/app/clip_fx.py pone en la cadena de FFmpeg, con sus límites y con el redondeo de sus
// números al escribirlos (%.2f, %.0f, %.3f, %.5f), que es lo que FFmpeg lee. El render
// (gc/render/effects.hpp) los aplica con las cuentas de esos filtros sobre el clip ya escalado.
//
// El preview (effectsCss de clipFx.js) los hace con filtros CSS que no se parecen (el desenfoque,
// la nitidez como contraste, el brillo que multiplica, el VHS con otro tono y un desenfoque) y no
// hace ni el grano ni el pixelado; el «glow» solo existe en el preview. Se sigue al export
// (decisión pendiente de confirmar por Edu, notas de F3). Las cadenas de FFmpeg no se portan.

#include <optional>

#include "gc/core/clip_adjust.hpp"
#include "gc/core/json.hpp"
#include "gc/core/model.hpp"

namespace gc::core {

// eq de FFmpeg: brillo (−1…1, se suma a la luma), contraste (alrededor de 0,5) y saturación (0…3).
struct EqParams {
    double brightness = 0.0;
    double contrast = 1.0;
    double saturation = 1.0;
};

// Los efectos en el orden en que van en la cadena del export.
struct ClipEffects {
    double blur = 0.0;                 // gblur: σ en px (min(20, blur), 2 decimales); 0 = sin
    double sharpen = 0.0;              // unsharp 5×5 de la luma: cantidad (min(5, sharpen), 2 decimales)
    bool grayscale = false;            // hue=s=0
    bool sepia = false;                // colorchannelmixer de sepia
    bool vhs = false;                  // eq=contrast=1.35:saturation=0.65 y hue=h=-8
    int grain = 0;                     // noise=alls: fuerza (min(60, grain), sin decimales); 0 = sin
    int pixelate = 0;                  // lado del bloque en px (min(32, max(2, px)), sin decimales); 0 = sin
    std::optional<EqParams> eq;        // brightness, contrast y saturation (3 decimales)
    std::optional<ColorMatrix> adjust; // exposición, blancos, temperatura y tono (clip_adjust, 5 decimales)

    // ¿Cambia algo la imagen? (un grano o un eq que se quedan en 0 no la cambian)
    [[nodiscard]] bool any() const noexcept;
};

// `effects` es el dict de efectos del clip (lo que no es un dict cuenta como vacío).
[[nodiscard]] ClipEffects clip_effects(const Json& effects);
[[nodiscard]] ClipEffects clip_effects(const TimelineClip& clip);

} // namespace gc::core
