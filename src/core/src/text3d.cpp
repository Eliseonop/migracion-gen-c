#include "gc/core/text3d.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <format>
#include <numbers>
#include <string>

#include "gc/core/blend.hpp"
#include "gc/core/masks.hpp"
#include "gc/core/text_metrics.hpp"
#include "py.hpp"

namespace gc::core {

using detail::py_len;
using detail::py_num;

namespace {

constexpr double kPerspectiveDefault = 0.5;

const Json& field(const Json& obj, std::string_view key) {
    static const Json kNull;
    if (!obj.is_object()) {
        return kNull;
    }
    const auto it = obj.find(key);
    return it == obj.end() ? kNull : *it;
}

// min(hi, max(lo, v)) de Python.
double py_clamp(double v, double lo, double hi) { return std::min(hi, std::max(lo, v)); }

double radians(double deg) { return deg * (std::numbers::pi / 180.0); }

// _pos_frac de text_ass.py con st.get(key, 0.5): el defecto solo si falta la clave.
double pos_frac(const Json& style, std::string_view key) {
    const bool present = style.is_object() && style.contains(key);
    return py_clamp(present ? py_num(style.at(key), 0.5) : 0.5, -10.0, 11.0);
}

// clipper._fnum: el número con 6 decimales que lee FFmpeg (f"{v:.6f}"), leído de vuelta.
double fnum(double v) {
    const std::string s = std::format("{:.6f}", v);
    double out = 0.0;
    std::from_chars(s.data(), s.data() + s.size(), out);
    return out == 0.0 ? 0.0 : out; // "-0" → 0
}

} // namespace

double text3d_focal(double frame_h, const Json& perspective) {
    const double p = std::min(1.0, std::max(0.0, py_num(perspective, kPerspectiveDefault)));
    return frame_h / (0.25 + (1.5 * p));
}

std::optional<std::pair<double, double>> text3d_angles(double rot_x, double rot_y) {
    // _num: un NaN cuenta como 0.
    const double rx = py_clamp(std::isnan(rot_x) ? 0.0 : rot_x, -kText3dMaxAngle, kText3dMaxAngle);
    const double ry = py_clamp(std::isnan(rot_y) ? 0.0 : rot_y, -kText3dMaxAngle, kText3dMaxAngle);
    if (std::abs(rx) < 0.05 && std::abs(ry) < 0.05) {
        return std::nullopt;
    }
    return std::pair{rx, ry};
}

std::optional<std::pair<double, double>> text3d_angles(const Json& props) {
    return text3d_angles(py_num(field(props, "rot_x"), 0.0), py_num(field(props, "rot_y"), 0.0));
}

Projected text3d_project(double cx, double cy, double rx, double ry, double f, double x, double y) {
    const double dx = x - cx;
    const double dy = y - cy;
    const double a = radians(rx);
    const double b = radians(ry);
    const double y1 = dy * std::cos(a);
    const double z1 = -dy * std::sin(a);
    const double x2 = (dx * std::cos(b)) - (z1 * std::sin(b));
    const double z2 = (dx * std::sin(b)) + (z1 * std::cos(b));
    double w = (f + z2) / f;
    if (std::abs(w) < 1e-3) { // en el plano de la cámara: al infinito (acotado)
        w = w >= 0 ? 1e-3 : -1e-3;
    }
    return {cx + (x2 / w), cy + (y1 / w), w};
}

std::pair<double, double> text3d_half_extents(const Json& style, std::string_view text, double width, double height,
                                              double font_px, const std::optional<TextShadow>& shadow) {
    static_cast<void>(height);
    const double scale = py_num(field(style, "scale"), 1.0);
    const double bw = std::max(0.2, std::min(1.0, py_num(field(style, "w"), 0.88))) * width * (scale > 0 ? scale : 1.0);
    const auto per_line = std::max<std::int64_t>(1, static_cast<std::int64_t>(std::min(1e15, bw / std::max(1.0, 0.5 * font_px))));
    std::int64_t lines = 0;
    std::size_t pos = 0;
    while (true) {
        const std::size_t nl = text.find('\n', pos);
        const std::string_view part = text.substr(pos, nl == std::string_view::npos ? std::string_view::npos : nl - pos);
        const double n = static_cast<double>(py_len(part));
        lines += std::max<std::int64_t>(1, static_cast<std::int64_t>(std::ceil(n / static_cast<double>(per_line))));
        if (nl == std::string_view::npos) {
            break;
        }
        pos = nl + 1;
    }
    lines = lines == 0 ? 1 : lines;
    double pad = (font_px * 0.6) + (py_num(field(style, "border_width"), 0.0) * 2);
    if (shadow) {
        pad += std::max(std::abs(shadow->dx), std::abs(shadow->dy)) + (3 * shadow->sigma);
    }
    return {(bw / 2) + pad, (static_cast<double>(lines) * 1.22 * font_px / 2) + pad};
}

WarpRegion text3d_warp_region(const std::vector<Text3dPose>& poses, double width, double height) {
    std::vector<double> xs;
    std::vector<double> ys;
    for (const Text3dPose& p : poses) {
        for (const int sx : {-1, 1}) {
            for (const int sy : {-1, 1}) {
                const double x = p.cx + (sx * p.hx);
                const double y = p.cy + (sy * p.hy);
                xs.push_back(x);
                ys.push_back(y);
                const Projected q = text3d_project(p.cx, p.cy, p.rx, p.ry, p.f, x, y);
                if (q.w > 0.05) {
                    xs.push_back(q.x);
                    ys.push_back(q.y);
                }
            }
        }
    }
    // min() y max() de Python: el primero, cambiado solo por uno estrictamente menor (mayor).
    const auto bounds = [](const std::vector<double>& v) {
        double lo = v.front();
        double hi = v.front();
        for (const double d : v) {
            lo = d < lo ? d : lo;
            hi = d > hi ? d : hi;
        }
        return std::pair{lo, hi};
    };
    constexpr double kMargin = 4.0;
    if (xs.empty()) {
        return {};
    }
    const auto [min_x, max_x] = bounds(xs);
    const auto [min_y, max_y] = bounds(ys);
    double x0 = min_x - kMargin;
    double x1 = max_x + kMargin;
    double y0 = min_y - kMargin;
    double y1 = max_y + kMargin;
    // Acotado a 2 cuadros alrededor del lienzo: más lejos no se ve nada.
    x0 = std::max(x0, -width);
    y0 = std::max(y0, -height);
    x1 = std::min(x1, 2 * width);
    y1 = std::min(y1, 2 * height);
    return {static_cast<std::int64_t>(std::floor(x0)), static_cast<std::int64_t>(std::floor(y0)),
            static_cast<std::int64_t>(std::ceil(x1)), static_cast<std::int64_t>(std::ceil(y1))};
}

Corners text3d_region_corners(const Text3dPose& p, const WarpRegion& r) {
    const auto x0 = static_cast<double>(r.x0);
    const auto y0 = static_cast<double>(r.y0);
    const auto x1 = static_cast<double>(r.x1);
    const auto y1 = static_cast<double>(r.y1);
    Corners out;
    const std::array<std::pair<double, double>, 4> src{{{x0, y0}, {x1, y0}, {x0, y1}, {x1, y1}}};
    for (std::size_t i = 0; i < 4; ++i) {
        const Projected q = text3d_project(p.cx, p.cy, p.rx, p.ry, p.f, src[i].first, src[i].second);
        out[i] = {q.x - x0, q.y - y0};
    }
    return out;
}

std::vector<std::pair<std::int64_t, double>> text_frames(const TimelineClip& clip, double fps) {
    // max(1.0, float(fps or 30)) de Python: 0 vale 30 y un NaN, 1.
    const double rate = std::max(1.0, fps == 0 ? 30.0 : fps);
    const double start = std::max(0.0, clip.start);
    const double end = start + text_clip_duration(clip);
    const auto k0 = static_cast<std::int64_t>(std::ceil((start * rate) - 1e-6));
    const auto k1 = static_cast<std::int64_t>(std::ceil((end * rate) - 1e-6));
    std::vector<std::pair<std::int64_t, double>> out;
    for (std::int64_t k = k0; k < std::max(k0 + 1, k1); ++k) {
        out.emplace_back(k, std::min(end - start, std::max(0.0, (static_cast<double>(k) / rate) - start)));
    }
    return out;
}

bool text_is_3d(const TimelineClip& clip, double fps) {
    const auto angles_at = [&](double t) {
        const TextPose p = text_pose_at(clip, t);
        return text3d_angles(p.rot_x, p.rot_y).has_value();
    };
    if (angles_at(0.0)) {
        return true;
    }
    if (!text_animates(clip, fps)) {
        return false;
    }
    return std::ranges::any_of(text_frames(clip, fps), [&](const auto& f) { return angles_at(f.second); });
}

const Corners& TextWarp::corners_at(std::int64_t k) const {
    for (std::size_t i = 0; i < frames.size(); ++i) {
        if (k < frames[i] + 1) {
            return corners[i];
        }
    }
    return corners.back();
}

std::optional<TextWarp> text_warp_spec(const TimelineClip& clip, const Json& style, double width, double height,
                                       double fps) {
    const double f = text3d_focal(height, field(style, "perspective"));
    const auto frames = text_animates(clip, fps) ? text_frames(clip, fps)
                                                 : std::vector<std::pair<std::int64_t, double>>{{0, 0.0}};
    const Json& appear = field(style, "block_appear");
    const bool slide = appear.is_string() && (appear.get_ref<const std::string&>() == "slide_up" ||
                                              appear.get_ref<const std::string&>() == "slide_left");
    const std::string shown = apply_text_case(clip.text.value_or(""), field(style, "text_case"));
    std::vector<Text3dPose> poses;
    bool any3d = false;
    for (const auto& [k, local] : frames) {
        const Json st = posed_text_style(clip, style, local);
        const auto ang = text3d_angles(st);
        any3d = any3d || ang.has_value();
        const auto [rx, ry] = ang.value_or(std::pair{0.0, 0.0});
        const double font_px = text_font_px(st, height);
        auto [hx, hy] = text3d_half_extents(st, shown, width, height, font_px, text_shadow(st, font_px));
        if (slide) {
            hx += 2.2 * font_px;
            hy += 2.6 * font_px;
        }
        if (std::abs(py_num(field(st, "rotation"), 0.0)) > 0.05) {
            hx = hy = std::hypot(hx, hy);
        }
        poses.push_back({pos_frac(st, "x") * width, pos_frac(st, "y") * height, rx, ry, f, hx, hy});
    }
    if (!any3d) {
        return std::nullopt;
    }
    TextWarp warp;
    warp.region = text3d_warp_region(poses, width, height);
    for (std::size_t i = 0; i < frames.size(); ++i) {
        warp.frames.push_back(frames[i].first);
        Corners c = text3d_region_corners(poses[i], warp.region);
        for (auto& [x, y] : c) {
            x = fnum(x);
            y = fnum(y);
        }
        warp.corners.push_back(c);
    }
    return warp;
}

bool text_own_layer(const TimelineClip& clip, double fps) {
    return has_mask(clip) || text_is_3d(clip, fps) || clip_blend(clip) != "normal";
}

} // namespace gc::core
