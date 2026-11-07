#pragma once

// «Seguir cara» de una máscara (followMaskFace de VideoEditor.jsx): el seguimiento de caras del material (el
// job face-track del backend, puntos {t, cx, cy} en segundos del archivo) pasado a keyframes de la posición de
// la máscara (followTrackMaskKeys de lib/clipLayout.js) y puestos en el clip.

#include <vector>

#include "gc/core/ids.hpp"
#include "gc/core/json.hpp"
#include "gc/core/model.hpp"

namespace gc::core {

struct MaskFollowKey {
    double t = 0; // tiempo local del clip
    double mx = 0;
    double my = 0;
};

// followTrackMaskKeys: un keyframe por punto del tramo del clip (con su velocidad y al revés), a `min_gap` s
// como poco del anterior, en la posición de salida (0–1) de la cara en ese instante; a 4 decimales.
[[nodiscard]] std::vector<MaskFollowKey> follow_track_mask_keys(const TimelineClip& clip, const Json& track, double src_w,
                                                            double src_h, double out_w, double out_h,
                                                            double min_gap = 0.2);

// El final de followMaskFace: keyframes encendidos (en el primero, si no lo estaban) y en cada uno, mx y my.
void apply_follow_keys(TimelineClip& clip, const std::vector<MaskFollowKey>& keys, double src_t, double fps,
                       IdSource& ids);

} // namespace gc::core
