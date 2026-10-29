#pragma once

// Un dibujo de escala y traslación (sin giro ni sesgo) con el bicúbico de Skia, pero separable:
// lo mismo que hace Canvas::draw_image con `drawImageRect` (SkCubicResampler{0, 0,6}, kStrict,
// sin antialias) sobre el lienzo RGBA premultiplicado, unas 5 veces más rápido. El bicúbico de
// Skia lee 16 texels por píxel en coma flotante (155 ms por una imagen a pantalla completa en 4K,
// con un hilo); aquí cada fila de la fuente se filtra una vez en horizontal y cada píxel solo
// combina 4 filas (29 ms; con SSE2 en x86-64, en escalar en lo demás).
//
// Lo que copia de Skia (src/core/SkBitmapDevice.cpp, SkDraw.cpp, SkScan.cpp, SkMatrix.cpp,
// src/shaders/SkImageShader.cpp y src/opts/SkRasterPipeline_opts.h de la versión de vcpkg):
//   - qué píxeles se pintan: el rectángulo del recorte llevado al cuadro (en float) y redondeado
//     con floor(v + 0,5) en doble (SkScan::FillRect sin antialias);
//   - dónde se muestrea: el centro de cada píxel por la inversa de la matriz total (la del cuadro
//     por la traslación del recorte), en float como SkMatrix::invert y con FMA como la etapa
//     `matrix_scale_translate`;
//   - de qué texels: con kStrict y un recorte menor que la imagen, Skia dibuja de la subimagen
//     `roundOut(recorte)` y acota a ella (`clamp_ex` y truncar, en `ix_and_ptr`);
//   - los pesos: los polinomios de SkImageShader::CubicResamplerMatrix(0, 0,6) en t = fract(x + 0,5)
//     (`bicubic_wts`, con FMA);
//   - y después: filtrar sin premultiplicar, `clamp_01`, premultiplicar, el alfa de la pintura,
//     la matriz de color si la hay (SkMatrixColorFilter: `unpremul`, `matrix_4x5`, `clamp_01` y
//     `premul`) y `srcover_rgba_8888` (d·(1 − a) + c·255, redondeado al par con cvtps).
// Lo que no: el orden de las sumas (Skia suma los 16 productos peso_x·peso_y·texel en cadena; aquí
// primero las filas) y las FMA de la mezcla. Sale igual salvo ±1 nivel en muy pocos píxeles
// (test_render_resample.cpp lo compara con Skia).

#include <cstddef>
#include <cstdint>
#include <optional>

#include "gc/core/clip_filters.hpp"
#include "gc/core/clip_layout.hpp"
#include "gc/core/frame.hpp"
#include "gc/render/canvas.hpp"

namespace gc::render::detail {

// Píxeles RGBA premultiplicados de destino (la capa de arriba del lienzo).
struct PremulPixels {
    std::uint8_t* pixels = nullptr;
    std::size_t row_bytes = 0;
    int width = 0;
    int height = 0;
};

// Rectángulo de píxeles [left, right) × [top, bottom).
struct PixelRect {
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
};

// Para los tests: con false, Canvas::draw_image dibuja siempre con Skia (la referencia).
void set_fast_scaled_draw(bool on) noexcept;
[[nodiscard]] bool fast_scaled_draw() noexcept;

// Dibuja el recorte `src` de `image` (RGBA sin premultiplicar, ya validado dentro de la imagen)
// llevado al cuadro por `m`, con opacidad `alpha` (0–1), la matriz de color `color` (la de
// Canvas::draw_image) y src-over, sobre `dst` y solo dentro de `clip` (en píxeles de `dst`).
// Devuelve false, sin tocar nada, si `m` gira, sesga o tiene una escala nula o no finita, o si las
// cuentas se salen de lo que Skia dibuja con este camino.
[[nodiscard]] bool draw_scaled_bicubic(const Image& image, const core::SourceCrop& src, const core::Affine& m,
                                       float alpha, const std::optional<core::AffineColorMatrix>& color,
                                       const PremulPixels& dst, const PixelRect& clip);

} // namespace gc::render::detail
