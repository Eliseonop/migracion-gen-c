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
//   kRenderLayers   el del render C++ (preview y export), con las reglas del export
//                   (compose.build_command) donde deciden el resultado: visible si
//                   start ≤ t < fin con start = max(0, start); solo clips que encajan en
//                   su pista (clip_fits_track) y de más de 0,02 s; y dentro de una capa
//                   va delante el clip que empieza después, con el índice en la lista
//                   como desempate. Los textos se ordenan por la pila como el resto (el
//                   export los pone en un .ass por orden de la lista: es cosa de libass).
//   kPreviewLayers  el layersAt literal del preview de video-yt: ya se ve 0,02 s antes
//                   de su inicio (margen por el seek de <video>), no mira si el clip
//                   encaja en su pista (en una de audio va al fondo, capa −1) y dentro
//                   de una capa solo cuenta el índice en la lista.
// El fin es siempre start + clip_timeline_duration (redondeada a ms, como las ops y el
// export); el clipEnd de JS no redondea y trata las figuras como velocidad 1.

#include <vector>

#include "gc/core/cow.hpp"
#include "gc/core/model.hpp"

namespace gc::core {

struct LayerRules {
    double seek_margin = 0.0; // s antes del inicio en que el clip ya se ve
    bool export_rules = true; // start ≥ 0, desempate por start, encaje en la pista y > 0,02 s
};

// El export no dibuja clips de esta duración o menos.
inline constexpr double kMinExportClipDur = 0.02;

inline constexpr LayerRules kRenderLayers{0.0, true};
inline constexpr LayerRules kPreviewLayers{0.02, false};

// ¿Es un clip que se dibuja? (vídeo, imagen, figura, motion, ajuste o texto)
[[nodiscard]] bool paints(const TimelineClip& clip);

// Clips que se dibujan en `head` (s de la timeline), de fondo a frente.
[[nodiscard]] std::vector<Cow<TimelineClip>> layers_at(const Timeline& timeline, double head,
                                                       LayerRules rules = kRenderLayers);
// Lo mismo sin los textos.
[[nodiscard]] std::vector<Cow<TimelineClip>> videos_at(const Timeline& timeline, double head,
                                                       LayerRules rules = kRenderLayers);

} // namespace gc::core
