#pragma once

// Aparición y salida del clip (appear/exit) y su look, en cada instante
// (espejo de clip_fx.clip_fx_at de backend/app/clip_fx.py y de clipFxAt de
// frontend/src/lib/clipFx.js).
//
// `local_t` es el tiempo desde el inicio del clip en la timeline (0 = primer
// fotograma) y `duration`, su duración en la timeline. tx/ty son fracción del destino
// (+x derecha, +y abajo); scale multiplica el destino; wipe es la fracción de ancho
// que se ve (de izquierda a derecha). Las cadenas de FFmpeg (effects_ffmpeg,
// video_fx_chain) no se portan: el render las sustituye.

#include <string>
#include <utility>

#include "gc/core/model.hpp"

namespace gc::core {

inline constexpr double kFxDur = 0.4;     // ventana de aparición y de salida
inline constexpr double kFxZoomFrom = 1.18;
inline constexpr double kFxPopFrom = 0.72;

struct ClipFx {
    double opacity = 1.0;
    double scale = 1.0;
    double tx = 0.0;
    double ty = 0.0;
    std::string look = "none";
    double wipe = 1.0;
};

// Ventanas de aparición y salida: 0,4 s o la mitad del clip si es más corto.
[[nodiscard]] std::pair<double, double> fx_windows(double duration);

[[nodiscard]] ClipFx clip_fx_at(const TimelineClip& clip, double local_t, double duration);

} // namespace gc::core
