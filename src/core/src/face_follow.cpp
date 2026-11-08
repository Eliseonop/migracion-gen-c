#include "gc/core/face_follow.hpp"

#include <algorithm>
#include <cmath>

#include "gc/core/clip_layout.hpp"
#include "gc/core/clip_pose_edit.hpp"
#include "gc/core/timeline_ui.hpp"

namespace gc::core {

namespace {

// keyframesOn de lib/clipKeyframes.js: encendidos y con algún item (keyframes_enabled solo mira lo primero).
bool keyframes_on(const TimelineClip& clip) {
    if (!clip.keyframes || !clip.keyframes->is_object()) {
        return false;
    }
    // Sin iteradores de find(): con ellos, GCC 15 da un falso positivo de -Wnull-dereference en nlohmann.
    const Json& kf = *clip.keyframes;
    if (!kf.contains("enabled") || !kf.contains("items")) {
        return false;
    }
    const Json& enabled = kf.at("enabled");
    const Json& items = kf.at("items");
    const bool on = (enabled.is_boolean() && enabled.get<bool>()) || (enabled.is_number() && enabled.get<double>() != 0);
    return on && items.is_array() && items.size() > 0;
}

} // namespace

std::vector<MaskFollowKey> follow_track_mask_keys(const TimelineClip& clip, const Json& track, double src_w, double src_h,
                                              double out_w, double out_h, double min_gap) {
    const double sp = std::max(0.01, clip.speed != 0 && std::isfinite(clip.speed) ? clip.speed : 1.0);
    struct P {
        double t;
        double cx;
        double cy;
    };
    std::vector<P> pts;
    if (track.is_array()) {
        for (const Json& p : track) {
            const auto t = p.is_object() ? p.find("t") : Json::const_iterator();
            if (!p.is_object() || t == p.end() || !t->is_number() || !std::isfinite(t->get<double>())) {
                continue;
            }
            const double at = t->get<double>();
            if (at < clip.in_point - 1e-6 || at > clip.out_point + 1e-6) {
                continue;
            }
            const auto cx = p.find("cx");
            const auto cy = p.find("cy");
            pts.push_back({at, cx != p.end() && cx->is_number() ? cx->get<double>() : std::nan(""),
                           cy != p.end() && cy->is_number() ? cy->get<double>() : 0.5}); // p.cy ?? 0.5
        }
    }
    std::ranges::stable_sort(pts, {}, &P::t);
    std::vector<MaskFollowKey> out;
    for (const P& p : pts) {
        const double local = clip.reverse ? (clip.out_point - p.t) / sp : (p.t - clip.in_point) / sp;
        if (!out.empty() && local - out.back().t < min_gap) {
            continue;
        }
        const Point o = source_point_to_output(clip, p.cx, p.cy, src_w, src_h, out_w, out_h, p.t, local);
        out.push_back({js_fixed(local, 4), js_fixed(o.x, 4), js_fixed(o.y, 4)});
    }
    return out;
}

void apply_follow_keys(TimelineClip& clip, const std::vector<MaskFollowKey>& keys, double src_t, double fps,
                       IdSource& ids) {
    if (keys.empty()) {
        return;
    }
    if (!keyframes_on(clip)) {
        enable_keyframes(clip, keys.front().t, src_t, ids);
    }
    for (const MaskFollowKey& k : keys) {
        upsert_keyframe_ui(clip, k.t, {{"mx", k.mx}, {"my", k.my}}, std::nullopt, fps, ids);
    }
}

} // namespace gc::core
