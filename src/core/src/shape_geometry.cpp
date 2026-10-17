#include "gc/core/shape_geometry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>

#include "gc/core/keyframes.hpp"
#include "gc/core/shapes.hpp"
#include "json_reader.hpp"
#include "py.hpp"

namespace gc::core {

using detail::py_float;
using detail::py_num;
using detail::py_truthy;

namespace {

constexpr double kPi = std::numbers::pi;

const Json& field(const Json& obj, std::string_view key) {
    static const Json kNull;
    if (!obj.is_object()) {
        return kNull;
    }
    const auto it = obj.find(key);
    return it == obj.end() ? kNull : *it;
}

double clamp(double v, double lo, double hi) { return std::min(hi, std::max(lo, v)); }

// float(v or fallback) de Python; lo que no es un número, el defecto.
double num_or(const Json& v, double fallback) {
    if (!py_truthy(v)) {
        return fallback;
    }
    return py_float(v).value_or(fallback);
}

// int(round(x)) de Python: empates al par.
double round_half_even(double x) { return std::nearbyint(x); }

bool is_arrow(std::string_view t) { return t == "arrow" || t == "arrow_curve" || t == "arrow_double"; }

Polyline ellipse_pts(double cx, double cy, double rx, double ry, int n = 64) {
    Polyline out;
    for (int i = 0; i < n; ++i) {
        const double a = static_cast<double>(i) / n * kPi * 2;
        out.push_back({cx + std::cos(a) * rx, cy + std::sin(a) * ry});
    }
    return out;
}

Polyline round_rect(double x, double y, double w, double h, double r, int n = 6) {
    const double rr = clamp(r, 0, std::min(w, h) / 2);
    if (rr < 0.4) {
        return {{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}};
    }
    const std::array<std::array<double, 4>, 4> corners{{
        {x + w - rr, y + rr, -kPi / 2, 0},
        {x + w - rr, y + h - rr, 0, kPi / 2},
        {x + rr, y + h - rr, kPi / 2, kPi},
        {x + rr, y + rr, kPi, kPi * 1.5},
    }};
    Polyline out;
    for (const auto& [cx, cy, a0, a1] : corners) {
        for (int i = 0; i <= n; ++i) {
            const double a = a0 + (a1 - a0) * (static_cast<double>(i) / n);
            out.push_back({cx + std::cos(a) * rr, cy + std::sin(a) * rr});
        }
    }
    return out;
}

Polyline star_pts(double cx, double cy, double r_outer, double r_inner, int spikes) {
    const int n = std::max(3, spikes);
    Polyline out;
    for (int i = 0; i < n * 2; ++i) {
        const double a = -kPi / 2 + (i * kPi) / n;
        const double r = i % 2 == 0 ? r_outer : r_inner;
        out.push_back({cx + std::cos(a) * r, cy + std::sin(a) * r});
    }
    return out;
}

Polyline poly_pts(double cx, double cy, double r, int sides) {
    const int n = std::max(3, sides);
    Polyline out;
    for (int i = 0; i < n; ++i) {
        const double a = -kPi / 2 + i * 2 * kPi / n;
        out.push_back({cx + std::cos(a) * r, cy + std::sin(a) * r});
    }
    return out;
}

Polyline curved_arrow_poly() {
    const double cx = 66.0;
    const double cy = 84.0;
    const double r = 50.0;
    const double half = 5.0;
    const double head_len = 26.0;
    const double head_half = 14.0;
    const double a0 = kPi;
    const double a_neck = kPi * 1.5;
    const int n_shaft = 44;
    Polyline left;
    Polyline right;
    for (int i = 0; i < n_shaft; ++i) {
        const double a = a0 + (a_neck - a0) * (static_cast<double>(i) / n_shaft);
        const double c = std::cos(a);
        const double s = std::sin(a);
        left.push_back({cx + c * (r + half), cy + s * (r + half)});
        right.push_back({cx + c * (r - half), cy + s * (r - half)});
    }
    const Point neck{cx, cy - r};
    Polyline out = left;
    out.push_back({neck.x, neck.y - half});
    out.push_back({neck.x, neck.y - head_half});
    out.push_back({neck.x + head_len, neck.y});
    out.push_back({neck.x, neck.y + head_half});
    out.push_back({neck.x, neck.y + half});
    out.insert(out.end(), right.rbegin(), right.rend());
    return out;
}

Polyline heart_pts() {
    Polyline raw;
    for (int i = 0; i < 64; ++i) {
        const double t = static_cast<double>(i) / 64 * kPi * 2;
        const double x = 16 * std::pow(std::sin(t), 3.0);
        const double y = 13 * std::cos(t) - 5 * std::cos(2 * t) - 2 * std::cos(3 * t) - std::cos(4 * t);
        raw.push_back({x, -y});
    }
    double min_x = raw[0].x;
    double max_x = raw[0].x;
    double min_y = raw[0].y;
    double max_y = raw[0].y;
    for (const Point& p : raw) {
        min_x = std::min(min_x, p.x);
        max_x = std::max(max_x, p.x);
        min_y = std::min(min_y, p.y);
        max_y = std::max(max_y, p.y);
    }
    const double sx = 76 / (max_x - min_x != 0 ? max_x - min_x : 1);
    const double sy = 76 / (max_y - min_y != 0 ? max_y - min_y : 1);
    Polyline out;
    for (const Point& p : raw) {
        out.push_back({12 + (p.x - min_x) * sx, 12 + (p.y - min_y) * sy});
    }
    return out;
}

Point catmull(Point p0, Point p1, Point p2, Point p3, double t) {
    const double t2 = t * t;
    const double t3 = t * t * t;
    const auto c = [&](double a, double b, double cc, double d) {
        return 0.5 * ((2 * b) + (-a + cc) * t + (2 * a - 5 * b + 4 * cc - d) * t2 + (-a + 3 * b - 3 * cc + d) * t3);
    };
    return {c(p0.x, p1.x, p2.x, p3.x), c(p0.y, p1.y, p2.y, p3.y)};
}

// _map_pt: de la caja (0–100) al cuadro, centrada en (cx, cy), de bw × bh y girada `rot` grados.
Point map_pt(Point pt, double cx, double cy, double bw, double bh, double rot_deg) {
    const double rad = rot_deg * kPi / 180;
    const double cos_a = std::cos(rad);
    const double sin_a = std::sin(rad);
    const double lx = (pt.x / 100 - 0.5) * bw;
    const double ly = (pt.y / 100 - 0.5) * bh;
    return {cx + lx * cos_a - ly * sin_a, cy + lx * sin_a + ly * cos_a};
}

ShapeRgb hex_rgb(const Json& v) {
    // _bgr: el color si es «#rrggbb» válido; si no, blanco.
    std::string s = v.is_string() ? v.get<std::string>() : std::string();
    const auto hex = [](char ch) -> int {
        if (ch >= '0' && ch <= '9') {
            return ch - '0';
        }
        if (ch >= 'a' && ch <= 'f') {
            return ch - 'a' + 10;
        }
        if (ch >= 'A' && ch <= 'F') {
            return ch - 'A' + 10;
        }
        return -1;
    };
    if (s.size() != 7 || s[0] != '#' || std::any_of(s.begin() + 1, s.end(), [&](char ch) { return hex(ch) < 0; })) {
        s = "#ffffff";
    }
    const auto byte = [&](std::size_t i) { return static_cast<std::uint8_t>(hex(s[i]) * 16 + hex(s[i + 1])); };
    return {byte(1), byte(3), byte(5)};
}

} // namespace

Polyline path_points(const Json& points, bool closed, bool smooth) {
    Polyline pts;
    for (const auto& [x, y] : path_anchors(points)) {
        pts.push_back({x, y});
    }
    const auto n = static_cast<long long>(pts.size());
    if (n < 2 || !smooth || n == 2) {
        return pts;
    }
    const auto at = [&](long long i) {
        return closed ? pts[static_cast<std::size_t>(((i + n) % n))]
                      : pts[static_cast<std::size_t>(std::max(0LL, std::min(n - 1, i)))];
    };
    Polyline out;
    for (long long i = 0; i < (closed ? n : n - 1); ++i) {
        const Point p0 = at(i - 1);
        const Point p1 = at(i);
        const Point p2 = at(i + 1);
        const Point p3 = at(i + 2);
        for (int k = 0; k < kPathSteps; ++k) {
            out.push_back(catmull(p0, p1, p2, p3, static_cast<double>(k) / kPathSteps));
        }
    }
    if (!closed) {
        out.push_back(pts.back());
    }
    return out;
}

ShapeGeometry shape_geometry(std::string_view type, const Json& opts) {
    const std::string_view t = type.empty() ? std::string_view("rect") : type;
    const double radius = clamp(num_or(field(opts, "cornerRadius"), 0), 0, 0.5) * 50;
    const int sides = static_cast<int>(clamp(round_half_even(num_or(field(opts, "sides"), 6)), 3, 12));
    ShapeGeometry g;
    auto& fills = g.fills;
    auto& strokes = g.strokes;
    if (t == "square" || t == "rect") {
        const double pad = t == "square" ? 12 : 8;
        const double y = t == "square" ? 12 : 22;
        const double h = t == "square" ? 76 : 56;
        fills.push_back(round_rect(pad, y, 100 - pad * 2, h, radius));
    } else if (t == "circle") {
        fills.push_back(ellipse_pts(50, 50, 40, 40));
    } else if (t == "ellipse") {
        fills.push_back(ellipse_pts(50, 50, 44, 28));
    } else if (t == "line") {
        strokes.push_back({{8, 50}, {92, 50}});
    } else if (t == "arrow") {
        fills.push_back({{8, 38}, {58, 38}, {58, 20}, {94, 50}, {58, 80}, {58, 62}, {8, 62}});
    } else if (t == "arrow_double") {
        fills.push_back(
            {{6, 50}, {24, 22}, {24, 38}, {76, 38}, {76, 22}, {94, 50}, {76, 78}, {76, 62}, {24, 62}, {24, 78}});
    } else if (t == "arrow_curve") {
        fills.push_back(curved_arrow_poly());
    } else if (t == "triangle") {
        fills.push_back({{50, 10}, {92, 88}, {8, 88}});
    } else if (t == "star") {
        fills.push_back(star_pts(50, 52, 40, 16, sides));
    } else if (t == "polygon") {
        fills.push_back(poly_pts(50, 52, 40, sides));
    } else if (t == "speech") {
        fills.push_back(round_rect(8, 8, 84, 62, std::max(8.0, radius)));
        fills.push_back({{36, 68}, {22, 94}, {54, 68}});
    } else if (t == "check") {
        strokes.push_back({{18, 52}, {40, 74}, {84, 26}});
    } else if (t == "x") {
        strokes.push_back({{22, 22}, {78, 78}});
        strokes.push_back({{78, 22}, {22, 78}});
    } else if (t == "marker") {
        fills.push_back({{50, 94}, {24, 40}, {76, 40}});
        fills.push_back(ellipse_pts(50, 34, 16, 16));
    } else if (t == "heart") {
        fills.push_back(heart_pts());
    } else if (t == "letterbox") {
        // float(opts.get("bar", 0.12) or 0): sin la clave, 0,12.
        const Json& bar = field(opts, "bar");
        const double b = clamp(opts.is_object() && opts.contains("bar") ? num_or(bar, 0) : 0.12, 0, 0.5) * 100;
        if (b > 0) {
            fills.push_back({{-1, -1}, {101, -1}, {101, b}, {-1, b}});
            fills.push_back({{-1, 100 - b}, {101, 100 - b}, {101, 101}, {-1, 101}});
        }
    } else if (t == "path") {
        const bool closed = py_truthy(field(opts, "closed"));
        const Json& smooth = field(opts, "smooth");
        Polyline line = path_points(field(opts, "points"), closed, !(smooth.is_boolean() && !smooth.get<bool>()));
        if (closed && line.size() >= 3) {
            fills.push_back(std::move(line));
        } else if (line.size() >= 2) {
            strokes.push_back(std::move(line));
        }
    } else {
        fills.push_back(round_rect(8, 22, 84, 56, radius));
    }
    const Json& head = field(opts, "arrowHead");
    if (is_arrow(t) && head == Json("line")) {
        ShapeGeometry out;
        for (Polyline ring : g.fills) {
            ring.push_back(ring.front());
            out.strokes.push_back(std::move(ring));
        }
        return out;
    }
    if (is_arrow(t) && head == Json("none")) {
        return {{}, std::move(g.fills)};
    }
    return g;
}

Polyline trim_polyline(const Polyline& pts, double frac) {
    if (frac >= 1 || pts.size() < 2) {
        return pts;
    }
    if (frac <= 0) {
        return {};
    }
    double total = 0;
    for (std::size_t i = 1; i < pts.size(); ++i) {
        total += std::hypot(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y);
    }
    double left = total * frac;
    Polyline out{pts[0]};
    for (std::size_t i = 1; i < pts.size(); ++i) {
        const Point a = pts[i - 1];
        const Point b = pts[i];
        const double d = std::hypot(b.x - a.x, b.y - a.y);
        if (d >= left) {
            const double u = d > 0 ? left / d : 0;
            out.push_back({a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u});
            return out;
        }
        left -= d;
        out.push_back(b);
    }
    return out;
}

DashRuns dash_runs(const Polyline& pts, std::pair<double, double> pattern) {
    const auto [on, off] = pattern;
    const double period = on + off;
    if (period <= 0 || pts.size() < 2) {
        return {{pts}, {}};
    }
    std::vector<double> cum{0.0};
    for (std::size_t i = 1; i < pts.size(); ++i) {
        cum.push_back(cum.back() + std::hypot(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y));
    }
    const double total = cum.back();
    const auto point_at = [&](double s) {
        // bisect.bisect_left(cum, s), acotado a [1, n − 1]
        const auto lb = static_cast<std::size_t>(std::lower_bound(cum.begin(), cum.end(), s) - cum.begin());
        const std::size_t i = std::max<std::size_t>(1, std::min(pts.size() - 1, lb));
        const Point a = pts[i - 1];
        const Point b = pts[i];
        const double d = cum[i] - cum[i - 1];
        const double u = d > 0 ? (s - cum[i - 1]) / d : 0.0;
        return Point{a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u};
    };
    DashRuns out;
    for (long long k = 0; static_cast<double>(k) * period <= total + 1e-9; ++k) {
        const double s0 = static_cast<double>(k) * period;
        const double s1 = std::min(total, s0 + on);
        if (on <= 1e-9) {
            out.dots.push_back(point_at(s0));
            continue;
        }
        Polyline run{point_at(s0)};
        for (std::size_t i = 0; i < pts.size(); ++i) {
            if (s0 < cum[i] && cum[i] < s1) {
                run.push_back(pts[i]);
            }
        }
        run.push_back(point_at(s1));
        if (s1 > s0) {
            out.runs.push_back(std::move(run));
        }
    }
    return out;
}

std::optional<std::pair<double, double>> dash_pattern(std::string_view dash, double lw) {
    if (dash == "dash") {
        return std::pair{2 * lw, 2 * lw};
    }
    if (dash == "dot") {
        return std::pair{0.0, 2 * lw};
    }
    return std::nullopt;
}

ShapeGeometry flip_geometry(ShapeGeometry geo, bool horizontal, bool vertical) {
    if (!horizontal && !vertical) {
        return geo;
    }
    const auto f = [&](Polyline& line) {
        for (Point& p : line) {
            p = {horizontal ? 100 - p.x : p.x, vertical ? 100 - p.y : p.y};
        }
    };
    std::ranges::for_each(geo.fills, f);
    std::ranges::for_each(geo.strokes, f);
    return geo;
}

Result<double> shape_draw_at(const TimelineClip& clip, double t) {
    if (keyframes_enabled(clip)) {
        return clamp(clip_props_at(clip, t).get("draw"), 0, 1);
    }
    auto st = normalize_shape(clip.shape.value_or(Json()));
    if (!st) {
        return std::unexpected(st.error());
    }
    return (*st)["draw"].get<double>();
}

Result<ShapePlan> shape_plan(const Json& shape, const ShapePose& pose, double draw, Flip flip, int width, int height) {
    auto normalized = normalize_shape(shape);
    if (!normalized) {
        return std::unexpected(normalized.error());
    }
    Json st = std::move(*normalized);
    // rasterize_shape: al doble de resolución (W, H) y con los vértices en enteros.
    const int w = std::max(2, width);
    const int h = std::max(2, height);
    const double big_w = static_cast<double>(w) * 2;
    const double big_h = static_cast<double>(h) * 2;
    const std::string type = st["type"].get<std::string>();
    double frac = clamp(draw, 0, 1);
    // Barras de cine: `draw` es cuánto han entrado las barras, no un trazo.
    if (type == "letterbox") {
        st["bar"] = st["bar"].get<double>() * frac;
        frac = 1.0;
    }
    const ShapeGeometry geo = flip_geometry(shape_geometry(type, st), flip.h, flip.v);
    const double k = std::max(0.0, pose.scale);
    const double cx = pose.x * big_w;
    const double cy = pose.y * big_h;
    const double bw = st["w"].get<double>() * big_w * k;
    const double bh = st["h"].get<double>() * big_h * k;
    const double rot = pose.rotation;
    const bool fill_on = st["fill"] != Json("none");
    const double stroke_width = st["strokeWidth"].get<double>();
    const double sw = std::max(1.0, round_half_even(stroke_width * (big_h / 720)));
    const auto pattern = dash_pattern(st["dash"].get<std::string>(), std::max(1.0, stroke_width * (big_h / 720)));

    ShapePlan plan;
    plan.line_width = sw / 2;
    plan.fill = fill_on ? hex_rgb(st["fill"]) : ShapeRgb{};
    plan.stroke = hex_rgb(st["stroke"]);
    // `if opacity < 0.999: alpha *= opacity`
    plan.opacity = pose.opacity < 0.999 ? clamp(pose.opacity, 0, 1) : 1.0;
    // Un punto del doble, en enteros (int(round())), llevado al cuadro de salida.
    const auto out_pt = [](Point p2) { return Point{round_half_even(p2.x) / 2 + 0.25, round_half_even(p2.y) / 2 + 0.25}; };
    const auto mapped_int = [&](const Polyline& ring) {
        Polyline out;
        for (const Point& p : ring) {
            out.push_back(out_pt(map_pt(p, cx, cy, bw, bh, rot)));
        }
        return out;
    };
    const auto paint_fill = [&](const Polyline& ring) {
        Polyline pts = mapped_int(ring);
        if (pts.size() < 3 || frac <= 0) {
            return;
        }
        plan.ops.push_back({ShapeOp::Kind::Fill, std::move(pts), false, std::min(1.0, frac), {}, 0.0});
    };
    const auto paint_stroke = [&](const Polyline& ring, bool closed) {
        if (frac >= 1 && !pattern) {
            Polyline pts = mapped_int(ring);
            if (pts.size() < 2) {
                return;
            }
            plan.ops.push_back({ShapeOp::Kind::Line, std::move(pts), closed, 1.0, {}, 0.0});
            return;
        }
        Polyline mapped;
        for (const Point& p : ring) {
            mapped.push_back(map_pt(p, cx, cy, bw, bh, rot));
        }
        if (closed && !mapped.empty()) {
            mapped.push_back(mapped.front());
        }
        mapped = trim_polyline(mapped, frac);
        if (mapped.size() < 2) {
            return;
        }
        const DashRuns dr = pattern ? dash_runs(mapped, *pattern) : DashRuns{{mapped}, {}};
        for (const Polyline& run : dr.runs) {
            Polyline pts;
            for (const Point& p : run) {
                pts.push_back(out_pt(p));
            }
            plan.ops.push_back({ShapeOp::Kind::Line, std::move(pts), false, 1.0, {}, 0.0});
        }
        const double rad = std::max(1.0, round_half_even(sw / 2));
        for (const Point& d : dr.dots) {
            plan.ops.push_back({ShapeOp::Kind::Dot, {}, false, 1.0, out_pt(d), rad / 2});
        }
    };
    if (fill_on) {
        for (const Polyline& ring : geo.fills) {
            if (stroke_width > 0) {
                paint_stroke(ring, true);
            }
            paint_fill(ring);
        }
    } else {
        for (const Polyline& ring : geo.fills) {
            paint_stroke(ring, true);
        }
    }
    for (const Polyline& line : geo.strokes) {
        paint_stroke(line, false);
    }
    return plan;
}

} // namespace gc::core
