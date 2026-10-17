#include "gc/render/canvas.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <format>
#include <utility>

#include "include/core/SkBlendMode.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkColorFilter.h"
#include "include/core/SkImage.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkMatrix.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkPixmap.h"
#include "include/core/SkRect.h"
#include "include/core/SkSamplingOptions.h"
#include "include/core/SkSurface.h"

#include "canvas_impl.hpp"

namespace gc::render {

namespace {

SkColor to_sk(Rgba8 c) { return SkColorSetARGB(c.a, c.r, c.g, c.b); }

// La matriz afín 3×4 de los filtros como la 4×5 de Skia (filas R, G, B y A; el desplazamiento,
// en 0–1). Skia la aplica sin premultiplicar y acota el resultado a 0–1. El alfa no cambia.
sk_sp<SkColorFilter> color_filter(const core::AffineColorMatrix& m) {
    std::array<float, 20> row_major{};
    for (std::size_t r = 0; r < 3; ++r) {
        for (std::size_t c = 0; c < 3; ++c) {
            row_major[(r * 5) + c] = static_cast<float>(m[r][c]);
        }
        row_major[(r * 5) + 4] = static_cast<float>(m[r][3]);
    }
    row_major[18] = 1.0F; // A' = A
    return SkColorFilters::Matrix(row_major.data());
}

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

Result<void> Canvas::draw_image(const Image& image, const core::SourceCrop& src, const core::Affine& m, double alpha,
                                const std::optional<std::array<core::Point, 4>>& clip,
                                const std::optional<core::AffineColorMatrix>& color) {
    const auto row = static_cast<std::size_t>(image.width) * 4;
    if (image.width <= 0 || image.height <= 0 || image.pixels.size() != row * static_cast<std::size_t>(image.height)) {
        return fail(ErrorCode::InvalidArgument,
                    std::format("imagen de {}x{} con {} bytes", image.width, image.height, image.pixels.size()));
    }
    // source_crop_px ya lo mete dentro de la imagen; el margen cubre el redondeo de sus cuentas.
    constexpr double kSlack = 1e-6;
    const bool inside = std::isfinite(src.sx) && std::isfinite(src.sy) && std::isfinite(src.sw) &&
                        std::isfinite(src.sh) && src.sw > 0 && src.sh > 0 && src.sx >= -kSlack && src.sy >= -kSlack &&
                        src.sx + src.sw <= image.width + kSlack && src.sy + src.sh <= image.height + kSlack;
    if (!inside) {
        return fail(ErrorCode::InvalidArgument,
                    std::format("recorte ({}, {}, {}, {}) fuera de una imagen de {}x{}", src.sx, src.sy, src.sw, src.sh,
                                image.width, image.height));
    }
    const SkPixmap pixmap(SkImageInfo::Make(image.width, image.height, kRGBA_8888_SkColorType, kUnpremul_SkAlphaType),
                          image.pixels.data(), row);
    // Sin copiar los píxeles: el lienzo raster dibuja al momento y la imagen no sale de aquí.
    const sk_sp<SkImage> sk_image = SkImages::RasterFromPixmap(pixmap, nullptr, nullptr);
    if (!sk_image) {
        return fail(ErrorCode::Internal, "Skia no pudo leer la imagen");
    }

    SkCanvas* canvas = impl_->surface->getCanvas();
    canvas->save();
    if (clip) {
        SkPathBuilder path;
        path.moveTo(static_cast<float>((*clip)[0].x), static_cast<float>((*clip)[0].y));
        for (std::size_t i = 1; i < clip->size(); ++i) {
            path.lineTo(static_cast<float>((*clip)[i].x), static_cast<float>((*clip)[i].y));
        }
        path.close();
        canvas->clipPath(path.detach(), SkClipOp::kIntersect, false);
    }
    // Affine es la del contexto 2D (x' = a·x + c·y + e); SkMatrix va por filas.
    canvas->concat(SkMatrix::MakeAll(static_cast<float>(m.a), static_cast<float>(m.c), static_cast<float>(m.e),
                                     static_cast<float>(m.b), static_cast<float>(m.d), static_cast<float>(m.f), 0.0F,
                                     0.0F, 1.0F));
    SkPaint paint;
    // Un alfa que no es un número no cambia nada, como `globalAlpha = NaN` en el contexto 2D.
    paint.setAlphaf(static_cast<float>(std::isnan(alpha) ? 1.0 : std::clamp(alpha, 0.0, 1.0)));
    if (color) {
        paint.setColorFilter(color_filter(*color));
    }
    const SkRect rect = SkRect::MakeXYWH(static_cast<float>(src.sx), static_cast<float>(src.sy),
                                         static_cast<float>(src.sw), static_cast<float>(src.sh));
    // Bicúbico de swscale (libswscale/utils.c: B = 0 y C = 0,6 por defecto). Strict: no muestrea
    // fuera del recorte.
    canvas->drawImageRect(sk_image, rect, rect, SkSamplingOptions(SkCubicResampler{0.0F, 0.6F}), &paint,
                          SkCanvas::kStrict_SrcRectConstraint);
    canvas->restore();
    return {};
}

namespace {

// Los modos de core::kBlendModes en Skia (sus fórmulas son las W3C de Compositing and Blending).
std::optional<SkBlendMode> sk_blend(std::string_view mode) {
    static constexpr std::array<std::pair<std::string_view, SkBlendMode>, 11> kModes{{
        {"darken", SkBlendMode::kDarken},
        {"multiply", SkBlendMode::kMultiply},
        {"color_burn", SkBlendMode::kColorBurn},
        {"lighten", SkBlendMode::kLighten},
        {"screen", SkBlendMode::kScreen},
        {"color_dodge", SkBlendMode::kColorDodge},
        {"overlay", SkBlendMode::kOverlay},
        {"soft_light", SkBlendMode::kSoftLight},
        {"hard_light", SkBlendMode::kHardLight},
        {"difference", SkBlendMode::kDifference},
        {"exclusion", SkBlendMode::kExclusion},
    }};
    for (const auto& [name, sk] : kModes) {
        if (name == mode) {
            return sk;
        }
    }
    return std::nullopt;
}

} // namespace

void Canvas::begin_layer(std::string_view blend) {
    SkCanvas* canvas = impl_->surface->getCanvas();
    if (const auto mode = sk_blend(blend)) {
        SkPaint paint;
        paint.setBlendMode(*mode);
        canvas->saveLayer(nullptr, &paint);
    } else {
        canvas->saveLayer(nullptr, nullptr);
    }
}

void Canvas::end_layer() { impl_->surface->getCanvas()->restore(); }

Result<void> Canvas::end_layer(const MaskAlpha& mask) {
    SkCanvas* canvas = impl_->surface->getCanvas();
    const auto row = static_cast<std::size_t>(mask.width);
    if (mask.width != impl_->width || mask.height != impl_->height ||
        mask.alpha.size() != row * static_cast<std::size_t>(mask.height)) {
        // Sin la máscara no se dibuja nada de la capa: se vacía antes de cerrarla.
        canvas->clear(SK_ColorTRANSPARENT);
        canvas->restore();
        return fail(ErrorCode::InvalidArgument, std::format("máscara de {}x{} para un lienzo de {}x{}", mask.width,
                                                            mask.height, impl_->width, impl_->height));
    }
    const SkPixmap pixmap(SkImageInfo::MakeA8(mask.width, mask.height), mask.alpha.data(), row);
    SkPaint paint;
    paint.setBlendMode(SkBlendMode::kDstIn);
    canvas->drawImage(SkImages::RasterFromPixmapCopy(pixmap), 0, 0, SkSamplingOptions(), &paint);
    canvas->restore();
    return {};
}

void Canvas::apply_color_matrix(const core::AffineColorMatrix& m) {
    SkSurface* surface = impl_->surface.get();
    const sk_sp<SkImage> below = surface->makeImageSnapshot();
    SkPaint paint;
    paint.setBlendMode(SkBlendMode::kSrc);
    paint.setColorFilter(color_filter(m));
    surface->getCanvas()->drawImage(below, 0, 0, SkSamplingOptions(), &paint);
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

std::span<const std::uint8_t> Canvas::opaque_pixels() const {
    SkPixmap pm;
    // Un lienzo raster de make() siempre se deja ver, con filas de width · 4 bytes.
    if (!impl_->surface->peekPixels(&pm) || pm.rowBytes() != static_cast<std::size_t>(impl_->width) * 4) {
        return {};
    }
    return {static_cast<const std::uint8_t*>(pm.addr()), pm.computeByteSize()};
}

} // namespace gc::render
