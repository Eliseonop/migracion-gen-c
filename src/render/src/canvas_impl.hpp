#pragma once

// Interior de Canvas para las demás piezas de gc_render (el texto dibuja en el mismo SkSurface).
// No sale de la biblioteca.

#include "gc/core/clip_filters.hpp"
#include "gc/render/canvas.hpp"
#include "include/core/SkCanvas.h"
#include "include/core/SkColorFilter.h"
#include "include/core/SkSurface.h"

namespace gc::render {

struct Canvas::Impl {
    sk_sp<SkSurface> surface;
    int width = 0;
    int height = 0;
};

// La matriz afín 3×4 de los filtros como la 4×5 de Skia (filas R, G, B y A; el desplazamiento,
// en 0–1). Skia la aplica sin premultiplicar y acota el resultado a 0–1. El alfa no cambia.
[[nodiscard]] sk_sp<SkColorFilter> color_filter(const core::AffineColorMatrix& m);

struct CanvasAccess {
    [[nodiscard]] static SkCanvas* sk(Canvas& canvas) { return canvas.impl_->surface->getCanvas(); }
};

} // namespace gc::render
