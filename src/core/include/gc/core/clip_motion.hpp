#pragma once

// Presets de animación de clip (zoom, giro, deslizar, fundido, pop, pulso, trazo):
// keyframes generados en vez de escritos a mano (espejo de backend/app/clip_motion.py).
// Los `t` son tiempo LOCAL del clip. Opcionalmente siguen una envolvente de audio
// [(t, amp 0–1)], p. ej. el RMS de un efecto de sonido.

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "gc/core/error.hpp"
#include "gc/core/ids.hpp"
#include "gc/core/json.hpp"
#include "gc/core/model.hpp"

namespace gc::core {

inline constexpr std::array<std::string_view, 13> kMotions{
    "zoom_in",     "zoom_out", "spin",    "spin_in", "slide_left", "slide_right", "slide_up",
    "slide_down",  "fade_in",  "fade_out", "pop",    "pulse",      "draw_in"};

// Propiedades que puede llevar un item generado, en este orden.
inline constexpr std::array<std::string_view, 9> kMotionKeys{"x",  "y",  "scale", "rotation", "opacity",
                                                             "cx", "cy", "zoom",  "draw"};

// Nombre del movimiento normalizado (minúsculas, «-» → «_» y alias: zoom, fade, slide,
// spin_out, whoosh, hit, draw). Error si no es uno de kMotions.
[[nodiscard]] Result<std::string> normalize_motion(const Json& motion);

// El zoom se hace con la escala (overlay, texto, figura) y no con el recorte.
[[nodiscard]] bool uses_scale_zoom(const TimelineClip& clip);

struct MotionItem {
    std::string id;
    double t = 0.0;
    std::string interpolation;
    std::array<std::optional<double>, kMotionKeys.size()> props; // por índice de kMotionKeys
};

// Items de keyframes de un preset. Los parámetros llevan la semántica de Python
// (`_num`: lo que no es un número vale el defecto): duration (s), intensity (0,15–2),
// turns (0,25–4) y envelope ([{t, amp}] o [[t, amp]]).
[[nodiscard]] Result<std::vector<MotionItem>> build_motion_items(const TimelineClip& clip, const Json& motion,
                                                                 const Json& duration, const Json& intensity,
                                                                 const Json& turns, const Json& envelope,
                                                                 IdSource& ids);

// Keyframes del clip con estos items: sustituyen los visuales y cada item lleva el resto
// de propiedades (volumen, efectos…) tal como estaban en su instante.
[[nodiscard]] Json merge_visual_keyframes(const TimelineClip& clip, const std::vector<MotionItem>& items);

} // namespace gc::core
