#include "gc/core/follow.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "edit_util.hpp"
#include "gc/core/clip_layout.hpp"
#include "gc/core/pose.hpp"
#include "gc/core/timeline_rules.hpp"

namespace gc::core {

using detail::invalid;
using detail::py_iter;
using detail::py_mod;
using detail::py_number;
using detail::py_round;
using detail::py_truthy;

namespace {

constexpr double kDegToRad = std::numbers::pi / 180.0; // math.radians
constexpr double kRadToDeg = 180.0 / std::numbers::pi; // math.degrees

// p[key] (o p.get(key, fallback)) de un punto del recorrido: número o bool, como en
// las cuentas de Python; texto y null son error.
Result<double> point_num(const Json& p, std::string_view key, std::optional<double> fallback) {
    const auto it = p.find(key);
    if (it == p.end()) {
        if (fallback) {
            return *fallback;
        }
        return invalid("track: a un punto le falta '" + std::string(key) + "'");
    }
    if (const auto n = py_number(*it)) {
        return *n;
    }
    return invalid("track: '" + std::string(key) + "' debe ser un número");
}

struct TrackPt {
    const Json* raw;
    double t; // s de la FUENTE
};

// Centro del objeto y extremo de su eje, en px de la salida (_obj_screen).
struct Screen {
    double cx;
    double cy;
    double ex;
    double ey;
};

Result<Screen> obj_screen(const TimelineClip& video, const TrackPt& p, double w0, const FollowDims& d, double sp) {
    const double local = (video.reverse ? video.out_point - p.t : p.t - video.in_point) / sp;
    auto rot = point_num(*p.raw, "rot", 0.0);
    auto s = point_num(*p.raw, "s", 1.0);
    auto cx = point_num(*p.raw, "cx", std::nullopt);
    auto cy = point_num(*p.raw, "cy", std::nullopt);
    for (const auto* v : {&rot, &s, &cx, &cy}) {
        if (!*v) {
            return std::unexpected(v->error());
        }
    }
    const double a = *rot * kDegToRad;
    const double r = w0 / 2 * *s; // medio ancho del recuadro, en px de la fuente
    const double ex = *cx + r * std::cos(a) / d.src_w;
    const double ey = *cy + r * std::sin(a) / d.src_h;
    const Point c = source_point_to_output(video, *cx, *cy, d.src_w, d.src_h, d.out_w, d.out_h, p.t, local);
    const Point e = source_point_to_output(video, ex, ey, d.src_w, d.src_h, d.out_w, d.out_h, p.t, local);
    return Screen{c.x * d.out_w, c.y * d.out_h, e.x * d.out_w, e.y * d.out_h};
}

} // namespace

Result<std::vector<FollowKey>> follow_keys(const TimelineClip& video, const TimelineClip& follower, const Json& track,
                                           const FollowDims& dims, double box_w, double anchor_t,
                                           std::string_view mode, std::optional<double> min_gap) {
    if (std::ranges::find(kFollowModes, mode) == kFollowModes.end()) {
        return invalid("mode debe ser uno de ['position', 'position_scale', 'position_scale_rotation']");
    }
    const double sp = clip_speed(video);

    std::vector<Json> points;
    if (py_truthy(track)) {
        auto list = py_iter(track);
        if (!list) {
            return invalid("track debe ser una lista");
        }
        points = std::move(*list);
    }
    std::vector<TrackPt> pts;
    for (const Json& p : points) {
        if (!p.is_object()) {
            return invalid("track: cada punto debe ser un objeto");
        }
        auto t = point_num(p, "t", std::nullopt);
        if (!t) {
            return std::unexpected(t.error());
        }
        if (video.in_point - 1e-6 <= *t && *t <= video.out_point + 1e-6) {
            pts.push_back({&p, *t});
        }
    }
    std::ranges::stable_sort(pts, {}, &TrackPt::t);
    if (pts.empty()) {
        return std::vector<FollowKey>{};
    }
    if (dims.src_w == 0.0 || dims.src_h == 0.0 || dims.out_w == 0.0 || dims.out_h == 0.0) {
        return invalid("dims: ningún tamaño puede ser 0"); // ZeroDivisionError en Python
    }

    auto tl_time = [&](const TrackPt& p) {
        return video.start + (video.reverse ? video.out_point - p.t : p.t - video.in_point) / sp;
    };
    const double w0 = box_w * dims.src_w;
    const TrackPt* ref = &pts.front(); // el primero de los más cercanos al ancla, como min()
    for (const auto& p : pts) {
        if (std::abs(tl_time(p) - anchor_t) < std::abs(tl_time(*ref) - anchor_t)) {
            ref = &p;
        }
    }
    const auto s0 = obj_screen(video, *ref, w0, dims, sp);
    if (!s0) {
        return std::unexpected(s0.error());
    }
    const double v0x = s0->ex - s0->cx;
    const double v0y = s0->ey - s0->cy;
    const double h0 = std::hypot(v0x, v0y);
    const double len0 = h0 != 0.0 ? h0 : 1.0;
    const double ang0 = std::atan2(v0y, v0x);
    const Pose base = clip_pose(follower, std::max(0.0, anchor_t - follower.start));
    const double f_dur = clip_timeline_duration(follower);

    std::vector<const TrackPt*> order;
    for (const auto& p : pts) {
        order.push_back(&p);
    }
    std::ranges::stable_sort(order, {}, [&](const TrackPt* p) { return tl_time(*p); });

    std::vector<FollowKey> out;
    for (const TrackPt* p : order) {
        const double lt = tl_time(*p) - follower.start;
        if (lt < -1e-6 || lt > f_dur + 1e-6) {
            continue;
        }
        if (!out.empty()) {
            if (!min_gap) {
                return invalid("min_gap debe ser un número");
            }
            if (lt - out.back().t < *min_gap - 1e-9) {
                continue;
            }
        }
        const auto sc = obj_screen(video, *p, w0, dims, sp);
        if (!sc) {
            return std::unexpected(sc.error());
        }
        FollowKey k;
        k.t = py_round(std::max(0.0, lt), 4);
        k.x = py_round(base.x + (sc->cx - s0->cx) / dims.out_w, 5);
        k.y = py_round(base.y + (sc->cy - s0->cy) / dims.out_h, 5);
        const double vx = sc->ex - sc->cx;
        const double vy = sc->ey - sc->cy;
        if (mode != "position") {
            k.scale = py_round(base.scale * (std::hypot(vx, vy) / len0), 5);
        }
        if (mode == "position_scale_rotation") {
            const double deg = (std::atan2(vy, vx) - ang0) * kRadToDeg;
            k.rotation = py_round(base.rotation + py_mod(deg + 180, 360) - 180, 3);
        }
        out.push_back(k);
    }
    return out;
}

} // namespace gc::core
