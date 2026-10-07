#pragma once

// «Eliminar fondo» de un clip: modelo normalizado de clip.bg_removal (espejo de la
// parte de datos de backend/app/clip_bg.py). El matte de IA y el croma los aplica
// el render/IA (F3/F6).
//
//   {enabled, mode: "auto"|"chroma",
//    auto:    {enabled, provider, model_version, base_key, status, error, mask_fps,
//              mask_height, stabilize, threshold, softness, feather, expansion,
//              opacity, invert, edits: [{op, size, points, t?, tool?}]},
//    chroma:  {enabled, color, similarity, blend, spill, edge, shrink},
//    outline: {enabled, color, width, soft, opacity}}

#include <string>

#include "gc/core/json.hpp"
#include "gc/core/model.hpp"

namespace gc::core {

// "#RRGGBB" en mayúsculas (admite "#rgb", "0x…" y sin "#"), o `fallback`.
[[nodiscard]] std::string normalize_hex(const Json& raw, const std::string& fallback = "#00FF00");

// Normalizado, o null si `raw` no es un objeto con contenido.
[[nodiscard]] Json normalize_bg(const Json& raw);

// Vídeo e imagen (incluye GIF) admiten quitar el fondo.
[[nodiscard]] bool bg_capable(const TimelineClip& clip);

} // namespace gc::core
