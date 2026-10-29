#include "resample.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

// En x86-64 (SSE2 siempre está), un píxel RGBA por registro de 4 floats; si no, lo mismo en escalar.
#if defined(_M_X64) || defined(__SSE2__)
#define GC_RESAMPLE_SSE2 1
#include <emmintrin.h>
#else
#define GC_RESAMPLE_SSE2 0
#endif

namespace gc::render::detail {

namespace {

std::atomic<bool> g_fast_scaled_draw{true};

// SkImageShader::CubicResamplerMatrix(B = 0, C = 0,6): la fila k es el peso de la muestra k (en
// x − 1,5, x − 0,5, x + 0,5 y x + 1,5) como a + b·t + c·t² + d·t³, con las mismas operaciones en
// float que Skia.
struct Poly {
    float a = 0.0F;
    float b = 0.0F;
    float c = 0.0F;
    float d = 0.0F;
};

std::array<Poly, 4> cubic_rows() {
    const float b = 0.0F;
    const float c = 0.6F;
    return {{
        {(1.F / 6) * b, (-(3.F / 6) * b) - c, ((3.F / 6) * b) + (2 * c), (-(1.F / 6) * b) - c},
        {1 - ((2.F / 6) * b), 0.0F, -3 + ((12.F / 6) * b) + c, 2 - ((9.F / 6) * b) - c},
        {(1.F / 6) * b, ((3.F / 6) * b) + c, 3 - ((15.F / 6) * b) - (2 * c), -2 + ((9.F / 6) * b) + c},
        {0.0F, 0.0F, -c, ((1.F / 6) * b) + c},
    }};
}

// Las 4 muestras de un eje: los texels (ya acotados a la subimagen) y sus pesos.
struct Taps {
    std::array<int, 4> index{};
    std::array<float, 4> weight{};
};

// `center`: la coordenada de la muestra en la subimagen (la x o la y de la etapa del bicúbico);
// `limit`: el ancho o el alto de la subimagen.
Taps taps_at(float center, int limit, const std::array<Poly, 4>& rows) {
    Taps out;
    // bicubic_clamp_8888: t = fract(x + 0,5) y bicubic_wts con FMA.
    const float v = center + 0.5F;
    const float t = v - std::floor(v);
    for (std::size_t k = 0; k < 4; ++k) {
        const Poly& p = rows[k];
        out.weight[k] = std::fma(t, std::fma(t, std::fma(t, p.d, p.c), p.b), p.a);
    }
    // ix_and_ptr: clamp_ex (de FLT_MIN al float anterior al límite) y truncar. Las muestras van
    // de x − 1,5 en pasos de 1, sumados en float como Skia.
    const float lo = std::numeric_limits<float>::min();
    const float hi = std::bit_cast<float>(std::bit_cast<std::uint32_t>(static_cast<float>(limit)) - 1U);
    float s = center - 1.5F;
    for (std::size_t k = 0; k < 4; ++k) {
        out.index[k] = static_cast<int>(std::min(std::max(lo, s), hi));
        s += 1.0F;
    }
    return out;
}

// sk_float_round2int: floor(v + 0,5) en doble, saturado a int.
int round_to_int(float v) {
    const double r = std::floor(static_cast<double>(v) + 0.5);
    constexpr double kMax = 2147483520.0; // SK_MaxS32FitsInFloat
    constexpr double kMin = -kMax;
    return static_cast<int>(std::clamp(r, kMin, kMax));
}

#if !GC_RESAMPLE_SSE2
// Redondeo al par (cvtps de SSE/AVX con el modo por defecto), para 0 ≤ v ≤ 255.
std::uint8_t round_even_byte(float v) {
    constexpr float kMagic = 12582912.0F; // 1,5 · 2²³: sumarlo redondea a entero, al par
    const float r = (v + kMagic) - kMagic;
    return static_cast<std::uint8_t>(static_cast<int>(r));
}

float clamp01(float v) { return std::min(std::max(0.0F, v), 1.0F); }
#endif

} // namespace

void set_fast_scaled_draw(bool on) noexcept { g_fast_scaled_draw.store(on, std::memory_order_relaxed); }

bool fast_scaled_draw() noexcept { return g_fast_scaled_draw.load(std::memory_order_relaxed); }

bool draw_scaled_bicubic(const Image& image, const core::SourceCrop& src, const core::Affine& m, float alpha,
                         const std::optional<core::AffineColorMatrix>& color, const PremulPixels& dst,
                         const PixelRect& clip) {
    // La matriz como la recibe Skia (SkMatrix::MakeAll en float): solo escala y traslación.
    const auto skew_x = static_cast<float>(m.c);
    const auto skew_y = static_cast<float>(m.b);
    const auto sx = static_cast<float>(m.a);
    const auto sy = static_cast<float>(m.d);
    const auto tx = static_cast<float>(m.e);
    const auto ty = static_cast<float>(m.f);
    if (skew_x != 0.0F || skew_y != 0.0F || !std::isfinite(sx) || !std::isfinite(sy) || !std::isfinite(tx) ||
        !std::isfinite(ty) || sx == 0.0F || sy == 0.0F) {
        return false;
    }
    if (image.width <= 0 || image.height <= 0 || dst.pixels == nullptr || dst.width <= 0 || dst.height <= 0) {
        return false;
    }

    // El recorte en float (SkRect::MakeXYWH: el borde derecho es x + w en float), recortado a la
    // imagen como SkBitmapDevice::drawImageRect (la matriz de src a dst es la identidad).
    const auto img_w = static_cast<float>(image.width);
    const auto img_h = static_cast<float>(image.height);
    float left = static_cast<float>(src.sx);
    float top = static_cast<float>(src.sy);
    float right = left + static_cast<float>(src.sw);
    float bottom = top + static_cast<float>(src.sh);
    if (!(left < right && top < bottom)) {
        return false;
    }
    left = std::max(left, 0.0F);
    top = std::max(top, 0.0F);
    right = std::min(right, img_w);
    bottom = std::min(bottom, img_h);
    if (!(left < right && top < bottom)) {
        return true; // fuera de la imagen: Skia no dibuja nada
    }

    // kStrict con un recorte menor que la imagen: la subimagen roundOut(recorte), a la que se
    // acotan las muestras, y la traslación de su origen en la matriz del shader.
    const bool subset = !(left <= 0.0F && top <= 0.0F && right >= img_w && bottom >= img_h);
    int gx = 0;
    int gy = 0;
    int gw = image.width;
    int gh = image.height;
    if (subset) {
        gx = static_cast<int>(std::floor(left));
        gy = static_cast<int>(std::floor(top));
        gw = static_cast<int>(std::ceil(right)) - gx;
        gh = static_cast<int>(std::ceil(bottom)) - gy;
    }
    // La matriz total (cuadro · traslación de la subimagen, SkMatrix::setConcat) y su inversa
    // (SkMatrix::invert), en float sin FMA, como el núcleo de Skia.
    const float total_tx = (sx * static_cast<float>(gx)) + tx;
    const float total_ty = (sy * static_cast<float>(gy)) + ty;
    const float inv_sx = 1.0F / sx;
    const float inv_sy = 1.0F / sy;
    const float inv_tx = -total_tx * inv_sx;
    const float inv_ty = -total_ty * inv_sy;
    if (!std::isfinite(inv_sx) || !std::isfinite(inv_sy) || !std::isfinite(inv_tx) || !std::isfinite(inv_ty)) {
        return false;
    }

    // Los píxeles que se pintan: el recorte llevado al cuadro (SkMatrix::mapPoints) y redondeado
    // (SkScan::FillRect). Fuera de 16.16 Skia lo rellena como camino, con otras reglas: que lo haga él.
    float x0 = (left * sx) + tx;
    float x1 = (right * sx) + tx;
    float y0 = (top * sy) + ty;
    float y1 = (bottom * sy) + ty;
    if (x0 > x1) {
        std::swap(x0, x1);
    }
    if (y0 > y1) {
        std::swap(y0, y1);
    }
    constexpr float kFixedMax = 32767.0F;
    if (!(std::abs(x0) <= kFixedMax && std::abs(x1) <= kFixedMax && std::abs(y0) <= kFixedMax &&
          std::abs(y1) <= kFixedMax)) {
        return false;
    }
    const int px0 = std::max({round_to_int(x0), clip.left, 0});
    const int px1 = std::min({round_to_int(x1), clip.right, dst.width});
    const int py0 = std::max({round_to_int(y0), clip.top, 0});
    const int py1 = std::min({round_to_int(y1), clip.bottom, dst.height});
    if (px0 >= px1 || py0 >= py1) {
        return true;
    }

    const std::array<Poly, 4> rows = cubic_rows();
    const auto nx = static_cast<std::size_t>(px1 - px0);

    // Las muestras de cada columna: el centro del píxel por la inversa, con FMA
    // (matrix_scale_translate). Y el tramo de columnas de la fuente que tocan.
    std::vector<Taps> cols(nx);
    int col_lo = gw;
    int col_hi = -1;
    for (std::size_t i = 0; i < nx; ++i) {
        const float cx = std::fma(static_cast<float>(px0 + static_cast<int>(i)) + 0.5F, inv_sx, inv_tx);
        cols[i] = taps_at(cx, gw, rows);
        col_lo = std::min(col_lo, *std::ranges::min_element(cols[i].index));
        col_hi = std::max(col_hi, *std::ranges::max_element(cols[i].index));
    }
    const std::size_t span = static_cast<std::size_t>(col_hi - col_lo) + 1;
    for (Taps& t : cols) {
        for (int& idx : t.index) {
            idx -= col_lo; // relativas al tramo
        }
    }

    // Una fila de la fuente filtrada en horizontal: RGBA en float por cada píxel del cuadro, sin
    // premultiplicar y en 0–1 (from_byte: el byte por 1/255 en float).
    std::vector<float> texels(span * 4);
    const auto filter_row = [&](int row, float* out) {
        const std::uint8_t* p = image.pixels.data() +
                                ((static_cast<std::size_t>(gy + row) * static_cast<std::size_t>(image.width)) +
                                 static_cast<std::size_t>(gx + col_lo)) * 4;
        for (std::size_t j = 0; j < span * 4; ++j) {
            texels[j] = static_cast<float>(p[j]) * (1 / 255.0F);
        }
        const float* tex = texels.data();
        for (std::size_t i = 0; i < nx; ++i) {
            const Taps& t = cols[i];
            const float* t0 = tex + (static_cast<std::size_t>(t.index[0]) * 4);
            const float* t1 = tex + (static_cast<std::size_t>(t.index[1]) * 4);
            const float* t2 = tex + (static_cast<std::size_t>(t.index[2]) * 4);
            const float* t3 = tex + (static_cast<std::size_t>(t.index[3]) * 4);
#if GC_RESAMPLE_SSE2
            __m128 acc = _mm_mul_ps(_mm_set1_ps(t.weight[0]), _mm_loadu_ps(t0));
            acc = _mm_add_ps(acc, _mm_mul_ps(_mm_set1_ps(t.weight[1]), _mm_loadu_ps(t1)));
            acc = _mm_add_ps(acc, _mm_mul_ps(_mm_set1_ps(t.weight[2]), _mm_loadu_ps(t2)));
            acc = _mm_add_ps(acc, _mm_mul_ps(_mm_set1_ps(t.weight[3]), _mm_loadu_ps(t3)));
            _mm_storeu_ps(out + (i * 4), acc);
#else
            for (std::size_t c = 0; c < 4; ++c) {
                out[(i * 4) + c] = (t.weight[0] * t0[c]) + (t.weight[1] * t1[c]) + (t.weight[2] * t2[c]) +
                                   (t.weight[3] * t3[c]);
            }
#endif
        }
    };

    // Las filas filtradas que pide cada fila del cuadro (4 a lo sumo), reutilizadas mientras sirvan.
    struct Slot {
        int row = -1;
        std::vector<float> rgba;
    };
    std::array<Slot, 4> slots;
    for (Slot& s : slots) {
        s.rgba.resize(nx * 4);
    }
    const float paint_alpha = alpha;
    // La matriz de color como la 4×5 de Skia (color_filter de canvas.cpp): filas R, G, B y A, el
    // desplazamiento en 0–1 y el alfa sin tocar.
    std::array<float, 20> cm{};
    if (color) {
        for (std::size_t r = 0; r < 3; ++r) {
            for (std::size_t c = 0; c < 3; ++c) {
                cm[(r * 5) + c] = static_cast<float>((*color)[r][c]);
            }
            cm[(r * 5) + 4] = static_cast<float>((*color)[r][3]);
        }
        cm[18] = 1.0F;
    }
    const bool matrix = color.has_value();
    for (int y = py0; y < py1; ++y) {
        const float cy = std::fma(static_cast<float>(y) + 0.5F, inv_sy, inv_ty);
        const Taps ty_taps = taps_at(cy, gh, rows);
        std::array<const float*, 4> src_rows{};
        for (std::size_t k = 0; k < 4; ++k) {
            const int want = ty_taps.index[k];
            Slot* hit = nullptr;
            for (Slot& s : slots) {
                if (s.row == want) {
                    hit = &s;
                    break;
                }
            }
            if (hit == nullptr) {
                for (Slot& s : slots) {
                    if (std::ranges::find(ty_taps.index, s.row) == ty_taps.index.end()) {
                        hit = &s; // una fila que esta del cuadro no usa
                        break;
                    }
                }
                // Con 4 huecos y como mucho 4 filas distintas, siempre queda uno libre.
                filter_row(want, hit->rgba.data());
                hit->row = want;
            }
            src_rows[k] = hit->rgba.data();
        }
        // Por cada píxel: las 4 filas por sus pesos en vertical, clamp_01, premul, el alfa de la
        // pintura (scale_1_float) y srcover_rgba_8888 (d·(1 − a) + c·255, acotado a 0–255 y
        // redondeado al par).
        const float w0 = ty_taps.weight[0];
        const float w1 = ty_taps.weight[1];
        const float w2 = ty_taps.weight[2];
        const float w3 = ty_taps.weight[3];
        const float* r0 = src_rows[0];
        const float* r1 = src_rows[1];
        const float* r2 = src_rows[2];
        const float* r3 = src_rows[3];
        std::uint8_t* out = dst.pixels + (static_cast<std::size_t>(y) * dst.row_bytes) +
                            (static_cast<std::size_t>(px0) * 4);
#if GC_RESAMPLE_SSE2
        const __m128 vw0 = _mm_set1_ps(w0);
        const __m128 vw1 = _mm_set1_ps(w1);
        const __m128 vw2 = _mm_set1_ps(w2);
        const __m128 vw3 = _mm_set1_ps(w3);
        const __m128 zero = _mm_setzero_ps();
        const __m128 one = _mm_set1_ps(1.0F);
        const __m128 v255 = _mm_set1_ps(255.0F);
        const __m128 pa = _mm_set1_ps(paint_alpha);
        const __m128 alpha_lane = _mm_castsi128_ps(_mm_set_epi32(-1, 0, 0, 0));
        const __m128i zi = _mm_setzero_si128();
        const bool scale = paint_alpha != 1.0F;
        // Las columnas de la matriz (el coeficiente de r, g, b y a en cada salida) y el desplazamiento.
        const __m128 col_r = _mm_setr_ps(cm[0], cm[5], cm[10], cm[15]);
        const __m128 col_g = _mm_setr_ps(cm[1], cm[6], cm[11], cm[16]);
        const __m128 col_b = _mm_setr_ps(cm[2], cm[7], cm[12], cm[17]);
        const __m128 col_a = _mm_setr_ps(cm[3], cm[8], cm[13], cm[18]);
        const __m128 offset = _mm_setr_ps(cm[4], cm[9], cm[14], cm[19]);
        const __m128 inf = _mm_set1_ps(std::numeric_limits<float>::infinity());
        // rgb·a y el alfa tal cual (premul).
        const auto premul = [&](__m128 v) {
            const __m128 a = _mm_shuffle_ps(v, v, _MM_SHUFFLE(3, 3, 3, 3));
            return _mm_or_ps(_mm_andnot_ps(alpha_lane, _mm_mul_ps(v, a)), _mm_and_ps(alpha_lane, v));
        };
        for (std::size_t i = 0; i < nx; ++i) {
            const std::size_t at = i * 4;
            __m128 v = _mm_add_ps(_mm_add_ps(_mm_add_ps(_mm_mul_ps(vw0, _mm_loadu_ps(r0 + at)),
                                                        _mm_mul_ps(vw1, _mm_loadu_ps(r1 + at))),
                                             _mm_mul_ps(vw2, _mm_loadu_ps(r2 + at))),
                                  _mm_mul_ps(vw3, _mm_loadu_ps(r3 + at)));
            v = _mm_min_ps(_mm_max_ps(v, zero), one);
            __m128 s = premul(v);
            if (scale) {
                s = _mm_mul_ps(s, pa);
            }
            if (matrix) {
                // unpremul (rgb por 1/a si es finito; si no, 0), la matriz en el orden de
                // matrix_4x5 (m3·a + m4, luego b, g y r), clamp_01 y premul.
                // Con a = 1 (lo opaco, casi todo el vídeo) unpremul no cambia nada: sin la división.
                const __m128 sa = _mm_shuffle_ps(s, s, _MM_SHUFFLE(3, 3, 3, 3));
                __m128 u = s;
                if (_mm_cvtss_f32(sa) != 1.0F) {
                    const __m128 recip = _mm_div_ps(one, sa);
                    const __m128 k = _mm_and_ps(recip, _mm_cmplt_ps(recip, inf));
                    u = _mm_or_ps(_mm_andnot_ps(alpha_lane, _mm_mul_ps(s, k)), _mm_and_ps(alpha_lane, s));
                }
                const __m128 ur = _mm_shuffle_ps(u, u, _MM_SHUFFLE(0, 0, 0, 0));
                const __m128 ug = _mm_shuffle_ps(u, u, _MM_SHUFFLE(1, 1, 1, 1));
                const __m128 ub = _mm_shuffle_ps(u, u, _MM_SHUFFLE(2, 2, 2, 2));
                const __m128 ua = _mm_shuffle_ps(u, u, _MM_SHUFFLE(3, 3, 3, 3));
                __m128 t = _mm_add_ps(_mm_mul_ps(ua, col_a), offset);
                t = _mm_add_ps(_mm_mul_ps(ub, col_b), t);
                t = _mm_add_ps(_mm_mul_ps(ug, col_g), t);
                t = _mm_add_ps(_mm_mul_ps(ur, col_r), t);
                s = premul(_mm_min_ps(_mm_max_ps(t, zero), one));
            }
            const __m128 inv = _mm_sub_ps(one, _mm_shuffle_ps(s, s, _MM_SHUFFLE(3, 3, 3, 3)));
            std::int32_t px = 0;
            std::memcpy(&px, out + at, 4);
            const __m128 d = _mm_cvtepi32_ps(_mm_unpacklo_epi16(_mm_unpacklo_epi8(_mm_cvtsi32_si128(px), zi), zi));
            __m128 o = _mm_add_ps(_mm_mul_ps(d, inv), _mm_mul_ps(s, v255));
            o = _mm_min_ps(_mm_max_ps(o, zero), v255);
            __m128i oi = _mm_cvtps_epi32(o); // al par, como cvtps en Skia
            oi = _mm_packs_epi32(oi, oi);
            oi = _mm_packus_epi16(oi, oi);
            px = _mm_cvtsi128_si32(oi);
            std::memcpy(out + at, &px, 4);
        }
#else
        for (std::size_t i = 0; i < nx; ++i) {
            const std::size_t at = i * 4;
            std::array<float, 4> s{};
            for (std::size_t c = 0; c < 4; ++c) {
                s[c] = clamp01((w0 * r0[at + c]) + (w1 * r1[at + c]) + (w2 * r2[at + c]) + (w3 * r3[at + c]));
            }
            for (std::size_t c = 0; c < 3; ++c) {
                s[c] *= s[3];
            }
            if (paint_alpha != 1.0F) {
                for (float& c : s) {
                    c *= paint_alpha;
                }
            }
            if (matrix) {
                const float recip = 1.0F / s[3];
                const float k = recip < std::numeric_limits<float>::infinity() ? recip : 0.0F;
                const std::array<float, 4> u{s[0] * k, s[1] * k, s[2] * k, s[3]};
                for (std::size_t c = 0; c < 4; ++c) {
                    const float* row = cm.data() + (c * 5);
                    s[c] = clamp01((u[0] * row[0]) +
                                   ((u[1] * row[1]) + ((u[2] * row[2]) + ((u[3] * row[3]) + row[4]))));
                }
                for (std::size_t c = 0; c < 3; ++c) {
                    s[c] *= s[3];
                }
            }
            const float inv = 1.0F - s[3];
            for (std::size_t c = 0; c < 4; ++c) {
                const float o = (static_cast<float>(out[at + c]) * inv) + (s[c] * 255.0F);
                out[at + c] = round_even_byte(std::min(std::max(0.0F, o), 255.0F));
            }
        }
#endif
    }
    return true;
}

} // namespace gc::render::detail
