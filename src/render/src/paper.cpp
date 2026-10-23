#include "paper.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "gc/core/io.hpp"
#include "include/codec/SkCodec.h"
#include "include/codec/SkWebpDecoder.h"
#include "include/core/SkColorFilter.h"
#include "include/core/SkData.h"
#include "include/core/SkImageFilter.h"
#include "include/core/SkPaint.h"
#include "include/core/SkSamplingOptions.h"
#include "include/core/SkSurface.h"
#include "include/effects/SkImageFilters.h"
#include "include/effects/SkPerlinNoiseShader.h"

namespace gc::render::detail {

namespace {

constexpr std::array<int, 4> kTornSeeds{20, 30, 40, 10};
constexpr int kFoldFrames = 6;
constexpr double kFoldDur = 1.0; // s que dura abrir o cerrar
constexpr double kRef = 1080.0;  // las medidas del borde son «px a 1080», como en Paper

// La textura (relativa a la raíz pública), decodificada una vez.
Result<sk_sp<SkImage>> texture(const std::filesystem::path& assets, const std::string& name) {
    static std::mutex mutex;
    static std::map<std::string, sk_sp<SkImage>> cache;
    const std::filesystem::path path = assets / "paper-animator" / "assets" / "texture" / name;
    const std::string key = core::path_to_utf8(path);
    const std::scoped_lock lock(mutex);
    if (const auto it = cache.find(key); it != cache.end()) {
        return it->second;
    }
    auto data = SkData::MakeFromFileName(key.c_str());
    if (!data) {
        return fail(ErrorCode::NotFound, std::format("falta la textura del papel {}", key));
    }
    SkCodec::Result res = SkCodec::kSuccess;
    auto codec = SkWebpDecoder::Decode(data, &res);
    if (!codec) {
        return fail(ErrorCode::Io, std::format("no se pudo leer la textura {}", key));
    }
    // Como la guarda el navegador: RGBA premultiplicado de 8 bits.
    const SkImageInfo info = codec->getInfo().makeColorType(kRGBA_8888_SkColorType).makeAlphaType(kPremul_SkAlphaType)
                                 .makeColorSpace(nullptr);
    auto [img, r] = codec->getImage(info);
    if (!img) {
        return fail(ErrorCode::Io, std::format("no se pudo decodificar la textura {}", key));
    }
    cache.emplace(key, img);
    return img;
}

// whitenedOverlay: la textura aclarada hacia el blanco, píxel a píxel como getImageData/putImageData
// (Uint8ClampedArray redondea al par).
Result<sk_sp<SkImage>> whitened(const std::filesystem::path& assets, int index, double strength_pct) {
    static std::mutex mutex;
    static std::map<std::string, sk_sp<SkImage>> cache;
    const double strength = (100.0 - strength_pct) / 100.0;
    const std::string key = std::format("{}:{}:{:.3f}", core::path_to_utf8(assets), index, strength);
    {
        const std::scoped_lock lock(mutex);
        if (const auto it = cache.find(key); it != cache.end()) {
            return it->second;
        }
    }
    auto tex = texture(assets, std::format("paper_overlay_{}.webp", index));
    if (!tex) {
        return std::unexpected(tex.error());
    }
    const int w = (*tex)->width();
    const int h = (*tex)->height();
    std::vector<std::uint8_t> px(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4);
    const SkImageInfo straight = SkImageInfo::Make(w, h, kRGBA_8888_SkColorType, kUnpremul_SkAlphaType);
    (void)(*tex)->readPixels(straight, px.data(), static_cast<std::size_t>(w) * 4, 0, 0);
    for (std::size_t i = 0; i < px.size(); i += 4) {
        for (std::size_t c = 0; c < 3; ++c) {
            const double v = px[i + c] + ((255.0 - px[i + c]) * strength);
            px[i + c] = static_cast<std::uint8_t>(std::clamp(std::nearbyint(v), 0.0, 255.0));
        }
    }
    auto img = SkImages::RasterFromData(straight, SkData::MakeWithCopy(px.data(), px.size()),
                                        static_cast<std::size_t>(w) * 4);
    const std::scoped_lock lock(mutex);
    cache.emplace(key, img);
    return img;
}

std::uint32_t to_int32(double x) {
    if (!std::isfinite(x)) {
        return 0;
    }
    const double m = std::fmod(std::trunc(x), 4294967296.0);
    return static_cast<std::uint32_t>(static_cast<std::int64_t>(m < 0 ? m + 4294967296.0 : m));
}

std::uint32_t rotl(std::uint32_t x, int r) { return (x << r) | (x >> (32 - r)); }

struct Jitter {
    double rotation = 0.0;
    double offset_x = 0.0;
    double offset_y = 0.0;
};

// paperJitter: la vibración del modo simple de Paper, función del tiempo.
Jitter paper_jitter(double t_ms, double speed_k, double strength_k, double seed) {
    Jitter j;
    const double rot_speed = 4 * speed_k;
    const double rot_strength = 0.5 * strength_k;
    if (rot_speed > 0 && rot_strength > 0) {
        const double cycle = std::floor(t_ms / (1000 / rot_speed));
        j.rotation = std::fmod(cycle, 2.0) == 0 ? rot_strength : -rot_strength;
    }
    const double sx = 2 * speed_k;
    const double sy = 4 * speed_k;
    if (sx > 0) {
        j.offset_x = (fx_rand({seed, 1, std::floor(t_ms / (1000 / sx))}) - 0.5) * 1 * strength_k;
    }
    if (sy > 0) {
        j.offset_y = (fx_rand({seed, 2, std::floor(t_ms / (1000 / sy))}) - 0.5) * 5 * strength_k;
    }
    return j;
}

// foldFrameAt: el fotograma del pliegue (0–5) o -1 si no se pliega en t.
int fold_frame_at(const std::string& mode, double t, double dur) {
    const double fd = std::min(kFoldDur, dur / 2);
    const double step = fd / kFoldFrames;
    if ((mode == "open" || mode == "both") && t < fd) {
        return static_cast<int>(std::min<double>(kFoldFrames - 1, std::floor(t / step)));
    }
    if ((mode == "close" || mode == "both") && t > dur - fd) {
        return kFoldFrames - 1 -
               static_cast<int>(std::min<double>(kFoldFrames - 1, std::floor((t - (dur - fd)) / step)));
    }
    return -1;
}

SkBlendMode blend_of(const std::string& id) {
    if (id == "multiply") {
        return SkBlendMode::kMultiply;
    }
    if (id == "overlay") {
        return SkBlendMode::kOverlay;
    }
    if (id == "soft-light") {
        return SkBlendMode::kSoftLight;
    }
    if (id == "hard-light") {
        return SkBlendMode::kHardLight;
    }
    if (id == "screen") {
        return SkBlendMode::kScreen;
    }
    return SkBlendMode::kSrcOver;
}

double num(const Json& p, const char* key, double def) {
    const auto it = p.find(key);
    return it != p.end() && it->is_number() ? it->get<double>() : def;
}
bool flag(const Json& p, const char* key, bool def) {
    const auto it = p.find(key);
    return it != p.end() && it->is_boolean() ? it->get<bool>() : def;
}
std::string text(const Json& p, const char* key, const char* def) {
    const auto it = p.find(key);
    return it != p.end() && it->is_string() ? it->get<std::string>() : std::string(def);
}

// El navegador dibuja con suavizado «low»: bilineal, sin mipmaps.
const SkSamplingOptions kSmooth(SkFilterMode::kLinear, SkMipmapMode::kNone);

sk_sp<SkSurface> work(int w, int h) {
    auto s = SkSurfaces::Raster(SkImageInfo::Make(w, h, kRGBA_8888_SkColorType, kPremul_SkAlphaType));
    if (s) {
        s->getCanvas()->clear(SK_ColorTRANSPARENT);
    }
    return s;
}

void draw_image(SkCanvas& c, const sk_sp<SkImage>& img, const SkRect& src, const SkRect& dst,
                SkBlendMode mode = SkBlendMode::kSrcOver, const sk_sp<SkImageFilter>& filter = nullptr) {
    SkPaint paint;
    paint.setBlendMode(mode);
    paint.setImageFilter(filter);
    c.drawImageRect(img, src, dst, kSmooth, &paint, SkCanvas::kFast_SrcRectConstraint);
}

void draw_image(SkCanvas& c, const sk_sp<SkImage>& img, const SkRect& dst, SkBlendMode mode = SkBlendMode::kSrcOver) {
    draw_image(c, img, SkRect::MakeIWH(img->width(), img->height()), dst, mode);
}

} // namespace

double fx_rand(std::initializer_list<double> parts) {
    std::uint32_t h = 0x811c9dc5U;
    for (const double p : parts) {
        std::uint32_t k = to_int32(std::floor(p * 1000003)); // Math.floor(Number(p) * 1000003) | 0
        k *= 0xcc9e2d51U;
        k = rotl(k, 15);
        k *= 0x1b873593U;
        h ^= k;
        h = rotl(h, 13);
        h = (h * 5U) + 0xe6546b64U;
    }
    h ^= h >> 16U;
    h *= 0x85ebca6bU;
    h ^= h >> 13U;
    h *= 0xc2b2ae35U;
    h ^= h >> 16U;
    return static_cast<double>(h) / 4294967296.0;
}

Result<void> paper_cutout(SkCanvas& g, const sk_sp<SkImage>& src, const SkRect& src_rect, const SkRect& content,
                          double t, double dur, const Json& p, double seed, const std::filesystem::path& assets) {
    const int cw = g.imageInfo().width();
    const int ch = g.imageInfo().height();
    const double width = num(p, "width", 25);
    const double roughness = num(p, "roughness", 25);
    const double wrinkle_speed = num(p, "wrinkleSpeed", 4);
    const double big = std::max(content.width(), content.height());
    const double k = big / kRef;
    const double t_ms = t * 1000;
    const int fold_idx =
        wrinkle_speed > 0 ? static_cast<int>(std::fmod(std::floor(t_ms / (1000 / wrinkle_speed)), 4.0)) : 0;
    const int fold_frame = fold_frame_at(text(p, "fold", "none"), t, dur);
    const double cx = content.x() + (content.width() / 2);
    const double cy = content.y() + (content.height() / 2);

    // 1. Contenido: la imagen, la textura de arrugas (que también pinta el borde de papel) y la sombra
    //    del pliegue.
    auto C = work(cw, ch);
    auto O = work(cw, ch);
    if (!C || !O) {
        return fail(ErrorCode::Internal, "paper_cutout: Skia no pudo reservar los lienzos");
    }
    draw_image(*C->getCanvas(), src, src_rect, content);
    const double paper_w = content.width() + (2 * (width + roughness) * k);
    const double paper_h = content.height() + (2 * (width + roughness) * k);
    auto overlay = whitened(assets, fold_idx, num(p, "wrinkles", 75));
    if (!overlay) {
        return std::unexpected(overlay.error());
    }
    draw_image(*C->getCanvas(), *overlay,
               SkRect::MakeXYWH(static_cast<float>(cx - (paper_w / 2)), static_cast<float>(cy - (paper_h / 2)),
                                static_cast<float>(paper_w), static_cast<float>(paper_h)),
               blend_of(text(p, "blend", "multiply")));
    const double S = std::max(paper_w, paper_h);
    const SkRect square = SkRect::MakeXYWH(static_cast<float>(cx - (S / 2)), static_cast<float>(cy - (S / 2)),
                                           static_cast<float>(S), static_cast<float>(S));
    if (fold_frame >= 0) {
        auto layer = texture(assets, std::format("paper_fold_{}.webp", fold_frame));
        if (!layer) {
            return std::unexpected(layer.error());
        }
        draw_image(*C->getCanvas(), *layer, square);
    }

    // 2. La silueta rasgada (la imagen engordada y desplazada por ruido) como alfa: el filtro SVG de
    //    paper.js con las mismas primitivas de Skia que usa el navegador.
    sk_sp<SkImageFilter> torn;
    if (flag(p, "border", true) && width > 0) {
        const auto radius = static_cast<float>(std::trunc(width * k)); // FEMorphology: entero
        auto dilated = SkImageFilters::Dilate(radius, radius, nullptr);
        const auto freq = static_cast<float>(num(p, "detail", 0.02) / std::max(1e-3, k));
        auto noise = SkImageFilters::Shader(SkShaders::MakeFractalNoise(
            freq, freq, 3, static_cast<float>(kTornSeeds.at(static_cast<std::size_t>(fold_idx)) + ((seed - 1) * 7))));
        auto displaced = SkImageFilters::DisplacementMap(SkColorChannel::kR, SkColorChannel::kG,
                                                         static_cast<float>(roughness * k), noise, dilated);
        auto white = SkImageFilters::ColorFilter(SkColorFilters::Blend(SK_ColorWHITE, SkBlendMode::kSrcIn), displaced);
        torn = SkImageFilters::Merge(white, nullptr);
    }
    draw_image(*O->getCanvas(), src, src_rect, content, SkBlendMode::kSrcOver, torn);
    C->getCanvas()->drawImage(O->makeImageSnapshot(), 0, 0, SkSamplingOptions(), [] {
        static SkPaint paint;
        paint.setBlendMode(SkBlendMode::kDstIn);
        return &paint;
    }());

    // 3. Pliegue: la máscara del fotograma recorta el papel.
    sk_sp<SkImage> stamp = C->makeImageSnapshot();
    if (fold_frame >= 0) {
        auto mask = texture(assets, std::format("paper_mask_{}.webp", fold_frame));
        if (!mask) {
            return std::unexpected(mask.error());
        }
        auto F = work(cw, ch);
        draw_image(*F->getCanvas(), *mask, square);
        SkPaint in;
        in.setBlendMode(SkBlendMode::kSrcIn);
        F->getCanvas()->drawImage(stamp, 0, 0, SkSamplingOptions(), &in);
        stamp = F->makeImageSnapshot();
    }

    // 4. Al lienzo, con sombra (en px del lienzo, sin el giro) y la vibración.
    Jitter j;
    if (flag(p, "jitter", true)) {
        j = paper_jitter(t_ms, num(p, "jitterSpeed", 1), num(p, "jitterStrength", 1), seed);
    }
    const double jk = big / 1000;
    g.save();
    if (flag(p, "shadow", true)) {
        const std::string hex = text(p, "shadowColor", "#000000");
        const unsigned long n = hex.size() == 7 ? std::stoul(hex.substr(1), nullptr, 16) : 0;
        const double alpha = num(p, "shadowOpacity", 50) / 100;
        const SkColor color = SkColorSetARGB(static_cast<U8CPU>(std::lround(std::clamp(alpha, 0.0, 1.0) * 255)),
                                             (n >> 16U) & 255U, (n >> 8U) & 255U, n & 255U);
        const auto sigma = static_cast<float>(num(p, "shadowBlur", 5) * jk / 2); // la mitad del difuminado
        SkPaint layer;
        layer.setImageFilter(SkImageFilters::DropShadow(static_cast<float>(num(p, "shadowX", 5) * jk),
                                                        static_cast<float>(-num(p, "shadowY", -5) * jk), sigma, sigma,
                                                        color, nullptr));
        g.saveLayer(nullptr, &layer);
    }
    g.translate(static_cast<float>(cx + (j.offset_x * jk)), static_cast<float>(cy - (j.offset_y * jk)));
    if (j.rotation != 0) {
        g.rotate(static_cast<float>(j.rotation));
    }
    SkPaint paint;
    paint.setAntiAlias(j.rotation != 0);
    g.drawImage(stamp, static_cast<float>(-cx), static_cast<float>(-cy), kSmooth, &paint);
    g.restore();
    if (flag(p, "shadow", true)) {
        g.restore();
    }
    return {};
}

} // namespace gc::render::detail
