#include "gc/render/canvas.hpp"

#include <cstddef>
#include <format>
#include <utility>

#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPaint.h"
#include "include/core/SkRect.h"
#include "include/core/SkSurface.h"

namespace gc::render {

struct Canvas::Impl {
    sk_sp<SkSurface> surface;
    int width = 0;
    int height = 0;
};

namespace {

SkColor to_sk(Rgba8 c) { return SkColorSetARGB(c.a, c.r, c.g, c.b); }

} // namespace

Result<Canvas> Canvas::make(int width, int height) {
    if (width <= 0 || height <= 0) {
        return fail(ErrorCode::InvalidArgument, std::format("lienzo de {}x{}: las dimensiones deben ser mayores que 0",
                                                            width, height));
    }
    // Premultiplicado por dentro (lo que Skia dibuja más rápido); snapshot() lo devuelve sin
    // premultiplicar.
    auto surface = SkSurfaces::Raster(SkImageInfo::Make(width, height, kRGBA_8888_SkColorType, kPremul_SkAlphaType));
    if (!surface) {
        return fail(ErrorCode::Internal, std::format("Skia no pudo crear un lienzo de {}x{}", width, height));
    }
    auto impl = std::make_unique<Impl>();
    impl->surface = std::move(surface);
    impl->width = width;
    impl->height = height;
    return Canvas(std::move(impl));
}

Canvas::Canvas(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Canvas::Canvas(Canvas&&) noexcept = default;
Canvas& Canvas::operator=(Canvas&&) noexcept = default;
Canvas::~Canvas() = default;

int Canvas::width() const noexcept { return impl_->width; }
int Canvas::height() const noexcept { return impl_->height; }

void Canvas::clear(Rgba8 color) { impl_->surface->getCanvas()->clear(to_sk(color)); }

void Canvas::fill_rect(double x, double y, double w, double h, Rgba8 color) {
    SkPaint paint(SkColor4f::FromColor(to_sk(color)));
    paint.setAntiAlias(false);
    impl_->surface->getCanvas()->drawRect(
        SkRect::MakeXYWH(static_cast<float>(x), static_cast<float>(y), static_cast<float>(w), static_cast<float>(h)),
        paint);
}

Image Canvas::snapshot() const {
    Image img{impl_->width, impl_->height, {}};
    const auto row = static_cast<std::size_t>(impl_->width) * 4;
    img.pixels.resize(row * static_cast<std::size_t>(impl_->height));
    const auto info = SkImageInfo::Make(impl_->width, impl_->height, kRGBA_8888_SkColorType, kUnpremul_SkAlphaType);
    // Mismas dimensiones que el lienzo y formato soportado: no puede fallar.
    [[maybe_unused]] const bool ok = impl_->surface->readPixels(info, img.pixels.data(), row, 0, 0);
    return img;
}

} // namespace gc::render
