#include "gc/core/mask_ui.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

#include "gc/core/masks.hpp"
#include "gc/core/timeline_ui.hpp"

namespace gc::core {

namespace {

double clamp(double v, double lo, double hi) { return std::min(hi, std::max(lo, v)); }

// mask.<key> de la máscara normalizada (todos son números); `def` si no lo es.
double num(const Json& mask, const char* key, double def) {
    const auto it = mask.find(key);
    return it != mask.end() && it->is_number() ? it->get<double>() : def;
}

// `mask.<key> || def` de JS: el 0 también cae al defecto.
double num_or(const Json& mask, const char* key, double def) {
    const double v = num(mask, key, 0);
    return v != 0 && !std::isnan(v) ? v : def;
}

std::string type_of(const Json& mask) {
    const auto it = mask.find("type");
    return it != mask.end() && it->is_string() ? it->get<std::string>() : std::string();
}

} // namespace

MaskGeometry mask_geometry(const Json& mask, const Rect& frame) {
    const double ref = frame.h;
    return {
        .cx = frame.x + (num(mask, "x", 0) * frame.w),
        .cy = frame.y + (num(mask, "y", 0) * frame.h),
        .hw = std::max(0.5, (num(mask, "w", 0) * num(mask, "scale_x", 0) * ref) / 2),
        .hh = std::max(0.5, (num(mask, "h", 0) * num(mask, "scale_y", 0) * ref) / 2),
        .rot = num_or(mask, "rotation", 0) * std::numbers::pi / 180,
        .ref = ref,
        .feather = num(mask, "feather", 0) * ref,
    };
}

Point to_mask_local(double px, double py, const Json& mask, const Rect& frame) {
    const MaskGeometry g = mask_geometry(mask, frame);
    const double dx = px - g.cx;
    const double dy = py - g.cy;
    const double c = std::cos(-g.rot);
    const double s = std::sin(-g.rot);
    return {((dx * c) - (dy * s)) / g.ref, ((dx * s) + (dy * c)) / g.ref};
}

MaskGeometry mask_handle_box(const Json& mask, const Rect& frame) {
    MaskGeometry g = mask_geometry(mask, frame);
    const std::string type = type_of(mask);
    if (type == "linear") {
        g.hw = frame.w * 0.3;
        g.hh = std::max(18.0, frame.h * 0.035);
    } else if (type == "film") {
        g.hw = frame.w * 0.3;
    }
    return g;
}

MaskHandles mask_handles(const Json& mask, const Rect& frame) {
    const MaskGeometry g = mask_handle_box(mask, frame);
    const double c = std::cos(g.rot);
    const double s = std::sin(g.rot);
    const auto at = [&](double lx, double ly) { return Point{g.cx + (lx * c) - (ly * s), g.cy + (lx * s) + (ly * c)}; };
    const double pad = std::max(16.0, frame.h * 0.022);
    return {at(0, 0), at(g.hw, 0), at(0, g.hh), at(g.hw, g.hh), at(0, -g.hh - pad), at(-g.hw - pad, 0), g};
}

std::optional<std::string> mask_hit_mode(double px, double py, const Json& mask, const Rect& frame, double pad) {
    const MaskHandles h = mask_handles(mask, frame);
    const auto near = [&](const Point& p) { return ((px - p.x) * (px - p.x)) + ((py - p.y) * (py - p.y)) <= pad * pad; };
    if (near(h.rot)) {
        return "rotate";
    }
    if (near(h.fea)) {
        return "feather";
    }
    if (near(h.br)) {
        return "corner";
    }
    if (near(h.r)) {
        return "width";
    }
    if (near(h.b)) {
        return "height";
    }
    const Point local = to_mask_local(px, py, mask, frame);
    const MaskGeometry& g = h.box;
    if (std::abs(local.x) * g.ref <= g.hw && std::abs(local.y) * g.ref <= g.hh) {
        return "move";
    }
    return std::nullopt;
}

Json mask_drag_patch(std::string_view mode, const Json& mask, const Rect& frame, Point p0, Point p) {
    const MaskGeometry g = mask_handle_box(mask, frame);
    if (mode == "move") {
        return {{"mx", js_fixed(clamp(num(mask, "x", 0) + ((p.x - p0.x) / frame.w), -1, 2), 4)},
                {"my", js_fixed(clamp(num(mask, "y", 0) + ((p.y - p0.y) / frame.h), -1, 2), 4)}};
    }
    if (mode == "rotate") {
        const double ang0 = std::atan2(p0.y - g.cy, p0.x - g.cx);
        const double ang = std::atan2(p.y - g.cy, p.x - g.cx);
        return {{"mrot",
                 js_fixed(std::fmod(num_or(mask, "rotation", 0) + ((ang - ang0) * 180 / std::numbers::pi), 360), 2)}};
    }
    const Point l0 = to_mask_local(p0.x, p0.y, mask, frame);
    const Point l = to_mask_local(p.x, p.y, mask, frame);
    if (mode == "feather") {
        return {{"mfeather", js_fixed(clamp(num_or(mask, "feather", 0) + (l0.x - l.x), 0, kMaskFeatherMax), 4)}};
    }
    Json patch = Json::object();
    if (mode == "width" || mode == "corner") {
        patch["mw"] = js_fixed(clamp(std::abs(l.x) * 2 / num_or(mask, "scale_x", 1), 0.01, 8), 4);
    }
    if (mode == "height" || mode == "corner") {
        patch["mh"] = js_fixed(clamp(std::abs(l.y) * 2 / num_or(mask, "scale_y", 1), 0.01, 8), 4);
    }
    return patch;
}

std::optional<Json> mask_brush_point(const Json& mask, const Rect& frame, Point p, bool first, const Json* last) {
    const Point l = to_mask_local(p.x, p.y, mask, frame);
    Json pt = {{"x", js_fixed(l.x / num_or(mask, "scale_x", 1), 4)}, {"y", js_fixed(l.y / num_or(mask, "scale_y", 1), 4)}};
    if (first) {
        pt["m"] = 1;
    } else if (last != nullptr &&
               std::hypot(pt["x"].get<double>() - num(*last, "x", 0), pt["y"].get<double>() - num(*last, "y", 0)) <
                   0.004) {
        return std::nullopt;
    }
    return pt;
}

std::string resize_cursor(double deg) {
    static constexpr std::array<const char*, 4> kCursors{"ew-resize", "nwse-resize", "ns-resize", "nesw-resize"};
    const double a = std::fmod(std::fmod(deg, 180) + 180, 180);
    return kCursors[static_cast<std::size_t>(std::round(a / 45)) % kCursors.size()];
}

std::string mask_cursor(double px, double py, const Json& mask, const Rect& frame, bool drawing) {
    if (drawing && type_of(mask) == "brush") {
        return "crosshair";
    }
    const auto mode = mask_hit_mode(px, py, mask, frame);
    if (!mode) {
        return {};
    }
    if (*mode == "move" || *mode == "rotate") {
        return *mode;
    }
    // MASK_HANDLE_OF (corner → br, width → r, height → b, feather → l) y el eje de cada uno en HANDLE_AXIS.
    const double axis = *mode == "corner" ? 45 : *mode == "height" ? 90 : 0;
    return resize_cursor(axis + (mask_handle_box(mask, frame).rot * 180 / std::numbers::pi));
}

std::vector<Point> star_points(double hw, double hh, int spikes, double inner) {
    const int n = std::max(3, spikes);
    std::vector<Point> pts;
    pts.reserve(static_cast<std::size_t>(n) * 2);
    for (int i = 0; i < n * 2; ++i) {
        const double a = (-std::numbers::pi / 2) + ((i * std::numbers::pi) / n);
        const double r = i % 2 == 0 ? 1 : inner;
        pts.push_back({std::cos(a) * hw * r, std::sin(a) * hh * r});
    }
    return pts;
}

std::vector<Point> heart_points(double hw, double hh, int n) {
    std::vector<Point> raw;
    raw.reserve(static_cast<std::size_t>(std::max(0, n)));
    for (int i = 0; i < n; ++i) {
        const double t = (static_cast<double>(i) / n) * std::numbers::pi * 2;
        raw.push_back({16 * std::pow(std::sin(t), 3),
                       -((13 * std::cos(t)) - (5 * std::cos(2 * t)) - (2 * std::cos(3 * t)) - std::cos(4 * t))});
    }
    if (raw.empty()) {
        return raw;
    }
    const auto [min_x, max_x] = std::ranges::minmax(raw, {}, &Point::x);
    const auto [min_y, max_y] = std::ranges::minmax(raw, {}, &Point::y);
    for (Point& q : raw) {
        q = {((((q.x - min_x.x) / (max_x.x - min_x.x)) * 2) - 1) * hw, ((((q.y - min_y.y) / (max_y.y - min_y.y)) * 2) - 1) * hh};
    }
    return raw;
}

} // namespace gc::core
