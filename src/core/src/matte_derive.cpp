#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "cv_draw.hpp"
#include "gc/core/bg_matte.hpp"
#include "json_reader.hpp"
#include "py.hpp"

// derive_matte de clip_bg.py: el matte crudo del modelo (uint8) → el alfa final del clip (uint8),
// con niveles e invertir (la LUT), expansión, pluma, el pincel y la opacidad, en ese orden. Todo
// lo que no es la LUT va en float32, como numpy y OpenCV: GaussianBlur (núcleo de
// getGaussianKernel, borde BORDER_REFLECT_101), resize INTER_AREA e INTER_LINEAR. Los trazos del
// pincel, con el rasterizador de OpenCV portado (cv_draw.hpp).

namespace gc::core {

namespace {

using detail::py_float;
using detail::py_round;
using detail::py_truthy;

constexpr double kExpandMax = 0.06; // MATTE_EXPAND_MAX (unidades de alto)

struct Plane {
    int w = 0;
    int h = 0;
    std::vector<float> v;
    [[nodiscard]] float at(int x, int y) const {
        return v[(static_cast<std::size_t>(y) * static_cast<std::size_t>(w)) + static_cast<std::size_t>(x)];
    }
    float& at(int x, int y) {
        return v[(static_cast<std::size_t>(y) * static_cast<std::size_t>(w)) + static_cast<std::size_t>(x)];
    }
};

Plane make_plane(int w, int h, float fill = 0.0F) {
    return {w, h, std::vector<float>(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), fill)};
}

// borderInterpolate de OpenCV con BORDER_REFLECT_101 (también con un núcleo más grande que la imagen).
int reflect101(int p, int len) {
    if (len == 1) {
        return 0;
    }
    while (p < 0 || p >= len) {
        p = p < 0 ? -p : (2 * len) - p - 2;
    }
    return p;
}

// getGaussianKernel(n, sigma, CV_32F): en doble y normalizado, cada coeficiente a float.
std::vector<float> gaussian_kernel(int n, double sigma) {
    std::vector<double> cd(static_cast<std::size_t>(n));
    const double scale2 = -0.5 / (sigma * sigma);
    double sum = 0.0;
    for (int i = 0; i < n; ++i) {
        const double x = i - ((n - 1) * 0.5);
        cd[static_cast<std::size_t>(i)] = std::exp(scale2 * x * x);
        sum += cd[static_cast<std::size_t>(i)];
    }
    sum = 1.0 / sum;
    std::vector<float> k(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        k[static_cast<std::size_t>(i)] = static_cast<float>(cd[static_cast<std::size_t>(i)] * sum);
    }
    return k;
}

// GaussianBlur(src, (0, 0), sigma) de una imagen float32: el tamaño del núcleo de
// createGaussianKernels para profundidades que no son de 8 bits (cvRound(σ·4·2 + 1) | 1), filas y
// después columnas.
Plane gaussian_blur(const Plane& src, double sigma) {
    const int n = static_cast<int>(std::lrint((sigma * 8.0) + 1.0)) | 1; // cvRound: al par
    const auto k = gaussian_kernel(n, sigma);
    const int r = n / 2;
    Plane rows = make_plane(src.w, src.h);
    for (int y = 0; y < src.h; ++y) {
        for (int x = 0; x < src.w; ++x) {
            float acc = 0.0F;
            for (int i = 0; i < n; ++i) {
                acc += k[static_cast<std::size_t>(i)] * src.at(reflect101(x + i - r, src.w), y);
            }
            rows.at(x, y) = acc;
        }
    }
    Plane out = make_plane(src.w, src.h);
    for (int y = 0; y < src.h; ++y) {
        for (int x = 0; x < src.w; ++x) {
            float acc = 0.0F;
            for (int i = 0; i < n; ++i) {
                acc += k[static_cast<std::size_t>(i)] * rows.at(x, reflect101(y + i - r, src.h));
            }
            out.at(x, y) = acc;
        }
    }
    return out;
}

// Los pesos de una dimensión de resize INTER_AREA al reducir (computeResizeAreaTab de OpenCV): de
// qué píxeles de la fuente sale cada uno del destino y con qué peso.
struct AreaTap {
    int di = 0;
    int si = 0;
    float alpha = 0.0F;
};

std::vector<AreaTap> area_taps(int ssize, int dsize, double scale) {
    std::vector<AreaTap> tab;
    for (int dx = 0; dx < dsize; ++dx) {
        const double fsx1 = dx * scale;
        const double fsx2 = fsx1 + scale;
        const double cell = std::min(scale, ssize - fsx1);
        const int sx2 = std::min(static_cast<int>(std::floor(fsx2)), ssize - 1);
        const int sx1 = std::min(static_cast<int>(std::ceil(fsx1)), sx2);
        if (sx1 - fsx1 > 1e-3) {
            tab.push_back({dx, sx1 - 1, static_cast<float>((sx1 - fsx1) / cell)});
        }
        for (int sx = sx1; sx < sx2; ++sx) {
            tab.push_back({dx, sx, static_cast<float>(1.0 / cell)});
        }
        if (fsx2 - sx2 > 1e-3) {
            tab.push_back({dx, sx2, static_cast<float>(std::min({fsx2 - static_cast<double>(sx2), 1.0, cell}) / cell)});
        }
    }
    return tab;
}

Plane resize_area(const Plane& src, int dw, int dh) {
    const auto xt = area_taps(src.w, dw, static_cast<double>(src.w) / dw);
    const auto yt = area_taps(src.h, dh, static_cast<double>(src.h) / dh);
    Plane rows = make_plane(dw, src.h);
    for (int y = 0; y < src.h; ++y) {
        for (const auto& t : xt) {
            rows.at(t.di, y) += src.at(t.si, y) * t.alpha;
        }
    }
    Plane out = make_plane(dw, dh);
    for (const auto& t : yt) {
        for (int x = 0; x < dw; ++x) {
            out.at(x, t.di) += rows.at(x, t.si) * t.alpha;
        }
    }
    return out;
}

// resize INTER_LINEAR: centros alineados (fx = (dx + 0,5)·escala − 0,5), los bordes repiten.
Plane resize_linear(const Plane& src, int dw, int dh) {
    const auto taps = [](int ssize, int dsize) {
        const double scale = static_cast<double>(ssize) / dsize;
        std::vector<std::pair<int, float>> t(static_cast<std::size_t>(dsize));
        for (int d = 0; d < dsize; ++d) {
            const auto f = static_cast<float>(((d + 0.5) * scale) - 0.5);
            int s = static_cast<int>(std::floor(f));
            float frac = f - static_cast<float>(s);
            if (s < 0) {
                s = 0;
                frac = 0.0F;
            }
            if (s >= ssize - 1) {
                s = ssize - 1;
                frac = 0.0F;
            }
            t[static_cast<std::size_t>(d)] = {s, frac};
        }
        return t;
    };
    const auto xt = taps(src.w, dw);
    const auto yt = taps(src.h, dh);
    Plane out = make_plane(dw, dh);
    for (int y = 0; y < dh; ++y) {
        const auto [sy, fy] = yt[static_cast<std::size_t>(y)];
        const int sy1 = std::min(sy + 1, src.h - 1);
        for (int x = 0; x < dw; ++x) {
            const auto [sx, fx] = xt[static_cast<std::size_t>(x)];
            const int sx1 = std::min(sx + 1, src.w - 1);
            const float top = (src.at(sx, sy) * (1.0F - fx)) + (src.at(sx1, sy) * fx);
            const float bot = (src.at(sx, sy1) * (1.0F - fx)) + (src.at(sx1, sy1) * fx);
            out.at(x, y) = (top * (1.0F - fy)) + (bot * fy);
        }
    }
    return out;
}

// feather_alpha: el desenfoque de `filter: blur(Npx)` del canvas; con σ grande, a escala reducida.
Plane feather_alpha(const Plane& alpha, double sigma) {
    if (sigma <= 0.3) {
        return alpha;
    }
    const int step = std::max(1, static_cast<int>(std::floor(sigma / 16.0)));
    if (step > 1) {
        const Plane small = resize_area(alpha, std::max(2, alpha.w / step), std::max(2, alpha.h / step));
        return resize_linear(gaussian_blur(small, sigma / step), alpha.w, alpha.h);
    }
    return gaussian_blur(alpha, sigma);
}

// expand_alpha: difuminar y sumar un sesgo, que mueve el cruce del borde.
Plane expand_alpha(Plane alpha, double expansion, int height) {
    if (std::abs(expansion) < 1e-4) {
        return alpha;
    }
    const double sigma = std::abs(expansion) * kExpandMax * height;
    Plane b = sigma > 0.3 ? feather_alpha(alpha, sigma) : std::move(alpha);
    const auto bias = static_cast<float>(expansion * 0.5);
    for (float& v : b.v) {
        v = std::clamp(v + bias, 0.0F, 1.0F);
    }
    return b;
}

// edits_alpha y paint_edit: los trazos de un tipo (keep o erase) en un lienzo de 8 bits de
// max(2, alto) × max(2, ancho), con cv2.polylines y cv2.circle con LINE_AA (cv_draw.hpp, el código
// de OpenCV); nada si no hay ninguno. De 0–255 a 0–1 en float32, como `img.astype(np.float32) / 255`.
std::optional<Plane> edits_alpha(const Json& edits, int w, int h, std::string_view op) {
    if (!edits.is_array()) {
        return std::nullopt;
    }
    std::optional<detail::CvGray> img;
    const int cw = std::max(2, w);
    const int ch = std::max(2, h);
    for (const Json& e : edits) {
        if (!e.is_object() || e.value("op", std::string()) != op) {
            continue;
        }
        if (!img) {
            img = detail::CvGray{cw, ch, std::vector<std::uint8_t>(static_cast<std::size_t>(cw) * ch, 0)};
        }
        const Json& pts = e.contains("points") ? e.at("points") : Json();
        if (!pts.is_array() || pts.empty()) {
            continue;
        }
        const double size = py_float(e.contains("size") ? e.at("size") : Json()).value_or(0.08);
        const auto thick = static_cast<int>(std::max(1.0, py_round(size * ch, 0)));
        // Un punto con «m» empieza otro trazo.
        std::vector<std::vector<detail::CvPoint>> strokes(1);
        for (const Json& p : pts) {
            if (p.contains("m") && py_truthy(p.at("m")) && !strokes.back().empty()) {
                strokes.emplace_back();
            }
            strokes.back().push_back({static_cast<std::int64_t>(py_round(py_float(p.at("x")).value_or(0.0) * cw, 0)),
                                      static_cast<std::int64_t>(py_round(py_float(p.at("y")).value_or(0.0) * ch, 0))});
        }
        const int cap = std::max(1, thick / 2);
        for (const auto& s : strokes) {
            if (s.size() > 1) {
                detail::cv_polylines_aa(*img, s, thick);
            }
            detail::cv_circle_filled_aa(*img, s.front(), cap);
            if (s.size() > 1) {
                detail::cv_circle_filled_aa(*img, s.back(), cap);
            }
        }
    }
    if (!img) {
        return std::nullopt;
    }
    Plane out = make_plane(cw, ch);
    for (std::size_t i = 0; i < out.v.size(); ++i) {
        out.v[i] = static_cast<float>(img->px[i]) / 255.0F;
    }
    return out;
}

} // namespace

std::vector<std::uint8_t> derive_matte(std::span<const std::uint8_t> raw, int width, int height,
                                       const Json& normalized_auto) {
    const auto lut = matte_lut(normalized_auto);
    std::vector<std::uint8_t> a(raw.size());
    std::ranges::transform(raw, a.begin(), [&](std::uint8_t v) { return lut[v]; });
    const auto num = [&](std::string_view key, double fallback) {
        return normalized_auto.contains(key) ? py_float(normalized_auto.at(key)).value_or(fallback) : fallback;
    };
    const double feather = num("feather", 0.0);
    const double expansion = num("expansion", 0.0);
    const double opacity = num("opacity", 1.0);
    const Json& provider = normalized_auto.contains("provider") ? normalized_auto.at("provider") : Json();
    const bool interactive =
        provider.is_string() && provider.get_ref<const std::string&>().starts_with("sam"); // is_interactive_provider
    const Json edits = interactive || !normalized_auto.contains("edits") ? Json::array() : normalized_auto.at("edits");
    auto keep = edits_alpha(edits, width, height, "keep");
    auto erase = edits_alpha(edits, width, height, "erase");
    if (feather <= 0.0 && std::abs(expansion) < 1e-4 && opacity >= 1.0 && !keep && !erase) {
        return a; // sin ajustes espaciales la LUT ya es el resultado exacto
    }
    Plane out = make_plane(width, height);
    for (std::size_t i = 0; i < a.size(); ++i) {
        out.v[i] = static_cast<float>(a[i]) / 255.0F;
    }
    out = expand_alpha(std::move(out), expansion, height);
    out = feather_alpha(out, feather * height);
    // Los lienzos del pincel miden max(2, ·): en un matte de 1 px de alto o ancho, más que el alfa.
    const auto paint = [&](const std::optional<Plane>& p, bool is_keep) {
        if (!p) {
            return;
        }
        for (int y = 0; y < height; ++y) {
            for (int x = 0; x < width; ++x) {
                const float e = p->at(x, y);
                float& v = out.at(x, y);
                v = is_keep ? std::max(v, e) : std::min(v, 1.0F - e);
            }
        }
    };
    paint(keep, true);
    paint(erase, false);
    if (opacity < 1.0) {
        const auto o = static_cast<float>(opacity);
        for (float& v : out.v) {
            v *= o;
        }
    }
    std::vector<std::uint8_t> res(out.v.size());
    for (std::size_t i = 0; i < out.v.size(); ++i) {
        res[i] = static_cast<std::uint8_t>(std::clamp((out.v[i] * 255.0F) + 0.5F, 0.0F, 255.0F));
    }
    return res;
}

} // namespace gc::core
