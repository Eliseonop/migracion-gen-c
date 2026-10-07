#include "gc/core/pose.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "gc/core/keyframes.hpp"
#include "json_reader.hpp"
#include "py.hpp"

namespace gc::core {

using detail::py_iter;
using detail::py_num;

namespace {

struct TrackKey {
    double t;
    double v;
    bool direct;
};

const Json& field(const Json& obj, std::string_view key) {
    static const Json kNull;
    if (!obj.is_object()) {
        return kNull;
    }
    const auto it = obj.find(key);
    return it == obj.end() ? kNull : *it;
}

std::vector<TrackKey> normalize_track(const Json& kfs) {
    std::vector<TrackKey> out;
    if (!detail::py_truthy(kfs)) {
        return out;
    }
    constexpr double nan = std::numeric_limits<double>::quiet_NaN();
    for (const Json& k : py_iter(kfs).value_or(std::vector<Json>{})) {
        if (!k.is_object()) {
            continue;
        }
        const double t = py_num(field(k, "t"), nan);
        const double v = py_num(field(k, "v"), nan);
        if (std::isnan(t) || std::isnan(v)) {
            continue;
        }
        out.push_back({t, v, field(k, "ease") == "direct"});
    }
    std::ranges::stable_sort(out, {}, &TrackKey::t);
    return out;
}

double clamp(double v, double lo, double hi) { return std::min(hi, std::max(lo, v)); }

double kf_zoom(const Keyframe& k, double fallback) { return clamp(k.zoom ? *k.zoom : fallback, 0.1, 1.0); }

std::string kf_mode(const Keyframe& k, const std::string& fallback) {
    const std::string& m = k.pan_mode ? *k.pan_mode : fallback;
    return m == "direct" ? "direct" : "smooth";
}

std::string kf_fit(const Keyframe& k) { return k.fit && *k.fit == "contain" ? "contain" : "cover"; }

Frame frame_of(const Keyframe& k, double fallback_zoom, const std::string& fallback_mode) {
    return {k.cx, k.cy, kf_zoom(k, fallback_zoom), kf_mode(k, fallback_mode), kf_fit(k)};
}

} // namespace

double interp_track(const Json& track, double t, double fallback) {
    const auto s = normalize_track(track);
    const double fb = std::isnan(fallback) ? 0.0 : fallback;
    if (s.empty()) {
        return fb;
    }
    const double time = std::isnan(t) ? 0.0 : t;
    if (time <= s.front().t) {
        return s.front().v;
    }
    if (time >= s.back().t) {
        return s.back().v;
    }
    for (std::size_t i = 0; i + 1 < s.size(); ++i) {
        const TrackKey& a = s[i];
        const TrackKey& b = s[i + 1];
        if (a.t <= time && time <= b.t) {
            if (b.direct) {
                return a.v;
            }
            const double span = b.t - a.t;
            const double f = (time - a.t) / (span != 0.0 ? span : 1.0);
            return a.v + (b.v - a.v) * f;
        }
    }
    return s.back().v;
}

Pose static_pose(const TimelineClip& clip) {
    const KfProps p = static_props(clip);
    return {p.get("x"), p.get("y"), p.get("scale"), p.get("rotation"), p.get("opacity")};
}

Pose clip_pose(const TimelineClip& clip, double local_t) {
    if (keyframes_enabled(clip)) {
        const KfProps p = clip_props_at(clip, local_t);
        return {p.get("x"), p.get("y"), p.get("scale"), p.get("rotation"), p.get("opacity")};
    }
    const Pose base = static_pose(clip);
    const Json anim = clip.anim && clip.anim->is_object() ? *clip.anim : Json::object();
    return {interp_track(field(anim, "x"), local_t, base.x), interp_track(field(anim, "y"), local_t, base.y),
            interp_track(field(anim, "scale"), local_t, base.scale),
            interp_track(field(anim, "rotation"), local_t, base.rotation),
            interp_track(field(anim, "opacity"), local_t, base.opacity)};
}

Frame frame_at(const std::vector<Keyframe>& keyframes, double time, double fallback_zoom,
               const std::string& fallback_mode) {
    if (keyframes.empty()) {
        return {0.5, 0.5, fallback_zoom, fallback_mode, "cover"};
    }
    std::vector<const Keyframe*> s;
    for (const auto& k : keyframes) {
        s.push_back(&k);
    }
    std::ranges::stable_sort(s, {}, [](const Keyframe* k) { return k->t; });
    if (time <= s.front()->t) {
        return frame_of(*s.front(), fallback_zoom, fallback_mode);
    }
    if (time >= s.back()->t) {
        return frame_of(*s.back(), fallback_zoom, fallback_mode);
    }
    for (std::size_t i = 0; i + 1 < s.size(); ++i) {
        const Keyframe& a = *s[i];
        const Keyframe& b = *s[i + 1];
        if (a.t <= time && time <= b.t) {
            const double za = kf_zoom(a, fallback_zoom);
            const double zb = kf_zoom(b, fallback_zoom);
            if (kf_mode(b, fallback_mode) == "direct") {
                return {a.cx, a.cy, za, "direct", kf_fit(a)};
            }
            const double span = b.t - a.t;
            const double f = (time - a.t) / (span != 0.0 ? span : 1.0);
            return {a.cx + (b.cx - a.cx) * f, a.cy + (b.cy - a.cy) * f, za + (zb - za) * f, "smooth", kf_fit(b)};
        }
    }
    return frame_of(*s.back(), fallback_zoom, fallback_mode);
}

} // namespace gc::core
