#include "gc/core/frame.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>

#include "gc/core/blend.hpp"
#include "gc/core/clip_adjust.hpp"
#include "gc/core/fx_anims.hpp"
#include "gc/core/keyframes.hpp"
#include "gc/core/masks.hpp"
#include "gc/core/timeline_rules.hpp"

namespace gc::core {

namespace {

// Math.min(hi, Math.max(lo, v)) de JS (clamp de panning.js).
double clamp(double v, double lo, double hi) { return std::min(hi, std::max(lo, v)); }

struct Rect {
    double dx = 0.0;
    double dy = 0.0;
    double dw = 0.0;
    double dh = 0.0;
};

struct Draw {
    SourceCrop src;
    Rect dst;
};

// drawImage(img, sx, sy, sw, sh, dx, dy, dw, dh) con la transformación `m` del contexto.
Blit blit(const Affine& m, const SourceCrop& s, const Rect& d) {
    const Affine k{d.dw / s.sw, 0.0, 0.0, d.dh / s.sh, d.dx - s.sx * d.dw / s.sw, d.dy - s.sy * d.dh / s.sh};
    return {s, m * k, {d.dx, d.dy, d.dw, d.dh}, m};
}

std::array<Point, 4> quad(const Affine& m, double x, double y, double w, double h) {
    return {apply(m, x, y), apply(m, x + w, y), apply(m, x + w, y + h), apply(m, x, y + h)};
}

// sourceDrawRect de panning.js: recorte de la fuente y destino dentro de `area` para un
// encuadre `fr` (contain: la fuente entera encajada; cover: ventana del aspecto del hueco).
Draw source_draw_rect(const Frame& fr, double src_w, double src_h, const Rect& area, double slot_aspect) {
    const double src_aspect = src_w / std::max(1.0, src_h);
    if (fr.fit == "contain") {
        // containDest de recipeLayout.js
        const double scale = std::min(area.dw / std::max(1.0, src_w), area.dh / std::max(1.0, src_h));
        const double dw = src_w * scale;
        const double dh = src_h * scale;
        return {{0.0, 0.0, src_w, src_h}, {area.dx + (area.dw - dw) / 2, area.dy + (area.dh - dh) / 2, dw, dh}};
    }
    const double hf = clamp(fr.zoom, 0.1, 1.0);
    const double wf = std::min(1.0, (hf * slot_aspect) / src_aspect);
    const double cx = wf >= 1 ? 0.5 : clamp(fr.cx, wf / 2, 1 - wf / 2);
    const double cy = hf >= 1 ? 0.5 : clamp(fr.cy, hf / 2, 1 - hf / 2);
    const double sw = wf * src_w;
    const double sh = hf * src_h;
    return {{clamp((cx - wf / 2) * src_w, 0.0, std::max(0.0, src_w - sw)),
             clamp((cy - hf / 2) * src_h, 0.0, std::max(0.0, src_h - sh)), sw, sh},
            area};
}

// splitOrientationFor de recipeLayout.js (= recipe_layout.split_orientation_for).
std::string_view split_orientation(double out_aspect, const Reframe& rf) {
    if (rf.split_layout == "vertical" || rf.split_layout == "horizontal") {
        return rf.split_layout;
    }
    if (rf.split_layout != "auto" && (rf.split_orientation == "vertical" || rf.split_orientation == "horizontal")) {
        return rf.split_orientation;
    }
    return out_aspect < 1 ? "vertical" : "horizontal";
}

// drawReframe de panning.js para un clip de relleno: los dibujos dentro de `area` (el
// cuadro entero), antes de la pose. reframeForDraw: con keyframes (y sin doble encuadre)
// el recorte sale de la pose cx/cy/zoom y siempre cubre.
std::vector<Draw> fill_draws(const TimelineClip& clip, double vw, double vh, double out_w, double out_h,
                             double src_t, double local_t) {
    static const Reframe kNoReframe{};
    const Reframe& rf = clip.reframe ? *clip.reframe : kNoReframe;
    const double out_aspect = out_w / out_h;
    const Rect area{0.0, 0.0, out_w, out_h};
    const std::string mode = rf.pan_mode.empty() ? "smooth" : rf.pan_mode;
    if (!rf.dual_crop) {
        if (keyframes_enabled(clip)) {
            const CropWindow w = crop_window(clip, vw / vh, out_aspect, src_t, local_t);
            return {{source_crop_px(w.wf, w.hf, w.cx, w.cy, vw, vh), area}};
        }
        return {source_draw_rect(frame_at(rf.keyframes, src_t, rf.zoom, mode), vw, vh, area, out_aspect)};
    }
    const std::string_view orient = split_orientation(out_aspect, rf);
    const double target = orient == "horizontal" ? out_aspect / 2 : out_aspect * 2;
    const double z2 = rf.zoom2.value_or(rf.zoom);
    const auto& kfs2 = rf.keyframes2.empty() ? rf.keyframes : rf.keyframes2;
    const Rect first = orient == "vertical" ? Rect{0.0, 0.0, out_w, out_h / 2} : Rect{0.0, 0.0, out_w / 2, out_h};
    const Rect second =
        orient == "vertical" ? Rect{0.0, out_h / 2, out_w, out_h / 2} : Rect{out_w / 2, 0.0, out_w / 2, out_h};
    return {source_draw_rect(frame_at(rf.keyframes, src_t, rf.zoom, mode), vw, vh, first, target),
            source_draw_rect(frame_at(kfs2, src_t, z2, mode), vw, vh, second, target)};
}

bool has_media(const TimelineClip& clip) {
    return clip.kind == "video" || clip.kind == "image" || clip.kind == "motion";
}

// drawOverlayLayer y la rama de relleno de drawComposite (canvas.js).
void media_blits(FrameLayer& layer, double vw, double vh, double out_w, double out_h, bool preview) {
    const TimelineClip& clip = *layer.clip;
    const Pose& pose = layer.pose;
    const ClipFx& fx = layer.fx;
    const double fh = layer.flip.h ? -1.0 : 1.0;
    const double fv = layer.flip.v ? -1.0 : 1.0;
    if (is_overlay(clip)) {
        const CropWindow w = crop_window(clip, vw / vh, out_w / out_h, layer.src_t, layer.local_t);
        const SourceCrop px = source_crop_px(w.wf, w.hf, w.cx, w.cy, vw, vh);
        // destRect de clipLayout.js con posedRenderTransform (números de la pose tal cual).
        const double dw = px.sw * pose.scale;
        const double dh = px.sh * pose.scale;
        const double dx = pose.x * out_w - dw / 2;
        const double dy = pose.y * out_h - dh / 2;
        const double rot = std::isnan(pose.rotation) ? 0.0 : pose.rotation; // `rotation || 0`
        Affine m;
        if (fx.wipe < 1) {
            layer.wipe = quad(m, dx, dy, dw * std::max(0.0, fx.wipe), dh);
        }
        m = translated(m, dx + dw / 2 + fx.tx * dw, dy + dh / 2 + fx.ty * dh);
        m = rotated(m, rot * std::numbers::pi / 180);
        m = scaled(m, fx.scale * fh, fx.scale * fv);
        layer.blits.push_back(blit(m, px, {-dw / 2, -dh / 2, dw, dh}));
        return;
    }
    Affine m;
    if (fx.wipe < 1) {
        layer.wipe = quad(m, 0.0, 0.0, out_w * std::max(0.0, fx.wipe), out_h);
    }
    // applyCanvasFx
    m = translated(m, out_w / 2 + fx.tx * out_w, out_h / 2 + fx.ty * out_h);
    m = scaled(m, fx.scale, fx.scale);
    m = translated(m, -out_w / 2, -out_h / 2);
    // La pose sobre el cuadro entero. El preview se la salta si solo cambia la escala en
    // 0,001 o menos.
    const double dx = (pose.x - 0.5) * out_w;
    const double dy = (pose.y - 0.5) * out_h;
    const double rot = std::isnan(pose.rotation) ? 0.0 : pose.rotation;
    const double sc = pose.scale;
    if (!preview || dx != 0 || dy != 0 || rot != 0 || std::abs(sc - 1) > 0.001 || layer.flip.h || layer.flip.v) {
        m = translated(m, out_w / 2 + dx, out_h / 2 + dy);
        m = rotated(m, rot * std::numbers::pi / 180);
        m = scaled(m, sc * fh, sc * fv);
        m = translated(m, -out_w / 2, -out_h / 2);
    }
    for (const Draw& d : fill_draws(clip, vw, vh, out_w, out_h, layer.src_t, layer.local_t)) {
        layer.blits.push_back(blit(m, d.src, d.dst));
    }
}

bool positive_finite(double v) { return std::isfinite(v) && v > 0; }

} // namespace

Affine operator*(const Affine& m, const Affine& n) {
    return {m.a * n.a + m.c * n.b, m.b * n.a + m.d * n.b, m.a * n.c + m.c * n.d,
            m.b * n.c + m.d * n.d, m.a * n.e + m.c * n.f + m.e, m.b * n.e + m.d * n.f + m.f};
}

Affine translated(const Affine& m, double x, double y) { return m * Affine{1.0, 0.0, 0.0, 1.0, x, y}; }

Affine rotated(const Affine& m, double radians) {
    const double c = std::cos(radians);
    const double s = std::sin(radians);
    return m * Affine{c, s, -s, c, 0.0, 0.0};
}

Affine scaled(const Affine& m, double x, double y) { return m * Affine{x, 0.0, 0.0, y, 0.0, 0.0}; }

Point apply(const Affine& m, double x, double y) { return {m.a * x + m.c * y + m.e, m.b * x + m.d * y + m.f}; }

double source_time(const TimelineClip& clip, double t, double start) {
    const double local = (t - start) * clip_speed(clip);
    const double src = clip.reverse ? clip.out_point - local : clip.in_point + local;
    return clamp(src, clip.in_point, clip.out_point);
}

Result<std::vector<FrameLayer>> evaluate(const Timeline& timeline, double t, const FrameSpec& spec,
                                         const MediaSizeFn& media) {
    if (!positive_finite(spec.out_w) || !positive_finite(spec.out_h)) {
        return fail(ErrorCode::InvalidArgument, "las dimensiones de salida deben ser mayores que 0");
    }
    const bool preview = !spec.rules.export_rules;
    std::vector<FrameLayer> out;
    for (const Cow<TimelineClip>& c : layers_at(timeline, t, spec.rules)) {
        const double start = preview ? c->start : std::max(0.0, c->start);
        const double local_t = std::max(0.0, t - start);
        const double src_t = source_time(*c, t, start);
        const double dur = clip_timeline_duration(*c);
        std::optional<MediaSize> size;
        if (has_media(*c) && media) {
            size = media(*c);
            if (size && (!positive_finite(size->w) || !positive_finite(size->h))) {
                if (!preview) {
                    return fail(ErrorCode::InvalidArgument,
                                "el material del clip " + c->id + " no tiene dimensiones válidas");
                }
                size.reset();
            }
        }
        // El texto no pasa por adjust_passes (drawComposite lo dibuja aparte).
        std::vector<ClipPass> passes;
        if (c->kind == "text") {
            passes.push_back({c, false});
        } else {
            passes = adjust_passes(c);
        }
        for (const ClipPass& pass : passes) {
            FrameLayer layer;
            layer.clip = pass.clip;
            layer.ghost = pass.ghost;
            layer.local_t = local_t;
            layer.src_t = src_t;
            layer.pose = render_pose(*pass.clip, local_t);
            layer.fx = clip_fx_at(*pass.clip, local_t, dur);
            layer.alpha = layer.fx.opacity * layer.pose.opacity;
            layer.blend = clip_blend(*pass.clip);
            layer.flip = clip_flip(*pass.clip);
            layer.masks = clip_masks_at(*pass.clip, local_t);
            if (size) {
                media_blits(layer, size->w, size->h, spec.out_w, spec.out_h, preview);
            }
            out.push_back(std::move(layer));
        }
    }
    return out;
}

} // namespace gc::core
