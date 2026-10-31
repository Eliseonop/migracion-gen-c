#pragma once

// Marcos de plataforma (solo VISTA PREVIA), como features/editor/render/platformChrome.js de
// video-yt: la interfaz de TikTok o YouTube Shorts dibujada encima del recuadro exportable, para ver
// cómo quedará el vídeo en esas apps y qué zona tapan sus botones y textos. No se exporta.
// Vectorial: nítido a cualquier tamaño.

#include <array>
#include <string_view>

#include "gc/render/canvas.hpp"

namespace gc::render {

struct PlatformOverlay {
    std::string_view id;
    std::string_view label;
};

// PLATFORM_OVERLAYS, en el orden del selector del visor.
inline constexpr std::array<PlatformOverlay, 3> kPlatformOverlays{{
    {"none", "Sin marco"},
    {"tiktok", "TikTok"},
    {"shorts", "YouTube Shorts"},
}};

// overlayLabel: la etiqueta de un marco, o "" si no existe.
[[nodiscard]] constexpr std::string_view overlay_label(std::string_view id) noexcept {
    for (const auto& o : kPlatformOverlays) {
        if (o.id == id) {
            return o.label;
        }
    }
    return {};
}

// drawPlatformChrome: el marco `kind` ("tiktok" o "shorts") dentro del recuadro (x, y, w, h), en
// px del lienzo, recortado a él. Nada con "none", un marco desconocido o un ancho que no es > 0.
// Los textos van en la fuente del sistema (system-ui: Segoe UI en Windows).
void draw_platform_chrome(Canvas& canvas, double x, double y, double w, double h, std::string_view kind);

} // namespace gc::render
