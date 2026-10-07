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

#include <cstdint>
#include <string>
#include <string_view>

#include "gc/core/json.hpp"
#include "gc/core/model.hpp"

namespace gc::core {

// "#RRGGBB" en mayúsculas (admite "#rgb", "0x…" y sin "#"), o `fallback`.
[[nodiscard]] std::string normalize_hex(const Json& raw, const std::string& fallback = "#00FF00");

// Normalizado, o null si `raw` no es un objeto con contenido.
[[nodiscard]] Json normalize_bg(const Json& raw);

// Vídeo e imagen (incluye GIF) admiten quitar el fondo.
[[nodiscard]] bool bg_capable(const TimelineClip& clip);

// Motor automático que se propone para un clip (recommended_provider): vídeo → RVM
// (personas, sin parpadeo), GIF → U²-Net, imagen fija → BiRefNet.
[[nodiscard]] std::string recommended_bg_provider(const TimelineClip& clip);
// ¿Es uno de los motores de «Eliminar fondo» (BG_PROVIDER_IDS)?
[[nodiscard]] bool is_bg_provider(std::string_view provider);
// Alto del matte con el que rinde cada motor (512 si no tiene uno propio).
[[nodiscard]] std::int64_t bg_provider_mask_height(std::string_view provider);

} // namespace gc::core
