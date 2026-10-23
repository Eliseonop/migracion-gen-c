#pragma once

// Efectos de vídeo animados (`clip.vfx`), espejo de core/runtime.js y core/gl.js del runtime de
// efectos de video-yt (frontend/src/fx), el mismo que el export hornea en Chromium (fx_bake.py).
//
// Cada efecto de motor «shader» es un GLSL `vec4 effect(vec2 uv)` sobre el preludio común; aquí se
// traduce a SkSL (Skia por CPU) sin tocar el código del efecto: el preludio se convierte de forma
// mecánica (la textura pasa a ser un `shader` hijo con muestreo bilineal y borde extendido, como
// el LINEAR + CLAMP_TO_EDGE de WebGL) y `main` hace lo de `MAIN` de gl.js. Los efectos de motor
// «canvas» (el papel) y los generadores aún no están (Unsupported).
//
// El GLSL sale de tools/gen/gen_vfx_shaders.mjs (src/render/src/vfx_shaders.inc) y los
// parámetros, del catálogo activo (gc::core::fx_catalog()).

#include <array>
#include <optional>
#include <string_view>

#include "gc/core/error.hpp"
#include "gc/core/json.hpp"
#include "gc/render/canvas.hpp"

namespace gc::render {

// vfxLayout: el lienzo de salida para un contenido de w×h px de la fuente con un margen de
// `pad_frac` del lado menor y el lado mayor limitado a `max_side` (k = px de salida por px de
// fuente, ≤ 1).
struct VfxLayout {
    double k = 1.0;
    int cw0 = 0;
    int ch0 = 0;
    int pad = 0; // P: margen en px de salida a cada lado
    int cw = 0;
    int ch = 0;
    std::array<double, 4> content{}; // el contenido dentro del lienzo (uv): x0, y0, x1, y1
    double pad_src = 0.0;            // el margen en px de la FUENTE
};
[[nodiscard]] VfxLayout vfx_layout(int w, int h, double pad_frac, double max_side);

// vfxPadFrac: el margen de toda la pila (suma de los `pad` de sus efectos, como mucho 0,5).
[[nodiscard]] double vfx_pad_frac(const Json& entries);

// clipVfx: las entradas aplicables de `clip.vfx` (activas, de un efecto conocido y que no es un
// generador), normalizadas: {effect, v, params, from, to}.
[[nodiscard]] Json clip_vfx(const Json& vfx);

struct VfxFrame {
    Image image; // RGBA sin premultiplicar, cw × ch
    VfxLayout layout;
};

// renderVfx con lienzo de destino (el export): aplica a `source` (el contenido, w×h px) los
// efectos de `entries` (las de clip_vfx) activos en `t` (s locales del clip de `clip_dur` s),
// uno tras otro. Sin ninguno activo, la fuente tal cual en el lienzo (con su margen). El margen
// solo se reserva con `pad_allowed`. Error si un efecto no es de motor «shader» o su SkSL no
// compila, o si la fuente no es de w×h.
[[nodiscard]] Result<VfxFrame> render_vfx(const Image& source, const Json& entries, double t, double clip_dur,
                                          bool pad_allowed, double max_side = 1e300);

// Para el test del catálogo: el SkSL de un efecto, o error si no es de motor «shader».
[[nodiscard]] Result<std::string> vfx_sksl(std::string_view effect_id);

} // namespace gc::render
