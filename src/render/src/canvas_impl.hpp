#pragma once

// Interior de Canvas para las demás piezas de gc_render (el texto dibuja en el mismo SkSurface).
// No sale de la biblioteca.

#include "gc/render/canvas.hpp"
#include "include/core/SkCanvas.h"
#include "include/core/SkSurface.h"

namespace gc::render {

struct Canvas::Impl {
    sk_sp<SkSurface> surface;
    int width = 0;
    int height = 0;
};

struct CanvasAccess {
    [[nodiscard]] static SkCanvas* sk(Canvas& canvas) { return canvas.impl_->surface->getCanvas(); }
};

} // namespace gc::render
