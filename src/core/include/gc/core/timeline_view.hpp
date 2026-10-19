#pragma once

// Cómo se ve la timeline del editor (espejo de timelineScale.js, trackRows.js y displayTracks de
// trackStack.js de video-yt): zoom en píxeles por segundo, marcas de la regla, alto de las filas
// y orden de las pistas en pantalla.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "gc/core/model.hpp"

namespace gc::core {

// normalizeFps: 24, 25, 30, 50 o 60; cualquier otro, 30.
[[nodiscard]] int normalize_fps(double fps);
[[nodiscard]] double frame_duration(double fps);

inline constexpr double kPpsFloor = 0.08;
inline constexpr double kPpsShortMin = 8;

[[nodiscard]] double max_pps(double fps, double duration = 12);
[[nodiscard]] double min_pps(double duration, double view_width = 900);
// Un `pps` no válido (0 o NaN) vale el mínimo.
[[nodiscard]] double clamp_pps(double pps, double duration, double view_width, double fps);
// Zoom al arrastrar `dx` px sobre la regla: a la derecha acerca.
[[nodiscard]] double zoom_by_drag(double start_pps, double dx, double duration, double view_width,
                                  double fps);

[[nodiscard]] std::vector<double> tick_steps(double fps);
[[nodiscard]] double tick_step(double pps, double fps, bool dense = false);
// m:ss, h:mm:ss, o m:ss:ff (cuadros) cuando el paso es menor de 1 s.
[[nodiscard]] std::string fmt_ruler(double seconds, double fps, std::optional<double> step = {},
                                    bool long_form = false);

struct Tick {
    double t;
    bool major;
    double step;
};
// Marcas de la regla en lo visible (con un margen), las principales primero.
[[nodiscard]] std::vector<Tick> build_ticks(double max_t, double pps, double fps, double scroll_x,
                                            double view_width, bool dense = false);

inline constexpr int kRowHeightMin = 34;
inline constexpr int kRowHeightMax = 120;
inline constexpr int kRowHeightDefault = 52;

[[nodiscard]] int clamp_row_height(std::optional<double> raw);
// Las pistas de texto miden ~la mitad que las de vídeo y audio.
[[nodiscard]] int track_row_height(std::string_view kind, std::optional<double> row_height);
// Ctrl+rueda: hacia arriba agranda, hacia abajo encoge.
[[nodiscard]] int step_row_height(std::optional<double> row_height, double delta_y);

// Orden en pantalla: la pila de vídeo y texto al revés (lo de delante arriba) y luego el audio.
[[nodiscard]] std::vector<const TimelineTrack*> display_tracks(const std::vector<TimelineTrack>& tracks);

} // namespace gc::core
