#pragma once

// Interior de Canvas para las demás piezas de gc_render (el texto dibuja en el mismo SkSurface).
// No sale de la biblioteca.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "gc/core/clip_filters.hpp"
#include "gc/render/canvas.hpp"
#include "include/core/SkCanvas.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkColorFilter.h"
#include "include/core/SkImage.h"
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
    // Lo dibujado hasta ahora, como imagen de Skia (premultiplicada).
    [[nodiscard]] static sk_sp<SkImage> image(Canvas& canvas) { return canvas.impl_->surface->makeImageSnapshot(); }

    // Todos los píxeles del lienzo (RGBA premultiplicado, filas de width · 4 bytes) a `out` y de
    // vuelta: lo que guarda LayerCache. Solo sin capas abiertas (begin_layer); si no, false.
    [[nodiscard]] static bool save_pixels(Canvas& canvas, std::vector<std::uint8_t>& out) {
        SkPixmap pm;
        if (canvas.impl_->surface->getCanvas()->getSaveCount() != 1 || !canvas.impl_->surface->peekPixels(&pm) ||
            pm.rowBytes() != static_cast<std::size_t>(canvas.impl_->width) * 4) {
            return false;
        }
        const auto* p = static_cast<const std::uint8_t*>(pm.addr());
        out.assign(p, p + pm.computeByteSize());
        return true;
    }
    [[nodiscard]] static bool restore_pixels(Canvas& canvas, const std::vector<std::uint8_t>& in) {
        SkSurface* surface = canvas.impl_->surface.get();
        if (surface->getCanvas()->getSaveCount() != 1) {
            return false;
        }
        // Se sustituye todo: si hay una instantánea, Skia no necesita copiar lo de antes.
        surface->notifyContentWillChange(SkSurface::kDiscard_ContentChangeMode);
        SkPixmap pm;
        if (!surface->peekPixels(&pm) || pm.computeByteSize() != in.size()) {
            return false;
        }
        std::ranges::copy(in, static_cast<std::uint8_t*>(pm.writable_addr()));
        return true;
    }
};

} // namespace gc::render
