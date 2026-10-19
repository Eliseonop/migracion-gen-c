#include "gc/core/clip_layout_ui.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <tuple>

#include "gc/core/clip_pose_edit.hpp"
#include "gc/core/keyframes.hpp"
#include "gc/core/serialize.hpp"
#include "gc/core/timeline_view.hpp"
#include "gc/core/timeline_ui.hpp"

namespace gc::core {

namespace {

constexpr double kMainFrameFrac = 1; // MAIN_FRAME_FRAC
constexpr double kPosMin = -10;      // CLIP_POS_MIN
constexpr double kPosMax = 11;       // CLIP_POS_MAX

double clamp(double v, double lo, double hi) { return std::min(hi, std::max(lo, v)); }

std::optional<Rect> slot_of(std::string_view frame) {
    if (frame == "top") {
        return Rect{0.5, 0.25, 1, 0.5};
    }
    if (frame == "bottom") {
        return Rect{0.5, 0.75, 1, 0.5};
    }
    if (frame == "left") {
        return Rect{0.25, 0.5, 0.5, 1};
    }
    if (frame == "right") {
        return Rect{0.75, 0.5, 0.5, 1};
    }
    return std::nullopt;
}

CropWindow clamp_center(double cx, double cy, double zoom, double src_aspect, double out_aspect) {
    const double hf = clamp(zoom, 0.1, 1);
    const double wf = std::min(1.0, hf * out_aspect / src_aspect);
    return {wf >= 1 ? 0.5 : clamp(cx, wf / 2, 1 - wf / 2), hf >= 1 ? 0.5 : clamp(cy, hf / 2, 1 - hf / 2), wf, hf};
}

double js_round(double v) { // Math.round
    const double r = std::floor(v);
    return v - r >= 0.5 ? r + 1 : r;
}

constexpr double kTextScaleMax = 100; // TEXT_SCALE_MAX

bool visual_kind(const TimelineClip& c) { return c.kind == "video" || c.kind == "image"; }
bool fills(const TimelineClip& c) { return visual_kind(c) && !is_overlay(c); }
bool height_fit(const TimelineClip& c) { return visual_kind(c) && is_overlay(c); }
double hs_of(const TimelineClip& c, double height_scale) { return height_fit(c) && height_scale > 0 ? height_scale : 1; }

double zoom_to_scale(double z) { return std::isfinite(z) && z > 0 ? js_fixed(clamp(1 / z, 1, 10), 4) : 1; }
double scale_to_zoom(double s) { return std::isfinite(s) && s > 0 ? js_fixed(clamp(1 / s, 0.1, 1), 4) : 1; }

struct Point2 {
    double x;
    double y;
};

} // namespace

std::string frame_of(const TimelineClip& clip) { return slot_of(clip.frame) ? clip.frame : "full"; }

double slot_aspect_of(const TimelineClip& clip, double out_aspect) {
    const auto slot = slot_of(clip.frame);
    return slot ? slot->w * out_aspect / slot->h : out_aspect;
}

std::pair<double, double> to_capcut_pos(double x, double y, double w, double h) {
    return {js_round((x - 0.5) * 2 * w), js_round((0.5 - y) * 2 * h)};
}

std::pair<double, double> from_capcut_pos(double X, double Y, double w, double h) {
    return {js_fixed(clamp(0.5 + X / (2 * w), kPosMin, kPosMax), 6),
            js_fixed(clamp(0.5 - Y / (2 * h), kPosMin, kPosMax), 6)};
}

Rect frame_rect_of(double cw, double ch, double view_zoom, double out_aspect) {
    const double frac = kMainFrameFrac * (std::isfinite(view_zoom) && view_zoom > 0 ? view_zoom : 1);
    const double a = out_aspect > 0 ? out_aspect : cw / ch;
    double h = ch * frac;
    double w = h * a;
    if (w > cw * frac) {
        w = cw * frac;
        h = w / a;
    }
    return {(cw - w) / 2, (ch - h) / 2, w, h};
}

DestRect dest_rect_ui(const Pose& t, const SourceCrop& crop, double out_w, double out_h) {
    const double dw = crop.sw * t.scale;
    const double dh = crop.sh * t.scale;
    return {t.x * out_w - dw / 2, t.y * out_h - dh / 2, dw, dh, t.rotation};
}

DestRect dest_rect_on_canvas(const Pose& transform, const SourceCrop& crop, double out_w, double out_h,
                             double canvas_w, double canvas_h) {
    const DestRect d = dest_rect_ui(transform, crop, out_w, out_h);
    const double sx = canvas_w / out_w;
    const double sy = canvas_h / out_h;
    return {d.dx * sx, d.dy * sy, d.dw * sx, d.dh * sy, d.rotation};
}

DestRect dest_rect_on_frame(const Pose& transform, const SourceCrop& crop, double out_w, double out_h,
                            const Rect& frame) {
    DestRect d = dest_rect_on_canvas(transform, crop, out_w, out_h, frame.w, frame.h);
    d.dx += frame.x;
    d.dy += frame.y;
    return d;
}

SourceCrop source_crop_px(const CropWindow& crop, double src_w, double src_h) {
    const double sw = crop.wf * src_w;
    const double sh = crop.hf * src_h;
    return {clamp((crop.cx - crop.wf / 2) * src_w, 0, std::max(0.0, src_w - sw)),
            clamp((crop.cy - crop.hf / 2) * src_h, 0, std::max(0.0, src_h - sh)), sw, sh};
}

CropWindow clamp_crop(double cx, double cy, double wf, double hf) {
    const double w = clamp(wf, 0.05, 1);
    const double h = clamp(hf, 0.05, 1);
    return {w >= 1 ? 0.5 : clamp(cx, w / 2, 1 - w / 2), h >= 1 ? 0.5 : clamp(cy, h / 2, 1 - h / 2), w, h};
}

std::pair<double, double> crop_size_from_corner(double nx, double ny, double cx, double cy) {
    return {clamp(std::abs(nx - cx) * 2, 0.05, 1), clamp(std::abs(ny - cy) * 2, 0.05, 1)};
}

std::array<CropHandle, 8> crop_handle_norms(const CropWindow& c) {
    const std::array xs{c.cx - c.wf / 2, c.cx, c.cx + c.wf / 2};
    const std::array ys{c.cy - c.hf / 2, c.cy, c.cy + c.hf / 2};
    std::array<CropHandle, 8> out{};
    std::size_t n = 0;
    for (int iy = 0; iy < 3; ++iy) {
        for (int ix = 0; ix < 3; ++ix) {
            if (ix != 1 || iy != 1) {
                out.at(n++) = {ix - 1, iy - 1, xs.at(ix), ys.at(iy)};
            }
        }
    }
    return out;
}

std::optional<std::pair<int, int>> crop_handle_at(double nx, double ny, const CropWindow& crop, double rect_w,
                                                  double rect_h, double px) {
    std::optional<std::pair<int, int>> best;
    double best_d = px * px;
    for (const CropHandle& h : crop_handle_norms(crop)) {
        const double dx = (nx - h.x) * (rect_w != 0 && !std::isnan(rect_w) ? rect_w : 1);
        const double dy = (ny - h.y) * (rect_h != 0 && !std::isnan(rect_h) ? rect_h : 1);
        const double d = dx * dx + dy * dy;
        if (d <= best_d) {
            best_d = d;
            best = std::pair{h.hx, h.hy};
        }
    }
    return best;
}

bool inside_crop(double nx, double ny, const CropWindow& c) {
    return nx >= c.cx - c.wf / 2 && nx <= c.cx + c.wf / 2 && ny >= c.cy - c.hf / 2 && ny <= c.cy + c.hf / 2;
}

CropWindow resize_crop_free(const CropWindow& start, int hx, int hy, double nx, double ny, double min) {
    double left = start.cx - start.wf / 2;
    double right = start.cx + start.wf / 2;
    double top = start.cy - start.hf / 2;
    double bottom = start.cy + start.hf / 2;
    if (hx < 0) {
        left = clamp(std::min(nx, right - min), 0, right - min);
    } else if (hx > 0) {
        right = clamp(std::max(nx, left + min), left + min, 1);
    }
    if (hy < 0) {
        top = clamp(std::min(ny, bottom - min), 0, bottom - min);
    } else if (hy > 0) {
        bottom = clamp(std::max(ny, top + min), top + min, 1);
    }
    const double wf = right - left;
    const double hf = bottom - top;
    return {left + wf / 2, top + hf / 2, wf, hf};
}

CropWindow resize_crop_locked(const CropWindow& start, int hx, int hy, double nx, double ny, double r, double min) {
    const double left = start.cx - start.wf / 2;
    const double right = start.cx + start.wf / 2;
    const double top = start.cy - start.hf / 2;
    const double bottom = start.cy + start.hf / 2;
    const std::optional<double> from_w =
        hx < 0 ? std::optional((right - nx) / r) : hx > 0 ? std::optional((nx - left) / r) : std::nullopt;
    const std::optional<double> want_h =
        hy < 0 ? std::optional(bottom - ny) : hy > 0 ? std::optional(ny - top) : std::nullopt;
    double hf = start.hf;
    if (from_w && want_h) {
        hf = std::max(*from_w, *want_h);
    } else if (from_w) {
        hf = *from_w;
    } else if (want_h) {
        hf = *want_h;
    }
    hf = clamp(hf, min, 1);
    double wf = hf * r;
    if (wf > 1) {
        wf = 1;
        hf = wf / r;
    }
    return {hx < 0 ? right - wf / 2 : hx > 0 ? left + wf / 2 : start.cx,
            hy < 0 ? bottom - hf / 2 : hy > 0 ? top + hf / 2 : start.cy, wf, hf};
}

std::optional<double> crop_ratio_norm(std::string_view ratio_id, double src_aspect, double out_aspect) {
    struct Ratio {
        std::string_view id;
        double aspect; // 0 = libre, -1 = el del material, -2 = el del proyecto
    };
    constexpr std::array<Ratio, 10> kRatios{{{"free", 0},
                                              {"original", -1},
                                              {"output", -2},
                                              {"16:9", 16.0 / 9},
                                              {"9:16", 9.0 / 16},
                                              {"1:1", 1},
                                              {"4:3", 4.0 / 3},
                                              {"3:4", 3.0 / 4},
                                              {"4:5", 4.0 / 5},
                                              {"2.35:1", 2.35}}};
    const auto* r = std::ranges::find(kRatios, ratio_id, &Ratio::id);
    if (r == kRatios.end() || r->aspect == 0 || !(src_aspect > 0)) {
        return std::nullopt;
    }
    const double aspect = r->aspect == -1 ? src_aspect : r->aspect == -2 ? out_aspect : r->aspect;
    return aspect > 0 ? std::optional(aspect / src_aspect) : std::nullopt;
}

CropWindow fit_crop_ratio(const CropWindow& crop, double rn) {
    const bool wide = rn >= 1;
    return clamp_crop(crop.cx, crop.cy, wide ? 1 : rn, wide ? 1 / rn : 1);
}

std::string crop_cursor(int hx, int hy) {
    if (hx != 0 && hy != 0) {
        return hx == hy ? "nwse-resize" : "nesw-resize";
    }
    return hx != 0 ? "ew-resize" : hy != 0 ? "ns-resize" : "move";
}

Frame free_frame_at(const TimelineClip& clip, double t, double src_w, double src_h, double out_w, double out_h) {
    const double src_aspect = src_w / src_h;
    const double out_aspect = out_w / out_h;
    const double local = t * ui_clip_speed(clip);
    const double src_t = clamp(clip.reverse ? clip.out_point - local : clip.in_point + local, clip.in_point,
                               clip.out_point);
    const CropWindow crop = crop_window(clip, src_aspect, out_aspect, src_t, t);
    const DestRect d = dest_rect_ui(clip_pose(clip, t), source_crop_px(crop, src_w, src_h), out_w, out_h);
    if (!(d.dw > 0) || !(d.dh > 0)) {
        return {};
    }
    const double zoom = clamp(out_h / d.dh * crop.hf, 0.1, 1);
    const double cx = crop.cx - crop.wf / 2 + (out_w / 2 - d.dx) / d.dw * crop.wf;
    const double cy = crop.cy - crop.hf / 2 + (out_h / 2 - d.dy) / d.dh * crop.hf;
    const CropWindow c = clamp_center(cx, cy, zoom, src_aspect, out_aspect);
    Frame out;
    out.cx = c.cx;
    out.cy = c.cy;
    out.zoom = zoom;
    return out;
}

std::optional<std::string> hit_transform_handle(double px, double py, const DestRect& dest, double pad) {
    const double cx = dest.dx + dest.dw / 2;
    const double cy = dest.dy + dest.dh / 2;
    const double rad = dest.rotation * std::numbers::pi / 180;
    const auto at = [&](double lx, double ly) {
        return Point2{cx + lx * std::cos(rad) - ly * std::sin(rad), cy + lx * std::sin(rad) + ly * std::cos(rad)};
    };
    const double hw = dest.dw / 2;
    const double hh = dest.dh / 2;
    const std::array handles{at(0, hh + kRotateGap), at(-hw, -hh), at(hw, -hh), at(-hw, hh), at(hw, hh)};
    for (std::size_t i = 0; i < handles.size(); ++i) {
        if (std::abs(px - handles.at(i).x) < pad && std::abs(py - handles.at(i).y) < pad) {
            return i == 0 ? "rotate" : "scale";
        }
    }
    const double a = -rad;
    const double x = (px - cx) * std::cos(a) - (py - cy) * std::sin(a);
    const double y = (px - cx) * std::sin(a) + (py - cy) * std::cos(a);
    return std::abs(x) <= dest.dw / 2 && std::abs(y) <= dest.dh / 2 ? std::optional<std::string>("move")
                                                                     : std::nullopt;
}

TransformView transform_view(const TimelineClip& clip, double t, double frame_w, double frame_h, double height_scale) {
    TransformView v;
    const bool keyable = clip.kind == "video" || clip.kind == "image" || clip.kind == "shape" || clip.kind == "text";
    if (!keyable) {
        return v;
    }
    const double local = std::max(0.0, t);
    const Pose pose = clip_pose(clip, local);
    const KfProps props = clip_props_at(clip, local);
    const bool fill = fills(clip);
    const bool free = is_overlay(clip) || clip.kind == "shape" || clip.kind == "text";
    v.show = true;
    v.xy = free || fill;
    v.scale = free;
    v.zoom_scale = fill;
    v.rotation = free || visual_kind(clip);
    v.capcut = !fill && frame_w > 0 && frame_h > 0;
    const double pan_x = fill ? props.get("cx") : pose.x;
    const double pan_y = fill ? props.get("cy") : pose.y;
    if (v.capcut) {
        std::tie(v.x, v.y) = to_capcut_pos(pan_x, pan_y, frame_w, frame_h);
    } else {
        v.x = js_round(pan_x * 100);
        v.y = js_round(pan_y * 100);
    }
    const double raw = fill ? zoom_to_scale(props.get("zoom"))
                       : height_fit(clip) ? pose.scale / hs_of(clip, height_scale)
                       : pose.scale != 0 && !std::isnan(pose.scale) ? pose.scale
                                         : 1;
    v.scale_pct = js_round(raw * 100);
    v.rotation_deg = js_fixed(pose.rotation, 1);
    return v;
}

Json transform_pos_patch(const TimelineClip& clip, double X, double Y, double frame_w, double frame_h) {
    const bool fill = fills(clip);
    double x = 0;
    double y = 0;
    if (!fill && frame_w > 0 && frame_h > 0) {
        std::tie(x, y) = from_capcut_pos(X, Y, frame_w, frame_h);
    } else {
        const double lo = fill ? 0 : kPosMin;
        const double hi = fill ? 1 : kPosMax;
        x = clamp(X / 100, lo, hi);
        y = clamp(Y / 100, lo, hi);
    }
    return fill ? Json{{"cx", x}, {"cy", y}} : Json{{"x", x}, {"y", y}};
}

Json transform_scale_patch(const TimelineClip& clip, double pct, double height_scale) {
    const double s = pct / 100;
    if (fills(clip)) {
        return {{"zoom", scale_to_zoom(s)}};
    }
    if (height_fit(clip)) {
        return {{"scale", js_fixed(clamp(s * hs_of(clip, height_scale), 0.0005, 100), 5)}};
    }
    return {{"scale", js_fixed(clamp(s, 0.05, clip.kind == "text" ? kTextScaleMax : 8), 4)}};
}

Json transform_reset_patch(const TimelineClip& clip, double height_scale) {
    Json p = {{"x", 0.5},  {"y", 0.5},  {"scale", hs_of(clip, height_scale)}, {"rotation", 0}, {"opacity", 1},
              {"cx", 0.5}, {"cy", 0.5}, {"zoom", 1}};
    if (clip.kind == "text") {
        p["rot_x"] = 0;
        p["rot_y"] = 0;
    }
    return p;
}

namespace {

Json new_reframe_json() {
    return {{"zoom", 1},          {"pan_mode", "smooth"}, {"dual_crop", false},       {"split_orientation", "vertical"},
            {"split_layout", "auto"}, {"master", false},  {"face_track_mode", nullptr}, {"keyframes", Json::array()},
            {"keyframes2", Json::array()}};
}

// changeReframe: {...(c.reframe || newReframe()), ...patch}.
Result<TimelineClip> change_reframe(const TimelineClip& clip, const Json& patch) {
    Json j = to_json(clip);
    Json rf = j.value("reframe", Json()).is_object() ? j["reframe"] : new_reframe_json();
    for (const auto& [key, value] : patch.items()) {
        rf[key] = value;
    }
    j["reframe"] = std::move(rf);
    return clip_from_json(j);
}

bool keyframes_on(const TimelineClip& clip) {
    return clip.keyframes && clip.keyframes->value("enabled", false) == true &&
           clip.keyframes->value("items", Json::array()).is_array() && !clip.keyframes->at("items").empty();
}

} // namespace

Result<TimelineClip> confirm_crop(const TimelineClip& clip, const CropWindow& crop, double rotation, double local_t,
                                  double src_t, double fps, IdSource& ids) {
    const Json original = to_json(clip);
    const Json rf = original.value("reframe", Json()).is_object() ? original.at("reframe") : new_reframe_json();
    const double cx = js_fixed(crop.cx, 4);
    const double cy = js_fixed(crop.cy, 4);
    const Json size = {{"crop_w", js_fixed(crop.wf, 4)}, {"crop_h", js_fixed(crop.hf, 4)}};
    const Json kfs = rf.value("keyframes", Json::array());
    TimelineClip out = clip;
    if (keyframes_on(clip) || (kfs.is_array() && kfs.size() > 1)) {
        auto sized = change_reframe(out, size);
        if (!sized) {
            return sized;
        }
        out = std::move(*sized);
        if (out.reframe && out.reframe->dual_crop) { // upsertKeyframe: keyframe del encuadre en la fuente
            Json j = to_json(out);
            Json list = j["reframe"].value("keyframes", Json::array());
            const double t = js_fixed(snap_to_frame(std::clamp(src_t, out.in_point, out.out_point), fps), 6);
            const auto hit = std::find_if(list.begin(), list.end(), [&](const Json& k) {
                return std::abs(js_number(k.value("t", Json(0))) - t) < frame_duration(fps) / 2;
            });
            if (hit != list.end()) {
                (*hit)["t"] = t;
                (*hit)["cx"] = cx;
                (*hit)["cy"] = cy;
            } else {
                list.push_back({{"id", make_uid(ids, "k")}, {"t", t}, {"cx", cx}, {"cy", cy},
                                {"zoom", j["reframe"].value("zoom", Json(1))}, {"pan_mode", "smooth"}});
            }
            std::stable_sort(list.begin(), list.end(), [](const Json& a, const Json& b) {
                return js_number(a.value("t", Json(0))) < js_number(b.value("t", Json(0)));
            });
            j["reframe"]["keyframes"] = std::move(list);
            auto back = clip_from_json(j);
            if (!back) {
                return back;
            }
            out = std::move(*back);
        } else {
            upsert_keyframe_ui(out, std::max(0.0, local_t), Json{{"cx", cx}, {"cy", cy}}, std::nullopt, fps, ids);
            apply_static_pose(out, Json{{"cx", cx}, {"cy", cy}});
        }
    } else {
        const Json k0 = kfs.is_array() && !kfs.empty() && kfs[0].is_object() ? kfs[0] : Json::object();
        const auto truthy = [](const Json& v) { return v.is_string() ? !v.get_ref<const std::string&>().empty() : !v.is_null(); };
        Json key = {{"id", truthy(k0.value("id", Json())) ? k0.at("id") : Json(make_uid(ids, "k"))},
                    {"t", clip.in_point},
                    {"cx", cx},
                    {"cy", cy},
                    {"zoom", !k0.value("zoom", Json()).is_null() ? k0.at("zoom") : !rf.value("zoom", Json()).is_null() ? rf.at("zoom") : Json(1)},
                    {"pan_mode", truthy(k0.value("pan_mode", Json())) ? k0.at("pan_mode") : Json("smooth")}};
        Json patch = size;
        patch["keyframes"] = Json::array({std::move(key)});
        auto fixed = change_reframe(out, patch);
        if (!fixed) {
            return fixed;
        }
        out = std::move(*fixed);
    }
    const double rot = js_fixed(rotation, 2);
    const Json& tr = original.value("transform", Json());
    const double current = tr.is_object() ? js_number(tr.value("rotation", Json(0))) : 0;
    if (std::abs((std::isfinite(current) ? current : 0) - rot) > 0.009) { // changeTransform
        Json j = to_json(out);
        Json t = {{"x", 0.5}, {"y", 0.5}, {"scale", 1}, {"rotation", 0}};
        if (j.value("transform", Json()).is_object()) {
            for (const auto& [key, value] : j["transform"].items()) {
                t[key] = value;
            }
        }
        t["rotation"] = rot;
        j["frame"] = "free";
        j["transform"] = std::move(t);
        auto back = clip_from_json(j);
        if (!back) {
            return back;
        }
        const Json patch = {{"rotation", rot}};
        const bool animate = should_keyframe(out, patch); // con el clip ya recortado, como setClips
        out = std::move(*back);
        if (animate) {
            upsert_keyframe_ui(out, std::max(0.0, local_t), flatten_pose_patch(patch), std::nullopt, fps, ids);
        }
    }
    return out;
}

} // namespace gc::core
