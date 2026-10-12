#include "gc/render/mask.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <format>
#include <numbers>
#include <optional>
#include <ranges>
#include <utility>

#include "include/core/SkCanvas.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPath.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkRect.h"
#include "include/core/SkSurface.h"

namespace gc::render {

namespace {

using Point = std::pair<double, double>;
using Plane = std::vector<float>; // un canal en float32, por filas, como los arrays de numpy

// El antialias de OpenCV (LINE_AA en fillPoly, ellipse y las líneas gruesas) cubre del todo los
// píxeles cuyo centro cae en el borde y añade un fleco de ~0,2: la forma sale ensanchada ~0,7 px
// por cada lado respecto a la geométrica (medido con un cuadrado, una línea de grosor 8 y un
// círculo de radio 10). Skia la dibuja con ese margen.
constexpr double kCvAaOutset = 0.7;

double num(const Json& m, const char* key, double fallback) {
    const auto it = m.find(key);
    return it != m.end() && it->is_number() ? it->get<double>() : fallback;
}

std::string type_of(const Json& m) {
    const auto it = m.find("type");
    return it != m.end() && it->is_string() ? it->get<std::string>() : std::string("circle");
}

bool enabled(const Json& m) {
    const auto it = m.find("enabled");
    return it == m.end() || !(it->is_boolean() && !it->get<bool>());
}

// round() de Python sobre un real: al entero más cercano, empates al par.
double py_round(double v) { return std::nearbyint(v); }

// mask_geometry de clip_mask.py.
struct Geometry {
    double cx;
    double cy;
    double hw;
    double hh;
    double rot;
    double ref;
    double feather;
};

Geometry geometry(const Json& m, int width, int height) {
    const auto ref = static_cast<double>(height);
    return {
        num(m, "x", 0.5) * width,
        num(m, "y", 0.5) * height,
        std::max(0.5, num(m, "w", 0.5) * num(m, "scale_x", 1.0) * ref / 2.0),
        std::max(0.5, num(m, "h", 0.5) * num(m, "scale_y", 1.0) * ref / 2.0),
        num(m, "rotation", 0.0) * (std::numbers::pi / 180.0), // math.radians
        ref,
        num(m, "feather", 0.0) * ref,
    };
}

// _to_canvas: puntos locales → píxeles del cuadro, girados y redondeados como int(round(v)).
std::vector<Point> to_canvas(const std::vector<Point>& local, const Geometry& g) {
    const double c = std::cos(g.rot);
    const double s = std::sin(g.rot);
    std::vector<Point> out;
    out.reserve(local.size());
    for (const auto& [x, y] : local) {
        out.emplace_back(py_round(g.cx + x * c - y * s), py_round(g.cy + x * s + y * c));
    }
    return out;
}

std::vector<Point> rect_points(const Json& m, const Geometry& g) {
    const double hw = g.hw;
    const double hh = g.hh;
    const double r = std::min(num(m, "radius", 0.0) * std::min(hw, hh) * 2.0, std::min(hw, hh));
    if (r < 0.5) {
        return {{-hw, -hh}, {hw, -hh}, {hw, hh}, {-hw, hh}};
    }
    constexpr double kPi = std::numbers::pi;
    const std::array<std::array<double, 4>, 4> corners{{
        {hw - r, -hh + r, -kPi / 2, 0.0},
        {hw - r, hh - r, 0.0, kPi / 2},
        {-hw + r, hh - r, kPi / 2, kPi},
        {-hw + r, -hh + r, kPi, kPi * 1.5},
    }};
    std::vector<Point> pts;
    for (const auto& [cx, cy, a0, a1] : corners) {
        for (int i = 0; i < 9; ++i) {
            const double a = a0 + (a1 - a0) * (i / 8.0);
            pts.emplace_back(cx + std::cos(a) * r, cy + std::sin(a) * r);
        }
    }
    return pts;
}

std::vector<Point> linear_points(const Geometry& g) {
    const double big = std::max(g.hw, g.hh) * 4.0 + g.ref * 4.0;
    return {{-big, -big}, {big, -big}, {big, 0.0}, {-big, 0.0}};
}

std::vector<Point> film_points(const Geometry& g) {
    const double big = std::max(g.hw, g.hh) * 4.0 + g.ref * 4.0;
    return {{-big, -g.hh}, {big, -g.hh}, {big, g.hh}, {-big, g.hh}};
}

std::vector<Point> star_points(double hw, double hh) {
    constexpr int kSpikes = 5;
    constexpr double kInner = 0.42;
    std::vector<Point> pts;
    for (int i = 0; i < kSpikes * 2; ++i) {
        const double a = -std::numbers::pi / 2 + (i * std::numbers::pi) / kSpikes;
        const double r = i % 2 == 0 ? 1.0 : kInner;
        pts.emplace_back(std::cos(a) * hw * r, std::sin(a) * hh * r);
    }
    return pts;
}

std::vector<Point> heart_points(double hw, double hh) {
    constexpr int kN = 96;
    std::vector<Point> raw;
    for (int i = 0; i < kN; ++i) {
        const double t = (static_cast<double>(i) / kN) * std::numbers::pi * 2;
        const double s = std::sin(t);
        raw.emplace_back(16 * std::pow(s, 3),
                         -(13 * std::cos(t) - 5 * std::cos(2 * t) - 2 * std::cos(3 * t) - std::cos(4 * t)));
    }
    const auto [min_x, max_x] = std::ranges::minmax(raw | std::views::keys);
    const auto [min_y, max_y] = std::ranges::minmax(raw | std::views::values);
    std::vector<Point> pts;
    for (const auto& [x, y] : raw) {
        pts.emplace_back(((x - min_x) / (max_x - min_x) * 2 - 1) * hw, ((y - min_y) / (max_y - min_y) * 2 - 1) * hh);
    }
    return pts;
}

// Un plano de 8 bits dibujado con Skia (A8, con antialias) en float 0–1. OpenCV pone el centro
// del píxel en el entero; Skia, en +0,5: los puntos se desplazan medio píxel.
template <typename Draw>
Plane raster(int w, int h, Draw&& draw) {
    auto surface = SkSurfaces::Raster(SkImageInfo::MakeA8(w, h));
    SkCanvas* canvas = surface->getCanvas();
    canvas->clear(SK_ColorTRANSPARENT);
    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setColor(SK_ColorWHITE);
    std::forward<Draw>(draw)(*canvas, paint);
    std::vector<std::uint8_t> a8(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
    [[maybe_unused]] const bool ok =
        surface->readPixels(SkImageInfo::MakeA8(w, h), a8.data(), static_cast<std::size_t>(w), 0, 0);
    Plane out(a8.size());
    for (std::size_t i = 0; i < a8.size(); ++i) {
        out[i] = static_cast<float>(a8[i]) / 255.0F;
    }
    return out;
}

SkPath polygon(const std::vector<Point>& pts) {
    SkPathBuilder b;
    for (std::size_t i = 0; i < pts.size(); ++i) {
        const auto x = static_cast<float>(pts[i].first + 0.5);
        const auto y = static_cast<float>(pts[i].second + 0.5);
        if (i == 0) {
            b.moveTo(x, y);
        } else {
            b.lineTo(x, y);
        }
    }
    b.close();
    return b.detach();
}

// _paint_brush: trazos de grosor `thick` (cv2.polylines con antialias; extremos y uniones
// redondos) y un punto suelto como círculo de radio thick // 2, con el margen de OpenCV.
void paint_brush(SkCanvas& canvas, const SkPaint& paint, const Json& m, const Geometry& g) {
    const Json brush = m.contains("brush") && m.at("brush").is_object() ? m.at("brush") : Json::object();
    if (!brush.contains("points") || !brush.at("points").is_array() || brush.at("points").empty()) {
        return;
    }
    const double sx = num(m, "scale_x", 1.0);
    const double sy = num(m, "scale_y", 1.0);
    const int thick = std::max(1, static_cast<int>(py_round(num(brush, "size", 0.12) * g.ref * (sx + sy) / 2.0)));
    std::vector<std::vector<Point>> strokes;
    std::vector<Point> stroke;
    for (const Json& p : brush.at("points")) {
        const bool move = p.contains("m") && p.at("m").is_number() && p.at("m").get<double>() != 0;
        if (move && !stroke.empty()) {
            strokes.push_back(std::move(stroke));
            stroke.clear();
        }
        stroke.emplace_back(num(p, "x", 0.0) * g.ref * sx, num(p, "y", 0.0) * g.ref * sy);
    }
    if (!stroke.empty()) {
        strokes.push_back(std::move(stroke));
    }
    for (const auto& s : strokes) {
        const auto pts = to_canvas(s, g);
        if (pts.size() == 1) {
            canvas.drawCircle(static_cast<float>(pts[0].first + 0.5), static_cast<float>(pts[0].second + 0.5),
                              static_cast<float>(std::max(1, thick / 2) + kCvAaOutset), paint);
            continue;
        }
        SkPathBuilder b;
        b.moveTo(static_cast<float>(pts[0].first + 0.5), static_cast<float>(pts[0].second + 0.5));
        for (std::size_t i = 1; i < pts.size(); ++i) {
            b.lineTo(static_cast<float>(pts[i].first + 0.5), static_cast<float>(pts[i].second + 0.5));
        }
        SkPaint line = paint; // el de los puntos sueltos sigue siendo de relleno
        line.setStyle(SkPaint::kStroke_Style);
        line.setStrokeWidth(static_cast<float>(thick + 2 * kCvAaOutset));
        line.setStrokeCap(SkPaint::kRound_Cap);
        line.setStrokeJoin(SkPaint::kRound_Join);
        canvas.drawPath(b.detach(), line);
    }
}

// Índice con el borde reflejado de OpenCV (BORDER_REFLECT_101: …c b | a b c … | … b a).
int reflect101(int i, int n) {
    if (n == 1) {
        return 0;
    }
    while (i < 0 || i >= n) {
        i = i < 0 ? -i : 2 * n - 2 - i;
    }
    return i;
}

// cv2.GaussianBlur(a, (0, 0), sigma) de un plano float32: núcleo de cvRound(8σ + 1) | 1 puntos
// (getGaussianKernel en float32), separable, con BORDER_REFLECT_101.
Plane gaussian(const Plane& a, int w, int h, double sigma) {
    const int n = static_cast<int>(std::nearbyint(sigma * 4 * 2 + 1)) | 1;
    std::vector<float> k(static_cast<std::size_t>(n));
    const double scale2 = -0.5 / (sigma * sigma);
    double sum = 0.0;
    for (int i = 0; i < n; ++i) {
        const double x = i - (n - 1) * 0.5;
        k[static_cast<std::size_t>(i)] = static_cast<float>(std::exp(scale2 * x * x));
        sum += k[static_cast<std::size_t>(i)];
    }
    for (float& v : k) {
        v = static_cast<float>(v * (1.0 / sum));
    }
    const int half = n / 2;
    Plane tmp(a.size());
    for (int y = 0; y < h; ++y) {
        const float* row = &a[static_cast<std::size_t>(y) * static_cast<std::size_t>(w)];
        for (int x = 0; x < w; ++x) {
            float acc = 0.0F;
            for (int i = 0; i < n; ++i) {
                acc += k[static_cast<std::size_t>(i)] * row[reflect101(x + i - half, w)];
            }
            tmp[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)] = acc;
        }
    }
    Plane out(a.size());
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            float acc = 0.0F;
            for (int i = 0; i < n; ++i) {
                acc += k[static_cast<std::size_t>(i)] *
                       tmp[static_cast<std::size_t>(reflect101(y + i - half, h)) * static_cast<std::size_t>(w) +
                           static_cast<std::size_t>(x)];
            }
            out[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)] = acc;
        }
    }
    return out;
}

// Pesos de cv2.resize con INTER_AREA al reducir de `src` a `dst` (computeResizeAreaTab): cada
// destino promedia su caja de la fuente, con los bordes fraccionarios.
std::vector<std::vector<std::pair<int, double>>> area_weights(int src, int dst) {
    const double scale = static_cast<double>(src) / dst;
    std::vector<std::vector<std::pair<int, double>>> out(static_cast<std::size_t>(dst));
    for (int d = 0; d < dst; ++d) {
        const double f1 = d * scale;
        const double f2 = f1 + scale;
        const double cell = std::min(scale, src - f1);
        int s1 = static_cast<int>(std::ceil(f1));
        int s2 = static_cast<int>(std::floor(f2));
        s2 = std::min(s2, src - 1);
        s1 = std::min(s1, s2);
        auto& w = out[static_cast<std::size_t>(d)];
        if (s1 - f1 > 1e-3) {
            w.emplace_back(s1 - 1, (s1 - f1) / cell);
        }
        for (int s = s1; s < s2; ++s) {
            w.emplace_back(s, 1.0 / cell);
        }
        if (f2 - s2 > 1e-3) {
            w.emplace_back(s2, std::min(std::min(f2 - s2, 1.0), cell) / cell);
        }
    }
    return out;
}

Plane resize_area(const Plane& a, int w, int h, int dw, int dh) {
    const auto wx = area_weights(w, dw);
    const auto wy = area_weights(h, dh);
    Plane tmp(static_cast<std::size_t>(dw) * static_cast<std::size_t>(h));
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < dw; ++x) {
            double acc = 0.0;
            for (const auto& [s, wt] : wx[static_cast<std::size_t>(x)]) {
                acc += wt * a[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(s)];
            }
            tmp[static_cast<std::size_t>(y) * static_cast<std::size_t>(dw) + static_cast<std::size_t>(x)] =
                static_cast<float>(acc);
        }
    }
    Plane out(static_cast<std::size_t>(dw) * static_cast<std::size_t>(dh));
    for (int y = 0; y < dh; ++y) {
        for (int x = 0; x < dw; ++x) {
            double acc = 0.0;
            for (const auto& [s, wt] : wy[static_cast<std::size_t>(y)]) {
                acc += wt * tmp[static_cast<std::size_t>(s) * static_cast<std::size_t>(dw) + static_cast<std::size_t>(x)];
            }
            out[static_cast<std::size_t>(y) * static_cast<std::size_t>(dw) + static_cast<std::size_t>(x)] =
                static_cast<float>(acc);
        }
    }
    return out;
}

// cv2.resize con INTER_LINEAR al ampliar: centros de píxel alineados y el borde repetido.
std::vector<std::pair<int, float>> linear_tab(int src, int dst) {
    const double scale = static_cast<double>(src) / dst;
    std::vector<std::pair<int, float>> out(static_cast<std::size_t>(dst));
    for (int d = 0; d < dst; ++d) {
        auto f = static_cast<float>((d + 0.5) * scale - 0.5);
        int s = static_cast<int>(std::floor(f));
        f -= static_cast<float>(s);
        if (s < 0) {
            f = 0.0F;
            s = 0;
        }
        if (s >= src - 1) {
            f = 0.0F;
            s = src - 1;
        }
        out[static_cast<std::size_t>(d)] = {s, f};
    }
    return out;
}

Plane resize_linear(const Plane& a, int w, int h, int dw, int dh) {
    const auto tx = linear_tab(w, dw);
    const auto ty = linear_tab(h, dh);
    Plane out(static_cast<std::size_t>(dw) * static_cast<std::size_t>(dh));
    const auto at = [&](int x, int y) {
        return a[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)];
    };
    for (int y = 0; y < dh; ++y) {
        const auto [sy, fy] = ty[static_cast<std::size_t>(y)];
        const int sy1 = std::min(sy + 1, h - 1);
        for (int x = 0; x < dw; ++x) {
            const auto [sx, fx] = tx[static_cast<std::size_t>(x)];
            const int sx1 = std::min(sx + 1, w - 1);
            const float top = at(sx, sy) * (1 - fx) + at(sx1, sy) * fx;
            const float bottom = at(sx, sy1) * (1 - fx) + at(sx1, sy1) * fx;
            out[static_cast<std::size_t>(y) * static_cast<std::size_t>(dw) + static_cast<std::size_t>(x)] =
                top * (1 - fy) + bottom * fy;
        }
    }
    return out;
}

// _feather: la gaussiana, a resolución reducida si σ es grande (paso = σ // 16).
Plane feather(Plane a, int w, int h, double sigma) {
    if (sigma <= 0.3) {
        return a;
    }
    const int step = std::max(1, static_cast<int>(std::floor(sigma / 16)));
    if (step > 1) {
        const int sw = std::max(2, w / step);
        const int sh = std::max(2, h / step);
        const Plane small = gaussian(resize_area(a, w, h, sw, sh), sw, sh, sigma / step);
        return resize_linear(small, sw, sh, w, h);
    }
    return gaussian(a, w, h, sigma);
}

// mask_alpha de clip_mask.py: el alfa (0–1) de UNA máscara.
Plane one_mask(const Json& m, int w, int h) {
    const Geometry g = geometry(m, w, h);
    const std::string kind = type_of(m);
    Plane a = raster(w, h, [&](SkCanvas& canvas, const SkPaint& paint) {
        if (kind == "circle") {
            // cv2.ellipse con centro y semiejes enteros, girada `rotation` grados.
            const double cx = py_round(g.cx) + 0.5;
            const double cy = py_round(g.cy) + 0.5;
            const double ax = std::max(1.0, py_round(g.hw)) + kCvAaOutset;
            const double ay = std::max(1.0, py_round(g.hh)) + kCvAaOutset;
            canvas.save();
            canvas.translate(static_cast<float>(cx), static_cast<float>(cy));
            canvas.rotate(static_cast<float>(num(m, "rotation", 0.0)));
            canvas.drawOval(SkRect::MakeLTRB(static_cast<float>(-ax), static_cast<float>(-ay), static_cast<float>(ax),
                                             static_cast<float>(ay)),
                            paint);
            canvas.restore();
        } else if (kind == "brush") {
            paint_brush(canvas, paint, m, g);
        } else {
            std::vector<Point> local;
            if (kind == "linear") {
                local = linear_points(g);
            } else if (kind == "film") {
                local = film_points(g);
            } else if (kind == "star") {
                local = star_points(g.hw, g.hh);
            } else if (kind == "heart") {
                local = heart_points(g.hw, g.hh);
            } else {
                local = rect_points(m, g);
            }
            SkPaint outset = paint;
            outset.setStyle(SkPaint::kStrokeAndFill_Style);
            outset.setStrokeWidth(static_cast<float>(2 * kCvAaOutset));
            canvas.drawPath(polygon(to_canvas(local, g)), outset);
        }
    });
    a = feather(std::move(a), w, h, g.feather);
    const bool invert = m.contains("invert") && m.at("invert").is_boolean() && m.at("invert").get<bool>();
    const auto op = static_cast<float>(num(m, "opacity", 1.0));
    for (float& v : a) {
        if (invert) {
            v = 1.0F - v;
        }
        if (op < 0.999F) {
            v = v + (1.0F - v) * (1.0F - op);
        }
        v = std::clamp(v, 0.0F, 1.0F);
    }
    return a;
}

} // namespace

std::string mask_unsupported(const std::vector<Json>& masks) {
    for (const Json& m : masks) {
        if (enabled(m) && type_of(m) == "text") {
            return "el render aún no dibuja máscaras de texto";
        }
    }
    return {};
}

Result<MaskAlpha> mask_alpha(const std::vector<Json>& masks, int width, int height) {
    if (auto reason = mask_unsupported(masks); !reason.empty()) {
        return fail(ErrorCode::Unsupported, reason);
    }
    const int w = std::max(2, width);
    const int h = std::max(2, height);
    std::optional<Plane> total;
    for (const Json& m : masks) {
        if (!m.is_object() || !enabled(m)) {
            continue;
        }
        Plane a = one_mask(m, w, h);
        if (!total) {
            total = std::move(a);
        } else {
            for (std::size_t i = 0; i < a.size(); ++i) {
                (*total)[i] *= a[i];
            }
        }
    }
    MaskAlpha out{w, h, std::vector<std::uint8_t>(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 255)};
    if (total) {
        for (std::size_t i = 0; i < total->size(); ++i) {
            out.alpha[i] = static_cast<std::uint8_t>(std::clamp((*total)[i] * 255.0F + 0.5F, 0.0F, 255.0F));
        }
    }
    return out;
}

} // namespace gc::render
