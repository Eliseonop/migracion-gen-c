#pragma once

// Seguimiento de objetos: del recorrido de un objeto en un vídeo a los keyframes de
// pose del clip que lo acompaña (espejo de object_track.follow_keys de video-yt).
// El seguimiento en sí (Lucas-Kanade + RANSAC) llega con la IA local en F6; aquí solo
// la matemática, que ya necesita el comando follow_object.

#include <array>
#include <optional>
#include <string_view>
#include <vector>

#include "gc/core/error.hpp"
#include "gc/core/json.hpp"
#include "gc/core/model.hpp"

namespace gc::core {

inline constexpr std::array<std::string_view, 3> kFollowModes{"position", "position_scale",
                                                               "position_scale_rotation"};

// Tamaños en px: fuente del vídeo y salida. Ninguno puede ser 0.
struct FollowDims {
    double src_w = 0.0;
    double src_h = 0.0;
    double out_w = 0.0;
    double out_h = 0.0;
};

// Keyframe de pose del clip que acompaña (t LOCAL del clip, redondeado como en Python).
struct FollowKey {
    double t = 0.0;
    double x = 0.5;
    double y = 0.5;
    std::optional<double> scale;    // modos con escala
    std::optional<double> rotation; // modo con giro
};

// Keyframes para que `follower` acompañe al objeto seguido en `video`. `track` es el
// recorrido de track_object: [{t (s de la FUENTE), cx, cy (0–1), s?, rot?}]. `box_w` es
// el ancho del recuadro inicial (0–1 de la fuente) y `anchor_t` el instante de la
// timeline en que se marcó: ahí el clip conserva su pose y desde ahí se mueve, escala y
// gira lo que el objeto en pantalla. Entre dos keyframes hay al menos `min_gap` s
// (nullopt = no es un número: error solo si llega a hacer falta, como en Python).
// Vacío si el recorrido no coincide en el tiempo con los dos clips.
[[nodiscard]] Result<std::vector<FollowKey>> follow_keys(const TimelineClip& video, const TimelineClip& follower,
                                                         const Json& track, const FollowDims& dims, double box_w,
                                                         double anchor_t, std::string_view mode,
                                                         std::optional<double> min_gap);

} // namespace gc::core
