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
#include <vector>

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
// --- Croma (las cadenas de FFmpeg que usa el export) ---------------------------------------
// Las funciones reciben el croma ya normalizado (normalize_bg(…)["chroma"]).

// chroma_active / auto_active de clip_bg.py, sobre el bg normalizado (null = nada).
[[nodiscard]] bool bg_chroma_active(const Json& bg);
[[nodiscard]] bool bg_auto_active(const Json& bg);
// chroma_filters: `format=yuva444p`, `chromakey=color=0xRRGGBB:similarity=…:blend=…` y, con
// derrame, `despill=…`; vacío si el croma está apagado.
[[nodiscard]] std::vector<std::string> chroma_filters(const Json& chroma);
// chroma_alpha_ffmpeg: filtros sobre el alfa (gris) que limpian o expanden el borde
// (`gblur` y `lutyuv`), con el desenfoque en px de un material de `height` de alto; "" si nada.
[[nodiscard]] std::string chroma_alpha_ffmpeg(const Json& chroma, std::int64_t height);
// La parte del croma de _bg_source_chain (compose.py) sin matte: la cadena del croma o, si hay
// que procesar el alfa, el subgrafo que lo separa (alphaextract), lo procesa y lo vuelve a unir
// (alphamerge). "" si el croma está apagado.
[[nodiscard]] std::string chroma_source_chain(const Json& chroma, std::int64_t height);

// ¿Es uno de los motores de «Eliminar fondo» (BG_PROVIDER_IDS)?
[[nodiscard]] bool is_bg_provider(std::string_view provider);
// Alto del matte con el que rinde cada motor (512 si no tiene uno propio).
[[nodiscard]] std::int64_t bg_provider_mask_height(std::string_view provider);

} // namespace gc::core
