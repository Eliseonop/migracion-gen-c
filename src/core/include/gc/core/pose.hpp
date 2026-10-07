#pragma once

// Pose de un clip y encuadre en un instante
// (espejo de backend/app/clip_anim.py y reframe_math.py).
//
//   clip_pose(clip, t) → {x, y, scale, rotation, opacity}: de los keyframes si
//                        están activos; si no, de las pistas `anim` (legado) o
//                        de los valores estáticos.
//   frame_at(keyframes, t) → centro (cx, cy), zoom y ajuste del encuadre (reframe).

#include <string>
#include <vector>

#include "gc/core/json.hpp"
#include "gc/core/model.hpp"

namespace gc::core {

struct Pose {
    double x = 0.5;
    double y = 0.5;
    double scale = 1.0;
    double rotation = 0.0;
    double opacity = 1.0;
};

// Interpolación de una pista legada [{t, v, ease: "direct"|"smooth"}].
[[nodiscard]] double interp_track(const Json& track, double t, double fallback);

[[nodiscard]] Pose static_pose(const TimelineClip& clip);
[[nodiscard]] Pose clip_pose(const TimelineClip& clip, double local_t);

struct Frame {
    double cx = 0.5;
    double cy = 0.5;
    double zoom = 1.0;
    std::string pan_mode = "smooth"; // "smooth" | "direct"
    std::string fit = "cover";       // "cover" | "contain"
};

// Encuadre en `time` (s de la FUENTE) entre los keyframes de un reframe.
[[nodiscard]] Frame frame_at(const std::vector<Keyframe>& keyframes, double time, double fallback_zoom = 1.0,
                             const std::string& fallback_mode = "smooth");

} // namespace gc::core
