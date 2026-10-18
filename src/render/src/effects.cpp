#include "gc/render/effects.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <vector>

namespace gc::render {

namespace {

std::uint8_t clip8(std::int64_t v) { return static_cast<std::uint8_t>(std::clamp<std::int64_t>(v, 0, 255)); }
std::uint8_t round8(double v) { return clip8(static_cast<std::int64_t>(std::floor(v + 0.5))); }

// --- RGB ↔ YUV --------------------------------------------------------------------------------
// El export da los efectos en gbrap y FFmpeg mete una conversión a yuva444p para los filtros que
// solo saben de YUV (eq, hue, unsharp): sin etiqueta de color, swscale usa BT.601 de rango
// limitado. Y, U y V van en los bytes de R, G y B; el alfa no cambia.
constexpr double kKr = 0.299;
constexpr double kKb = 0.114;
constexpr double kKg = 1.0 - kKr - kKb;

void to_yuv(Image& img) {
    auto& p = img.pixels;
    for (std::size_t i = 0; i < p.size(); i += 4) {
        const double r = p[i];
        const double g = p[i + 1];
        const double b = p[i + 2];
        const double y = kKr * r + kKg * g + kKb * b;
        p[i] = round8(16.0 + y * 219.0 / 255.0);
        p[i + 1] = round8(128.0 + (b - y) / (2.0 * (1.0 - kKb)) * 224.0 / 255.0);
        p[i + 2] = round8(128.0 + (r - y) / (2.0 * (1.0 - kKr)) * 224.0 / 255.0);
    }
}

void to_rgb(Image& img) {
    auto& p = img.pixels;
    for (std::size_t i = 0; i < p.size(); i += 4) {
        const double y = (p[i] - 16.0) * 255.0 / 219.0;
        const double u = (p[i + 1] - 128.0) * 255.0 / 224.0;
        const double v = (p[i + 2] - 128.0) * 255.0 / 224.0;
        const double r = y + 2.0 * (1.0 - kKr) * v;
        const double b = y + 2.0 * (1.0 - kKb) * u;
        const double g = (y - kKr * r - kKb * b) / kKg;
        p[i] = round8(r);
        p[i + 1] = round8(g);
        p[i + 2] = round8(b);
    }
}

// --- gblur (vf_gblur_init.h, steps = 1) ---------------------------------------------------------
// El filtro recursivo de Alvarez–Mazorra: una pasada causal y otra anticausal por filas y después
// por columnas, con el borde extendido (boundaryscale), en float y con las mismas operaciones que
// FFmpeg; los cuatro canales (planes = 0xF).
void gblur(Image& img, double sigma_in) {
    const auto sigma = static_cast<float>(sigma_in);
    const float sigma2 = sigma * sigma;
    const double lambda = static_cast<double>(sigma2) / 2.0;
    const double dnu = (1.0 + 2.0 * lambda - std::sqrt(1.0 + 4.0 * lambda)) / (2.0 * lambda);
    auto post = static_cast<float>(dnu / lambda);
    auto bscale = static_cast<float>(1.0 / (1.0 - dnu));
    auto nu = static_cast<float>(dnu);
    if (!std::isnormal(post)) {
        post = 1.0F;
    }
    if (!std::isnormal(bscale)) {
        bscale = 1.0F;
    }
    if (!std::isnormal(nu)) {
        nu = 0.0F;
    }
    const float postscale = post * post; // la horizontal por la vertical
    const auto w = static_cast<std::size_t>(img.width);
    const auto h = static_cast<std::size_t>(img.height);
    std::vector<float> buf(w * h);
    for (std::size_t c = 0; c < 4; ++c) {
        for (std::size_t i = 0; i < w * h; ++i) {
            buf[i] = img.pixels[(4 * i) + c];
        }
        for (std::size_t y = 0; y < h; ++y) {
            float* ptr = buf.data() + (w * y);
            ptr[0] *= bscale;
            for (std::size_t x = 1; x < w; ++x) {
                ptr[x] += nu * ptr[x - 1];
            }
            ptr[w - 1] *= bscale;
            for (std::size_t x = w - 1; x > 0; --x) {
                ptr[x - 1] += nu * ptr[x];
            }
        }
        // Por columnas, recorriendo filas enteras (las mismas operaciones en cada elemento).
        for (std::size_t x = 0; x < w; ++x) {
            buf[x] *= bscale;
        }
        for (std::size_t y = 1; y < h; ++y) {
            for (std::size_t x = 0; x < w; ++x) {
                buf[(y * w) + x] += nu * buf[((y - 1) * w) + x];
            }
        }
        for (std::size_t x = 0; x < w; ++x) {
            buf[((h - 1) * w) + x] *= bscale;
        }
        for (std::size_t y = h - 1; y > 0; --y) {
            for (std::size_t x = 0; x < w; ++x) {
                buf[((y - 1) * w) + x] += nu * buf[(y * w) + x];
            }
        }
        for (std::size_t i = 0; i < w * h; ++i) {
            const float v = std::clamp(buf[i] * postscale, 0.0F, 255.0F);
            img.pixels[(4 * i) + c] = static_cast<std::uint8_t>(std::lrint(v));
        }
    }
}

// --- unsharp (vf_unsharp.c) en la luma ----------------------------------------------------------
// 5×5: la media es la binomial [1 4 6 4 1] en x y en y (las sumas en cascada de FFmpeg), con el
// borde extendido; res = Y + ((Y − media) · cantidad) >> 16, con la cantidad en 16.16.
void unsharp_luma(Image& img, double amount_in) {
    const auto amount = static_cast<std::int64_t>(static_cast<double>(static_cast<float>(amount_in)) * 65536.0);
    if (amount == 0) {
        return;
    }
    const int w = img.width;
    const int h = img.height;
    constexpr std::array<std::uint32_t, 5> kTaps{1, 4, 6, 4, 1};
    const auto luma = [&](int x, int y) -> std::uint32_t {
        return img.pixels[4 * ((static_cast<std::size_t>(y) * static_cast<std::size_t>(w)) + static_cast<std::size_t>(x))];
    };
    std::vector<std::uint32_t> rows(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            std::uint32_t sum = 0;
            for (int k = 0; k < 5; ++k) {
                sum += kTaps[static_cast<std::size_t>(k)] * luma(std::clamp(x + k - 2, 0, w - 1), y);
            }
            rows[(static_cast<std::size_t>(y) * static_cast<std::size_t>(w)) + static_cast<std::size_t>(x)] = sum;
        }
    }
    std::vector<std::uint8_t> out(rows.size());
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            std::uint32_t sum = 0;
            for (int k = 0; k < 5; ++k) {
                const auto yy = static_cast<std::size_t>(std::clamp(y + k - 2, 0, h - 1));
                sum += kTaps[static_cast<std::size_t>(k)] *
                       rows[(yy * static_cast<std::size_t>(w)) + static_cast<std::size_t>(x)];
            }
            const auto blur = static_cast<std::int64_t>((sum + 128) >> 8);
            const auto src = static_cast<std::int64_t>(luma(x, y));
            out[(static_cast<std::size_t>(y) * static_cast<std::size_t>(w)) + static_cast<std::size_t>(x)] =
                clip8(src + (((src - blur) * amount) >> 16));
        }
    }
    for (std::size_t i = 0; i < out.size(); ++i) {
        img.pixels[4 * i] = out[i];
    }
}

// --- eq (vf_eq.h, process_c y create_lut) en un canal -------------------------------------------
// Y con contraste y brillo; U y V con la saturación como contraste y sin brillo. Un contraste de
// 7,9 o más (en valor absoluto) va por la tabla en coma flotante, como allí.
void eq_channel(Image& img, std::size_t ch, double contrast, double brightness) {
    if (contrast == 1.0 && brightness == 0.0) {
        return;
    }
    std::array<std::uint8_t, 256> lut{};
    if (std::abs(contrast) < 7.9) {
        const int c16 = static_cast<int>(contrast * 256 * 16);
        const int b = ((static_cast<int>((100.0 * brightness) + 100.0) * 511) / 200) - 128 - (c16 / 32);
        for (int i = 0; i < 256; ++i) {
            lut[static_cast<std::size_t>(i)] = clip8(((i * c16) >> 12) + b);
        }
    } else {
        for (int i = 0; i < 256; ++i) {
            const double v = (contrast * ((i / 255.0) - 0.5)) + 0.5 + brightness;
            lut[static_cast<std::size_t>(i)] =
                v <= 0.0 ? 0 : (v >= 1.0 ? 255 : static_cast<std::uint8_t>(256.0 * v));
        }
    }
    for (std::size_t i = ch; i < img.pixels.size(); i += 4) {
        img.pixels[i] = lut[img.pixels[i]];
    }
}

void eq(Image& img, const core::EqParams& p) {
    eq_channel(img, 0, p.contrast, p.brightness);
    eq_channel(img, 1, p.saturation, 0.0);
    eq_channel(img, 2, p.saturation, 0.0);
}

// --- hue (vf_hue.c): gira (U, V) `deg` grados y lo escala por `sat`, en enteros ----------------
void hue(Image& img, double deg, double sat) {
    const double rad = deg * std::numbers::pi / 180.0;
    const auto s = static_cast<std::int32_t>(std::lrint(std::sin(rad) * (1 << 16) * sat));
    const auto c = static_cast<std::int32_t>(std::lrint(std::cos(rad) * (1 << 16) * sat));
    for (std::size_t i = 0; i < img.pixels.size(); i += 4) {
        const std::int32_t u = img.pixels[i + 1] - 128;
        const std::int32_t v = img.pixels[i + 2] - 128;
        img.pixels[i + 1] = clip8(((c * u) - (s * v) + (1 << 15) + (128 << 16)) >> 16);
        img.pixels[i + 2] = clip8(((s * u) + (c * v) + (1 << 15) + (128 << 16)) >> 16);
    }
}

// --- colorchannelmixer (vf_colorchannelmixer.c, 8 bits): suma de tablas redondeadas -------------
void mix(Image& img, const core::ColorMatrix& m) {
    std::array<std::array<std::array<std::int32_t, 256>, 3>, 3> lut{};
    for (std::size_t r = 0; r < 3; ++r) {
        for (std::size_t c = 0; c < 3; ++c) {
            for (std::size_t i = 0; i < 256; ++i) {
                lut[r][c][i] = static_cast<std::int32_t>(std::lrint(static_cast<double>(i) * m[r][c]));
            }
        }
    }
    for (std::size_t i = 0; i < img.pixels.size(); i += 4) {
        const std::uint8_t r = img.pixels[i];
        const std::uint8_t g = img.pixels[i + 1];
        const std::uint8_t b = img.pixels[i + 2];
        for (std::size_t k = 0; k < 3; ++k) {
            img.pixels[i + k] = clip8(lut[k][0][r] + lut[k][1][g] + lut[k][2][b]);
        }
    }
}

constexpr core::ColorMatrix kSepia{{{0.393, 0.769, 0.189}, {0.349, 0.686, 0.168}, {0.272, 0.534, 0.131}}};

// --- noise (vf_noise.c, alls=N:allf=t) ----------------------------------------------------------
constexpr std::size_t kMaxNoise = 5120;
constexpr std::size_t kMaxShift = 1024;
constexpr std::size_t kMaxRes = kMaxNoise - kMaxShift;

// splitmix64: el azar del grano (FFmpeg usa su AVLFG; no hace falta el mismo grano).
struct Random {
    std::uint64_t state;
    std::uint64_t next() {
        std::uint64_t z = (state += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30U)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27U)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31U);
    }
    double unit() { return static_cast<double>(next() >> 11U) * 0x1.0p-53; }
};

// init_noise sin NOISE_UNIFORM: gaussiana (Box–Muller polar) de desviación N/√3, acotada a
// −128…127 y truncada. La tabla es la misma en todos los fotogramas, como allí.
std::vector<std::int8_t> noise_table(int strength, std::size_t comp) {
    Random rng{123457U + (comp * 31415U)};
    std::vector<std::int8_t> table(kMaxNoise);
    for (auto& v : table) {
        double x1 = 0;
        double w = 0;
        do {
            x1 = (2.0 * rng.unit()) - 1.0;
            const double x2 = (2.0 * rng.unit()) - 1.0;
            w = (x1 * x1) + (x2 * x2);
        } while (w >= 1.0 || w == 0.0);
        w = std::sqrt((-2.0 * std::log(w)) / w);
        const double y1 = std::clamp(x1 * w * strength / std::sqrt(3.0), -128.0, 127.0);
        v = static_cast<std::int8_t>(static_cast<int>(y1));
    }
    return table;
}

// Cada fila suma la tabla desde un desplazamiento al azar (0…1023) que cambia en cada fotograma
// (allf=t); a partir de 4096 px, la tabla se repite. Solo los canales de color.
void grain(Image& img, int strength, std::uint64_t seed) {
    const auto w = static_cast<std::size_t>(img.width);
    const auto h = static_cast<std::size_t>(img.height);
    for (std::size_t comp = 0; comp < 3; ++comp) {
        const auto table = noise_table(strength, comp);
        Random rng{seed ^ ((comp + 1) * 0xD1B54A32D192ED03ULL)};
        std::vector<std::size_t> shift(std::min(h, kMaxRes));
        for (auto& s : shift) {
            s = static_cast<std::size_t>(rng.next() & (kMaxShift - 1));
        }
        for (std::size_t y = 0; y < h; ++y) {
            const std::size_t base = shift[y & (kMaxRes - 1)];
            std::uint8_t* row = img.pixels.data() + (4 * w * y) + comp;
            for (std::size_t x = 0; x < w; ++x) {
                row[4 * x] = clip8(row[4 * x] + table[base + (x % kMaxRes)]);
            }
        }
    }
}

// --- pixelado: scale=iw/n:ih/n:flags=neighbor y de vuelta ---------------------------------------
// El vecino más próximo de swscale (filters.c de FFmpeg 9): cada píxel de destino toma el de la
// fuente en el que cae su centro, redondeado.
std::vector<std::size_t> point_positions(int src, int dst) {
    const double ratio_inv = 1.0 / (static_cast<double>(dst) / static_cast<double>(src));
    std::vector<std::size_t> pos(static_cast<std::size_t>(dst));
    for (int i = 0; i < dst; ++i) {
        const double p = std::round(((i + 0.5) * ratio_inv) - 0.5);
        pos[static_cast<std::size_t>(i)] = static_cast<std::size_t>(std::clamp(p, 0.0, static_cast<double>(src - 1)));
    }
    return pos;
}

// Lo que cada píxel de un eje toma tras bajar a `size / n` (truncado; 0 deja el tamaño, como el
// scale de FFmpeg) y volver a `size`.
std::vector<std::size_t> pixel_map(int size, int n) {
    int small = static_cast<int>(size / static_cast<double>(n));
    if (small == 0) {
        small = size;
    }
    const auto down = point_positions(size, small);
    const auto up = point_positions(small, size);
    std::vector<std::size_t> out(up.size());
    for (std::size_t i = 0; i < up.size(); ++i) {
        out[i] = down[up[i]];
    }
    return out;
}

void pixelate(Image& img, int n) {
    const auto cols = pixel_map(img.width, n);
    const auto rows = pixel_map(img.height, n);
    const auto w = static_cast<std::size_t>(img.width);
    std::vector<std::uint8_t> out(img.pixels.size());
    for (std::size_t y = 0; y < rows.size(); ++y) {
        for (std::size_t x = 0; x < cols.size(); ++x) {
            const std::size_t from = 4 * ((rows[y] * w) + cols[x]);
            const std::size_t to = 4 * ((y * w) + x);
            std::copy_n(img.pixels.begin() + static_cast<std::ptrdiff_t>(from), 4,
                        out.begin() + static_cast<std::ptrdiff_t>(to));
        }
    }
    img.pixels = std::move(out);
}

} // namespace

void apply_effects(Image& image, const core::ClipEffects& fx, std::uint64_t seed) {
    const auto size = static_cast<std::size_t>(image.width) * static_cast<std::size_t>(image.height) * 4;
    if (image.width <= 0 || image.height <= 0 || image.pixels.size() != size || !fx.any()) {
        return;
    }
    // El espacio en que está la imagen: los filtros de YUV lo piden y los de RGB vuelven.
    bool yuv = false;
    const auto in_yuv = [&] {
        if (!yuv) {
            to_yuv(image);
            yuv = true;
        }
    };
    const auto in_rgb = [&] {
        if (yuv) {
            to_rgb(image);
            yuv = false;
        }
    };
    if (fx.blur > 0) {
        gblur(image, fx.blur);
    }
    if (fx.sharpen > 0) {
        in_yuv();
        unsharp_luma(image, fx.sharpen);
    }
    if (fx.grayscale) {
        in_yuv();
        hue(image, 0.0, 0.0);
    }
    if (fx.sepia) {
        in_rgb();
        mix(image, kSepia);
    }
    if (fx.vhs) {
        in_yuv();
        eq(image, {0.0, 1.35, 0.65});
        hue(image, -8.0, 1.0);
    }
    if (fx.grain > 0) {
        grain(image, fx.grain, seed);
    }
    if (fx.pixelate >= 2) {
        pixelate(image, fx.pixelate);
    }
    if (fx.eq) {
        in_yuv();
        eq(image, *fx.eq);
    }
    if (fx.adjust) {
        in_rgb();
        mix(image, *fx.adjust);
    }
    in_rgb();
}

} // namespace gc::render
