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

// --- Envolvente de un sonido (lo que usa la herramienta MCP `animate_clip` con follow_audio_id) ---

using Envelope = std::vector<std::pair<double, double>>; // [(t, amp)]

// Como mucho `max_n` puntos repartidos por índice (`round(i·(n−1)/(max_n−1))`, al par).
// Con max_n = 1 y más de un punto Python divide entre 0: error.
[[nodiscard]] Result<Envelope> downsample_envelope(const Envelope& points, int max_n = 24);

// Amplitudes entre el pico (o 1 si el pico es 0) con un suelo de 0,06, en [0, 1].
[[nodiscard]] Envelope normalize_amps(const Envelope& points);

// Ventana de timeline [inicio, fin) en la que los dos clips coinciden; nada si dura 0,02 s o menos.
[[nodiscard]] std::optional<std::pair<double, double>> overlap_window(const TimelineClip& visual,
                                                                      const TimelineClip& audio);

// Segundo de la fuente del audio que suena en el instante `t_tl` de la timeline.
[[nodiscard]] double audio_src_at(const TimelineClip& audio, double t_tl);

// Núcleo de `sample_rms_envelope`: RMS a ~`rate` Hz de las muestras mono a 400 Hz (las que
// devuelve ffmpeg con `-ac 1 -ar 400 -f f32le`), con `t` relativo al inicio de la ventana,
// normalizado y reducido a 24 puntos. Con menos de 2 muestras, silencio de 0 a `duration`.
inline constexpr int kRmsSampleRate = 400;
[[nodiscard]] Envelope rms_envelope(const std::vector<float>& samples, double duration, int rate = 20);

} // namespace gc::core
