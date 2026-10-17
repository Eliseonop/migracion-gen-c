#pragma once

// Las figuras (clips `shape`) en el lienzo: la geometría y las operaciones de rasterize_shape de
// shapes.py (core::shape_plan) dibujadas con Skia, en la caja de la pose del clip (la de
// applyShapePose del preview; el export la hornea en su PNG, o lo rasteriza neutro y le aplica la
// pose animada al componer).
//
// Como el export, que trata el PNG de la figura como un still a cuadro completo: la opacidad es la
// de toda la figura (el trazo no se ve a través del relleno), los filtros de color van sobre la
// figura entera y la aparición y la salida (fundido, zoom, deslizar, cortinilla) se aplican al
// cuadro entero, con el zoom desde su centro. El preview no anima la aparición ni la salida de las
// figuras: divergencia apuntada en las notas de F3.

#include <optional>

#include "gc/core/clip_filters.hpp"
#include "gc/core/error.hpp"
#include "gc/core/frame.hpp"
#include "gc/render/canvas.hpp"

namespace gc::render {

// Dibuja la figura de `layer` (un clip `shape`) sobre el lienzo, encima de lo que haya, con
// layer.alpha (aparición × opacidad de la pose) y la matriz `color` si la hay. Error si la figura
// no se puede normalizar (un valor que no es un número).
[[nodiscard]] Result<void> draw_shape(Canvas& canvas, const core::FrameLayer& layer,
                                      const std::optional<core::AffineColorMatrix>& color = std::nullopt);

} // namespace gc::render
