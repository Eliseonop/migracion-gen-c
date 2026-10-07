#pragma once

// Máscaras de clip: modelo normalizado y máscaras en cada instante
// (espejo de la parte de datos de backend/app/clip_mask.py).
//
// Coordenadas independientes de la resolución: x, y = centro en fracción del
// ancho/alto de salida; w, h y feather en unidades de ALTO de salida (un círculo
// sigue redondo en 9:16, 16:9 o 1:1); rotation en grados.
// `target`: "clip" (qué parte del clip se ve) o "adjust" (dónde actúan los ajustes
// de color). El rasterizado del alfa es cosa del render (gc_render, F3).

#include <array>
#include <string_view>
#include <vector>

#include "gc/core/json.hpp"
#include "gc/core/model.hpp"

namespace gc::core {

inline constexpr std::array<std::string_view, 8> kMaskTypes{"linear", "film", "circle", "rectangle",
                                                             "star",   "heart", "text",  "brush"};
inline constexpr double kMaskFeatherMax = 0.25;

// Máscara con todos sus campos, valores acotados y defectos (normalize_mask).
[[nodiscard]] Json normalize_mask(const Json& raw);

// Máscaras normalizadas del clip (las que no son objeto se ignoran).
[[nodiscard]] std::vector<Json> clip_masks(const TimelineClip& clip);
// Vídeo, imagen, figura o texto.
[[nodiscard]] bool maskable(const TimelineClip& clip);
// ¿Alguna máscara activa que recorte el CLIP? (las de ajuste no cuentan)
[[nodiscard]] bool has_mask(const TimelineClip& clip);
[[nodiscard]] bool has_adjust_mask(const TimelineClip& clip);

// Máscaras activas en `local_t`: la primera con sus keyframes (mx, my, mw…)
// aplicados. Por defecto solo las de recorte; con include_adjust también las de ajuste.
[[nodiscard]] std::vector<Json> clip_masks_at(const TimelineClip& clip, double local_t,
                                              bool include_adjust = false);

// ¿Cambia algún parámetro animable de la máscara a lo largo del clip?
[[nodiscard]] bool mask_animates(const TimelineClip& clip, double duration, double eps = 1e-3);

} // namespace gc::core
