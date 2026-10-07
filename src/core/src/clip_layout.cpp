#include "gc/core/clip_layout.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string_view>
#include <utility>

#include "gc/core/keyframes.hpp"
#include "gc/core/pose.hpp"
#include "json_reader.hpp"
#include "py.hpp"

namespace gc::core {

using detail::py_float;
using detail::py_truthy;

namespace {

// min(hi, max(lo, v)) de Python, en ese orden (con NaN gana `lo`).
double clamp(double v, double lo, double hi) { return std::min(hi, std::max(lo, v)); }

// int(round(x)) de Python: empates al par; NaN e infinito son error.
Result<std::int64_t> round_int(double x) {
    if (!std::isfinite(x) || std::abs(x) >= 9.0e18) {
        return fail(ErrorCode::InvalidArgument, "el rectángulo de destino no es finito");
    }
    return static_cast<std::int64_t>(std::nearbyint(x));
}

// `_even`: un TAMAÑO redondeado a par, nunca por debajo de 2.
Result<std::int64_t> even_size(double n) {
    auto r = round_int(n);
    if (!r) {
        return r;
    }
    return *r >= 2 ? *r - *r % 2 : 2;
}

// `_even_pos`: una POSICIÓN redondeada a par; puede ser negativa o menor que 2.
Result<std::int64_t> even_pos(double n) {
    auto r = round_int(n / 2);
    if (!r) {
        return r;
    }
    return *r * 2;
}

// float(t.get(key) or fallback): lo falso (ausente, 0, "", null) vale `fallback`.
Result<double> transform_num(const Json& t, std::string_view key, double fallback) {
    if (!t.is_object()) {
        return fallback;
    }
    const auto it = t.find(key);
    if (it == t.end() || !py_truthy(*it)) {
        return fallback;
    }
    if (const auto f = py_float(*it)) {
        return *f;
    }
    const std::string path = "transform." + std::string(key);
    return fail(ErrorCode::InvalidArgument, path + " debe ser un número", path);
}

} // namespace

bool is_overlay(const TimelineClip& clip) { return clip.layout == "overlay"; }

Flip clip_flip(const TimelineClip& clip) { return {clip.flip_h, clip.flip_v}; }

std::string flip_filters(const TimelineClip& clip) {
    if (clip.kind == "shape" || clip.kind == "text") {
        return {};
    }
    std::string out = clip.flip_h ? "hflip" : "";
    if (clip.flip_v) {
        out += out.empty() ? "vflip" : ",vflip";
    }
    return out;
}

SourceCrop source_crop_px(double crop_w, double crop_h, double cx, double cy, double src_w, double src_h) {
    const double sw = crop_w * src_w;
    const double sh = crop_h * src_h;
    return {clamp((cx - crop_w / 2) * src_w, 0.0, std::max(0.0, src_w - sw)),
            clamp((cy - crop_h / 2) * src_h, 0.0, std::max(0.0, src_h - sh)), sw, sh};
}

Result<DestRect> dest_rect(const Json& transform, double crop_sw, double crop_sh, double out_w, double out_h) {
    if (py_truthy(transform) && !transform.is_object()) {
        return fail(ErrorCode::InvalidArgument, "transform debe ser un objeto", "transform");
    }
    const auto scale = transform_num(transform, "scale", 1.0);
    if (!scale) {
        return std::unexpected(scale.error());
    }
    const double dw = crop_sw * *scale;
    const double dh = crop_sh * *scale;
    const auto x = transform_num(transform, "x", 0.5);
    if (!x) {
        return std::unexpected(x.error());
    }
    const auto y = transform_num(transform, "y", 0.5);
    if (!y) {
        return std::unexpected(y.error());
    }
    const auto rot = transform_num(transform, "rotation", 0.0);
    if (!rot) {
        return std::unexpected(rot.error());
    }
    return DestRect{*x * out_w - dw / 2, *y * out_h - dh / 2, dw, dh, *rot};
}

Result<DestRectEven> dest_rect_even(const Json& transform, double crop_sw, double crop_sh, double out_w,
                                    double out_h) {
    const auto r = dest_rect(transform, crop_sw, crop_sh, out_w, out_h);
    if (!r) {
        return std::unexpected(r.error());
    }
    const auto dx = even_pos(r->dx);
    const auto dy = even_pos(r->dy);
    const auto dw = even_size(r->dw);
    const auto dh = even_size(r->dh);
    for (const auto* v : {&dx, &dy, &dw, &dh}) {
        if (!*v) {
            return std::unexpected(v->error());
        }
    }
    return DestRectEven{*dx, *dy, std::max<std::int64_t>(2, *dw), std::max<std::int64_t>(2, *dh), r->rotation};
}

CropWindow crop_window(const TimelineClip& clip, double src_aspect, double out_aspect, double src_t,
                       double local_t) {
    static const Reframe kNoReframe{};
    static const std::string kSmooth = "smooth";
    const Reframe& rf = clip.reframe ? *clip.reframe : kNoReframe;
    Frame fr = frame_at(rf.keyframes, src_t, rf.zoom != 0.0 ? rf.zoom : 1.0, rf.pan_mode.empty() ? kSmooth : rf.pan_mode);

    // Con keyframes, cx/cy/zoom animados mandan sobre el reframe (y siempre cubren).
    if (keyframes_enabled(clip)) {
        KfProps base = static_props(clip);
        base.set("cx", fr.cx);
        base.set("cy", fr.cy);
        base.set("zoom", fr.zoom);
        if (const auto p = clip_props_over(clip, local_t, base)) {
            fr.cx = p->get("cx");
            fr.cy = p->get("cy");
            fr.zoom = p->get("zoom");
            fr.fit = "cover";
        }
    }

    if (is_overlay(clip) && rf.crop_w && rf.crop_h) {
        const double w = clamp(*rf.crop_w, 0.05, 1.0);
        const double h = clamp(*rf.crop_h, 0.05, 1.0);
        return {w >= 1 ? 0.5 : clamp(fr.cx, w / 2, 1 - w / 2), h >= 1 ? 0.5 : clamp(fr.cy, h / 2, 1 - h / 2), w, h};
    }
    if (fr.fit == "contain") {
        return {0.5, 0.5, 1.0, 1.0};
    }
    const double hf = clamp(fr.zoom, 0.1, 1.0);
    const double wf = std::min(1.0, hf * out_aspect / src_aspect);
    return {wf >= 1 ? 0.5 : clamp(fr.cx, wf / 2, 1 - wf / 2), hf >= 1 ? 0.5 : clamp(fr.cy, hf / 2, 1 - hf / 2), wf,
            hf};
}

Point source_point_to_output(const TimelineClip& clip, double nx, double ny, double src_w, double src_h,
                             double out_w, double out_h, double src_t, double local_t) {
    const CropWindow crop = crop_window(clip, src_w / src_h, out_w / out_h, src_t, local_t);
    const double sw = crop.wf * src_w;
    const double sh = crop.hf * src_h;
    const double sx = clamp((crop.cx - crop.wf / 2) * src_w, 0.0, std::max(0.0, src_w - sw));
    const double sy = clamp((crop.cy - crop.hf / 2) * src_h, 0.0, std::max(0.0, src_h - sh));
    const Pose pose = clip_pose(clip, local_t);
    const double s = pose.scale;
    const bool overlay = is_overlay(clip);
    const double dw = overlay ? sw * s : out_w * s;
    const double dh = overlay ? sh * s : out_h * s;
    double rx = ((nx * src_w - sx) / (sw != 0.0 ? sw : 1.0) - 0.5) * dw;
    double ry = ((ny * src_h - sy) / (sh != 0.0 ? sh : 1.0) - 0.5) * dh;
    const Flip flip = clip_flip(clip);
    if (flip.h) {
        rx = -rx;
    }
    if (flip.v) {
        ry = -ry;
    }
    // math.radians(pose["rotation"] or 0) = x · (π/180)
    const double a = (pose.rotation != 0.0 ? pose.rotation : 0.0) * (std::numbers::pi / 180.0);
    return {(pose.x * out_w + rx * std::cos(a) - ry * std::sin(a)) / out_w,
            (pose.y * out_h + rx * std::sin(a) + ry * std::cos(a)) / out_h};
}

} // namespace gc::core
