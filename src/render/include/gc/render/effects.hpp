#pragma once

// Efectos de clip (core::ClipEffects) sobre la imagen del clip ya escalada a su destino, con las
// cuentas de los filtros de FFmpeg que pone el export (effects_ffmpeg de clip_fx.py, FFmpeg 9):
//
//   desenfoque  gblur: el filtro recursivo de Alvarez–Mazorra (un paso, en float), por filas y
//               después por columnas, con el borde extendido; también el alfa.
//   nitidez     unsharp 5×5 en la luma: la imagen menos su media binomial [1 4 6 4 1]², por la
//               cantidad (en enteros, como allí).
//   b/n, VHS,   hue y eq, en YUV: el export los recibe en gbrap y FFmpeg mete una conversión a
//   eq          yuva444p (BT.601, rango limitado), así que aquí también. Sepia y los ajustes de
//               color (colorchannelmixer) vuelven a RGB.
//   grano       noise=alls=N:allf=t: ruido gaussiano de desviación N/√3 (truncado a enteros) en
//               cada canal de color, de una tabla de 5120 valores con un desplazamiento al azar
//               por fila que cambia en cada fotograma. Con otra semilla que FFmpeg (no es el mismo
//               grano) y sin tocar el alfa (el export también le echa ruido).
//   pixelado    escala con vecino más próximo a tamaño / n y de vuelta (el muestreo de swscale).
//               Vuelve al tamaño del clip: el export, en overlay, lo estira al cuadro entero
//               (un fallo suyo; notas de F3).
//
// Cada filtro trabaja en el espacio (RGB o YUV) en que le llega la imagen, como en la cadena
// de FFmpeg: el desenfoque, el grano y el pixelado no lo cambian.

#include <cstdint>

#include "gc/core/clip_effects.hpp"
#include "gc/render/canvas.hpp"

namespace gc::render {

// `image`: RGBA sin premultiplicar. `seed` elige el grano del fotograma (mismo `seed`, mismo
// grano). No hace nada si la imagen está vacía o `fx` no cambia nada.
void apply_effects(Image& image, const core::ClipEffects& fx, std::uint64_t seed);

} // namespace gc::render
