#pragma once

// Qué clips se dibujan en un instante y en qué orden, de fondo a frente (espejo de
// layersAt/videosAt de frontend/src/lib/clipLayout.js, con las reglas del export de
// compose.build_command donde deciden el resultado).
//
// Se dibujan vídeo, imagen, figura, motion, capa de ajuste y texto, salvo los
// desactivados o los que están en una pista oculta o inexistente. El orden es el de la
// pila de pistas vídeo + texto (stack_layers) y, dentro de una capa, el de la lista.
//
// Dos juegos de reglas (ver las notas de F3 en docs/TODO.md):
//   kRenderLayers   el del render C++ (preview y export): visible si start ≤ t < fin,
//                   con start = max(0, start) como el export, y dentro de una capa el
//                   clip que empieza más tarde va delante (como el export), con el
//                   índice en la lista como desempate.
//   kPreviewLayers  el layersAt literal del preview de video-yt: ya se ve 0,02 s antes
//                   de su inicio (margen por el seek de <video>) y dentro de una capa
//                   solo cuenta el índice en la lista.
// El fin es siempre start + clip_timeline_duration (redondeada a ms, como las ops y el
// export); el clipEnd de JS no redondea y trata las figuras como velocidad 1.

#include <vector>

#include "gc/core/cow.hpp"
#include "gc/core/model.hpp"

namespace gc::core {

struct LayerRules {
    double seek_margin = 0.0; // s antes del inicio en que el clip ya se ve
    bool by_start = true;     // export: start ≥ 0 y desempate por start dentro de la capa
};

inline constexpr LayerRules kRenderLayers{0.0, true};
inline constexpr LayerRules kPreviewLayers{0.02, false};

// ¿Es un clip que se dibuja? (vídeo, imagen, figura, motion, ajuste o texto)
[[nodiscard]] bool paints(const TimelineClip& clip);

// Clips que se dibujan en `head` (s de la timeline), de fondo a frente.
[[nodiscard]] std::vector<Cow<TimelineClip>> layers_at(const Timeline& timeline, double head,
                                                       const LayerRules& rules = kRenderLayers);
// Lo mismo sin los textos.
[[nodiscard]] std::vector<Cow<TimelineClip>> videos_at(const Timeline& timeline, double head,
                                                       const LayerRules& rules = kRenderLayers);

} // namespace gc::core
