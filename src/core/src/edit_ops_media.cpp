// Operaciones que crean material o lo transforman: efectos y animaciones de
// catálogo, congelar fotograma, subtítulos, figuras, barras de cine, capa de
// ajuste y eliminar fondo (espejo de backend/app/timeline_ops.py).

#include <algorithm>
#include <array>
#include <cmath>

#include "edit_ops.hpp"
#include "edit_util.hpp"
#include "gc/core/bg.hpp"
#include "gc/core/captions.hpp"
#include "gc/core/fx_catalog.hpp"
#include "gc/core/keyframes.hpp"
#include "gc/core/masks.hpp"
#include "gc/core/pose.hpp"
#include "gc/core/serialize.hpp"
#include "gc/core/shapes.hpp"
#include "gc/core/timeline_rules.hpp"

namespace gc::core::detail {

namespace {

// `[str(x) for x in (clip_ids or []) if x]`
Result<std::vector<std::string>> id_list(const Json& raw) {
    std::vector<std::string> out;
    if (!py_truthy(raw)) {
        return out;
    }
    const auto items = py_iter(raw);
    if (!items) {
        return invalid("clip_ids debe ser una lista");
    }
    for (const Json& x : *items) {
        if (py_truthy(x)) {
            out.push_back(py_str(x));
        }
    }
    return out;
}

// dict(x or {}) para fusionar
Result<Json> dict_or_empty(const Json& v, std::string_view what) {
    if (!py_truthy(v)) {
        return Json::object();
    }
    if (!v.is_object()) {
        return invalid(std::string(what) + " debe ser un objeto");
    }
    return v;
}

// Pista de vídeo de ARRIBA si está libre en [start, start+dur); si no, una nueva
// encima de todas (su id va a `created`).
std::string top_free_video_track(Timeline& out, double start, double dur, std::vector<std::string>& created,
                                 IdSource& ids) {
    const TimelineTrack* top = nullptr;
    std::size_t videos = 0;
    for (const auto& t : out.tracks) {
        if (t.kind == "video") {
            top = &t;
            ++videos;
        }
    }
    if (top != nullptr) {
        const bool busy = std::ranges::any_of(out.clips, [&](const Cow<TimelineClip>& c) {
            return c->track_id == top->id && c->start < start + dur - 1e-6 &&
                   c->start + clip_span_duration(*c) > start + 1e-6;
        });
        if (!busy && !top->locked) {
            return top->id;
        }
    }
    TimelineTrack track;
    track.id = make_uid(ids, "K");
    track.kind = "video";
    track.name = "V" + std::to_string(videos + 1);
    const std::string id = track.id;
    insert_track(out.tracks, std::move(track));
    created.push_back(id);
    return id;
}

TimelineClip new_clip(IdSource& ids, std::string track_id, std::string kind, std::string asset_kind,
                      std::string asset_id, std::string name, double start, double dur) {
    TimelineClip c;
    c.id = make_uid(ids, "c");
    c.track_id = std::move(track_id);
    c.kind = std::move(kind);
    c.asset_kind = std::move(asset_kind);
    c.asset_id = std::move(asset_id);
    c.filename = "";
    if (!name.empty()) {
        c.name = std::move(name);
    }
    c.start = start;
    c.in_point = 0.0;
    c.out_point = dur;
    c.source_duration = dur;
    return c;
}

// Entrada de la pila de efectos (`entry` de apply_effect).
Result<Json> fx_entry(const FxCatalog& catalog, const Json& eid, const Json& params, const Json& frm,
                      const Json& to, const Json& uid, IdSource& ids) {
    const Json* man = catalog.effect(eid);
    if (man == nullptr) {
        return invalid("efecto desconocido: " + (py_truthy(eid) ? py_str(eid) : std::string()));
    }
    if (field(*man, "engine") == "generator") {
        return invalid(py_str(eid) + " es un generador: va en su propio clip, no sobre otro");
    }
    GC_TRY(from, to_py_float(py_truthy(frm) ? frm : Json(0.0), "start"));
    from = std::max(0.0, from);
    if (py_truthy(params) && !params.is_object()) {
        return invalid("params debe ser un objeto");
    }
    Json out = Json::object();
    out["uid"] = py_truthy(uid) ? uid : Json("fx" + ids.hex32().substr(0, 10));
    out["effect"] = (*man)["id"];
    const Json& v = field(*man, "v");
    out["v"] = py_truthy(v) ? py_int(v).value_or(1) : 1;
    out["enabled"] = true;
    out["params"] = FxCatalog::resolve_params(*man, params);
    out["from"] = from;
    if (to.is_null()) {
        out["to"] = nullptr;
    } else {
        GC_TRY(t, to_py_float(to, "end"));
        out["to"] = std::max(from, t);
    }
    return out;
}

} // namespace

// --- Efectos y animaciones de catálogo -----------------------------------------------------

Result<EditResult> run(const Timeline& tl, const ops::ApplyEffect& op, IdSource& ids) {
    GC_TRY(clip_ids, id_list(op.clip_ids));
    if (clip_ids.empty()) {
        return invalid("pasa clip_ids");
    }
    const auto catalog = fx_catalog();
    Timeline out = tl;
    for (const auto& cid : clip_ids) {
        const auto ci = find_clip(out, cid);
        if (!ci) {
            return std::unexpected(ci.error());
        }
        const std::string kind = out.clips[*ci]->kind;
        if (kind != "video" && kind != "image" && kind != "motion") {
            return invalid("los efectos de vídeo solo aplican a vídeo, imagen o motion (clip " + cid + " es " +
                           kind + ")");
        }
        std::vector<Json> cur;
        if (const auto& vfx = out.clips[*ci]->vfx; vfx && vfx->is_array()) {
            for (const Json& e : *vfx) {
                if (e.is_object()) {
                    cur.push_back(e);
                }
            }
        }
        if (!op.stack.is_null()) {
            const auto stack = py_iter(op.stack);
            if (!stack) {
                return invalid("stack debe ser una lista");
            }
            cur.clear();
            for (const Json& e : *stack) {
                if (!e.is_object()) {
                    continue;
                }
                GC_TRY(entry, fx_entry(*catalog, field(e, "effect"), field(e, "params"), field(e, "from"),
                                       field(e, "to"), field(e, "uid"), ids));
                cur.push_back(std::move(entry));
            }
        } else if (op.remove) {
            if (!py_truthy(op.uid) && !py_truthy(op.effect)) {
                return invalid("remove necesita uid o effect");
            }
            std::erase_if(cur, [&](const Json& e) {
                return py_truthy(op.uid) ? field(e, "uid") == op.uid : field(e, "effect") == op.effect;
            });
        } else if (py_truthy(op.uid)) {
            const auto hit = std::ranges::find_if(cur, [&](const Json& e) { return field(e, "uid") == op.uid; });
            if (hit == cur.end()) {
                return invalid("el clip " + cid + " no tiene el efecto " + py_str(op.uid));
            }
            GC_TRY(merged, dict_or_empty(field(*hit, "params"), "params"));
            GC_TRY(extra, dict_or_empty(op.params, "params"));
            for (const auto& [key, value] : extra.items()) {
                merged[key] = value;
            }
            if (!hit->contains("effect")) {
                return invalid("el efecto " + py_str(op.uid) + " no tiene 'effect'");
            }
            GC_TRY(entry, fx_entry(*catalog, (*hit)["effect"], merged,
                                   op.start.is_null() ? field(*hit, "from") : op.start,
                                   op.end.is_null() ? field(*hit, "to") : op.end, op.uid, ids));
            const Json& enabled = field(*hit, "enabled");
            entry["enabled"] = !(enabled.is_boolean() && !enabled.get<bool>()); // hit.get("enabled", True) is not False
            for (Json& e : cur) {
                if (field(e, "uid") == op.uid) {
                    e = entry;
                }
            }
        } else {
            if (!py_truthy(op.effect)) {
                return invalid("pasa effect, uid, remove o stack");
            }
            GC_TRY(entry, fx_entry(*catalog, op.effect, op.params, op.start, op.end, Json(), ids));
            cur.push_back(std::move(entry));
        }
        out.clips[*ci].mut().vfx = cur.empty() ? std::nullopt : std::optional<Json>(Json(cur));
    }
    return EditResult{std::move(out), clip_ids, {}};
}

Result<EditResult> run(const Timeline& tl, const ops::SetClipAnimation& op, IdSource&) {
    if (op.slot != "in" && op.slot != "out" && op.slot != "loop" && op.slot != "combo") {
        return invalid("slot debe ser in, out, loop o combo");
    }
    const std::string slot = op.slot.get<std::string>();
    GC_TRY(clip_ids, id_list(op.clip_ids));
    if (clip_ids.empty()) {
        return invalid("pasa clip_ids");
    }
    const auto catalog = fx_catalog();
    const Json* anim = nullptr;
    if (py_truthy(op.animation)) {
        anim = catalog->anim(op.animation);
        if (anim == nullptr) {
            return invalid("animación desconocida: " + py_str(op.animation));
        }
        if (field(*anim, "slot") != slot) {
            return invalid(py_str(op.animation) + " es de otra ranura, no " + slot);
        }
    }
    Timeline out = tl;
    for (const auto& cid : clip_ids) {
        const auto ci = find_clip(out, cid);
        if (!ci) {
            return std::unexpected(ci.error());
        }
        const std::string kind = out.clips[*ci]->kind;
        if (kind != "video" && kind != "image" && kind != "motion" && kind != "text" && kind != "shape") {
            return invalid("las animaciones solo aplican a vídeo, imagen, texto, figura o motion (" + cid + ")");
        }
        const auto& current = out.clips[*ci]->animations;
        Json cur = current && current->is_object() ? *current : Json::object();
        if (anim == nullptr) {
            cur.erase(slot);
        } else {
            Json val = {{"id", (*anim)["id"]}};
            if (slot != "combo") {
                double d = 0.0;
                if (!op.duration.is_null()) {
                    GC_TRY(v, to_py_float(op.duration, "duration"));
                    d = v;
                } else {
                    const Json& def = field(*anim, "dur");
                    GC_TRY(v, to_py_float(py_truthy(def) ? def : Json(0.5), "dur"));
                    d = v;
                }
                val["dur"] = round3(std::min(8.0, std::max(0.1, d)));
            }
            cur[slot] = std::move(val);
        }
        out.clips[*ci].mut().animations = cur.empty() ? std::nullopt : std::optional<Json>(std::move(cur));
    }
    return EditResult{std::move(out), clip_ids, {}};
}

// --- Congelar fotograma ----------------------------------------------------------------------

namespace {

Result<Keyframe> frozen_keyframe(double cx, double cy, std::optional<double> zoom, const Json& fit) {
    Json raw = {{"t", 0}, {"cx", cx}, {"cy", cy}, {"pan_mode", "smooth"}, {"fit", fit}};
    raw["zoom"] = zoom ? Json(*zoom) : Json(nullptr);
    return keyframe_from_json(raw); // Keyframe(...) de Pydantic: valida los rangos
}

} // namespace

Result<EditResult> run(const Timeline& tl, const ops::FreezeFrame& op, IdSource& ids) {
    Timeline out = tl;
    const auto vi = find_clip(out, op.clip_id);
    if (!vi) {
        return std::unexpected(vi.error());
    }
    const TimelineClip v = *out.clips[*vi];
    if (v.kind != "video") {
        return invalid("solo se puede congelar un fotograma de un clip de vídeo");
    }
    GC_TRY(dur_raw, to_py_float(op.duration, "duration"));
    const double dur = std::max(0.1, dur_raw);
    const double tdur = clip_timeline_duration(v);
    GC_TRY(at, to_py_float(op.at_time, "at_time"));
    double rel = at - v.start;
    if (rel < -1e-3 || rel > tdur + 1e-3) {
        return invalid("at_time debe caer dentro del clip");
    }
    rel = std::min(std::max(rel, 0.0), tdur);
    const double sp = clip_speed(v);
    double src = v.reverse ? v.out_point - rel * sp : v.in_point + rel * sp;
    src = std::min(std::max(src, v.in_point), v.out_point);

    // La imagen congelada lleva la pose, el encuadre, las máscaras y los efectos
    // que tenía el vídeo en ese instante.
    const Pose pose = clip_pose(v, rel);
    std::optional<Reframe> reframe;
    if (v.reframe) {
        const Reframe& rf = *v.reframe;
        double cx = 0.5;
        double cy = 0.5;
        std::optional<double> zoom;
        Json fit;
        if (keyframes_enabled(v)) {
            const KfProps p = clip_props_at(v, rel);
            cx = p.get("cx");
            cy = p.get("cy");
            zoom = p.get("zoom");
        } else {
            const Frame f = frame_at(rf.keyframes, src, rf.zoom != 0.0 ? rf.zoom : 1.0, rf.pan_mode.empty() ? "smooth" : rf.pan_mode);
            cx = f.cx;
            cy = f.cy;
            zoom = f.zoom;
            fit = f.fit;
        }
        GC_TRY(kf, frozen_keyframe(cx, cy, zoom, fit));
        Reframe r = rf;
        r.zoom = zoom && *zoom != 0.0 ? *zoom : rf.zoom;
        r.keyframes = {std::move(kf)};
        if (rf.dual_crop) {
            const double z2 = rf.zoom2 && *rf.zoom2 != 0.0 ? *rf.zoom2 : (rf.zoom != 0.0 ? rf.zoom : 1.0);
            const Frame f2 = frame_at(rf.keyframes2.empty() ? rf.keyframes : rf.keyframes2, src, z2,
                                      rf.pan_mode.empty() ? "smooth" : rf.pan_mode);
            GC_TRY(kf2, frozen_keyframe(f2.cx, f2.cy, f2.zoom, f2.fit));
            r.keyframes2 = {std::move(kf2)};
        } else {
            r.keyframes2.clear();
        }
        reframe = std::move(r);
    }

    if (!op.image.is_object() || !op.image.contains("id") || !op.image.contains("filename")) {
        return invalid("image debe ser {id, filename, label?}");
    }
    const Json& filename = op.image["filename"];
    const Json& label = field(op.image, "label");
    const Json& name = py_truthy(label) ? label : filename;
    if (!filename.is_string() || !name.is_string()) {
        return invalid("image.filename y image.label deben ser texto");
    }
    TimelineClip frozen;
    frozen.id = make_uid(ids, "c");
    frozen.track_id = v.track_id;
    frozen.kind = "image";
    frozen.asset_kind = "images";
    frozen.asset_id = py_str(op.image["id"]);
    frozen.filename = filename.get<std::string>();
    frozen.name = name.get<std::string>();
    frozen.in_point = 0.0;
    frozen.out_point = round3(dur);
    frozen.source_duration = round3(dur);
    frozen.layout = v.layout;
    frozen.frame = v.frame;
    frozen.reframe = std::move(reframe);
    frozen.transform = Json{{"x", pose.x}, {"y", pose.y}, {"scale", pose.scale}, {"rotation", pose.rotation}};
    if (pose.opacity < 1) {
        frozen.opacity = pose.opacity;
    }
    frozen.effects = v.effects && v.effects->is_object() ? *v.effects : Json::object();
    frozen.look = v.look.empty() ? "none" : v.look;
    frozen.masks = clip_masks_at(v, rel, true);
    frozen.flip_h = v.flip_h;
    frozen.flip_v = v.flip_v;
    frozen.blend_mode = v.blend_mode;
    // El croma se conserva (es una propiedad del píxel); el matte de IA no (era del vídeo).
    if (v.bg_removal && v.bg_removal->is_object() && !v.bg_removal->empty() &&
        py_truthy(field(field(*v.bg_removal, "chroma"), "enabled"))) {
        Json bg = *v.bg_removal;
        Json autom = field(bg, "auto").is_object() ? bg["auto"] : Json::object();
        autom["enabled"] = false;
        bg["auto"] = std::move(autom);
        frozen.bg_removal = std::move(bg);
    }

    // Partir el vídeo (o poner el congelado delante/detrás si el cabezal está en un
    // borde) y desplazar lo que viene detrás en la misma pista.
    const bool can_split = 0.1 < std::abs(src - v.in_point) && 0.1 < std::abs(v.out_point - src);
    const double pivot = can_split ? v.start + rel : (rel < tdur / 2 ? v.start : v.start + tdur);
    frozen.start = round3(pivot);
    std::vector<std::string> shifted;
    for (auto& c : out.clips) {
        if (c->id != v.id && c->track_id == v.track_id && c->start >= pivot - 1e-3) {
            c.mut().start = round3(c->start + dur);
            shifted.push_back(c->id);
        }
    }
    std::vector<std::string> changed;
    const auto at_it = out.clips.begin() + static_cast<std::ptrdiff_t>(*vi);
    const std::string frozen_id = frozen.id;
    if (can_split) {
        TimelineClip left = v;
        TimelineClip right = v;
        right.id = make_uid(ids, "c");
        const double cut = round3(src);
        if (v.reverse) {
            left.in_point = cut;
            right.out_point = cut;
        } else {
            left.out_point = cut;
            right.in_point = cut;
        }
        right.start = round3(pivot + dur);
        if (auto r = shift_keyframe_items(right, rel, ids); !r) {
            return std::unexpected(r.error());
        }
        changed = {left.id, frozen_id, right.id};
        *at_it = Cow<TimelineClip>(std::move(left));
        out.clips.insert(at_it + 1, {Cow<TimelineClip>(std::move(frozen)), Cow<TimelineClip>(std::move(right))});
    } else if (pivot <= v.start + 1e-6) {
        TimelineClip moved = v;
        moved.start = round3(v.start + dur);
        changed = {frozen_id, moved.id};
        *at_it = Cow<TimelineClip>(std::move(frozen));
        out.clips.insert(at_it + 1, Cow<TimelineClip>(std::move(moved)));
    } else {
        changed = {v.id, frozen_id};
        out.clips.insert(at_it + 1, Cow<TimelineClip>(std::move(frozen)));
    }
    for (auto& id : shifted) {
        changed.push_back(std::move(id));
    }
    return EditResult{std::move(out), std::move(changed), {}};
}

// --- Subtítulos -------------------------------------------------------------------------------

Result<EditResult> run(const Timeline& tl, const ops::AddSubtitles& op, IdSource& ids) {
    Timeline out = tl;
    const auto si = find_clip(out, op.source_clip_id);
    if (!si) {
        return std::unexpected(si.error());
    }
    const TimelineClip& src = *out.clips[*si];
    const double s_start = src.start;
    const double s_in = src.in_point;
    const double s_out = src.out_point;

    std::vector<std::string> created;
    std::string tid;
    std::optional<Json> track_style;
    if (op.track_id) {
        const auto t = find_track(out, *op.track_id);
        if (!t) {
            return std::unexpected(t.error());
        }
        if (out.tracks[*t].kind != "text") {
            return invalid("la pista " + *op.track_id + " no es de texto");
        }
        tid = *op.track_id;
        track_style = out.tracks[*t].style;
    } else {
        const auto existing =
            std::ranges::find_if(out.tracks, [](const TimelineTrack& t) { return t.kind == "text"; });
        if (existing == out.tracks.end()) {
            TimelineTrack track;
            track.id = make_uid(ids, "T");
            track.kind = "text";
            track.name = "Subtítulos";
            tid = track.id;
            insert_track(out.tracks, std::move(track));
            created.push_back(tid);
        } else {
            tid = existing->id;
            track_style = existing->style;
        }
    }
    // Sin estilo explícito, el de la pista (tema, fuente, karaoke…).
    const Json style = !op.style.is_null() ? op.style
                                           : (track_style && py_truthy(*track_style) ? *track_style : Json::object());
    GC_TRY(made, text_clips_from_transcript(s_start, s_in, s_out, op.segments, tid, style, op.transcript_id, ids));
    std::vector<std::string> changed = created;
    for (const Json& cd : made) {
        auto clip = clip_from_json(cd);
        if (!clip) {
            return std::unexpected(clip.error());
        }
        changed.push_back(clip->id);
        out.clips.emplace_back(std::move(*clip));
    }
    auto warnings = track_overlap_warnings(out, tid);
    return EditResult{std::move(out), std::move(changed), std::move(warnings)};
}

// --- Figuras, barras de cine y capa de ajuste ---------------------------------------------------

Result<EditResult> run(const Timeline& tl, const ops::AddShape& op, IdSource& ids) {
    Timeline out = tl;
    Json shape = op.shape;
    if (!op.points.is_null()) {
        GC_TRY(base, dict_or_empty(op.shape, "shape"));
        Json opts = Json::object();
        for (const auto& [key, value] : base.items()) {
            if (key != "type" && key != "points" && key != "x" && key != "y" && key != "w" && key != "h") {
                opts[key] = value;
            }
        }
        GC_TRY(path, path_shape(op.points, opts));
        shape = std::move(path);
    }
    GC_TRY(st, normalize_shape(shape));
    std::vector<std::string> created;
    std::string tid;
    if (!op.track_id) {
        const auto existing =
            std::ranges::find_if(out.tracks, [](const TimelineTrack& t) { return t.kind == "video"; });
        if (existing == out.tracks.end()) {
            TimelineTrack track;
            track.id = make_uid(ids, "K");
            track.kind = "video";
            track.name = "V" + std::to_string(out.tracks.size() + 1);
            tid = track.id;
            insert_track(out.tracks, std::move(track));
            created.push_back(tid);
        } else {
            tid = existing->id;
        }
    } else {
        const auto t = find_track(out, *op.track_id);
        if (!t) {
            return std::unexpected(t.error());
        }
        if (out.tracks[*t].kind != "video") {
            return invalid("una figura va en pista de vídeo, no '" + out.tracks[*t].kind + "'");
        }
        tid = *op.track_id;
    }
    GC_TRY(start_opt, opt_py_number(op.start, "start"));
    if (!start_opt) {
        return invalid("start debe ser un número");
    }
    const double start = *start_opt;
    if (start < 0) {
        return invalid("start no puede ser negativo");
    }
    double dur = kShapeDefaultDuration;
    if (!op.duration.is_null()) {
        GC_TRY(d, to_py_float(op.duration, "duration"));
        dur = d;
    }
    if (dur <= 0) {
        return invalid("duration debe ser > 0");
    }
    TimelineClip c = new_clip(ids, tid, "shape", "shape", "", "", round3(start), round3(dur));
    c.shape = std::move(st);
    const std::string id = c.id;
    out.clips.emplace_back(std::move(c));
    created.push_back(id);
    auto warnings = track_overlap_warnings(out, tid);
    return EditResult{std::move(out), std::move(created), std::move(warnings)};
}

Result<EditResult> run(const Timeline& tl, const ops::AddCinemaBars& op, IdSource& ids) {
    double r = 0.0;
    if (const Json* named = cinema_ratio(op.ratio.is_null() ? "None" : py_str(op.ratio))) {
        r = named->get<double>();
    } else {
        const auto f = py_float(op.ratio);
        if (!f) {
            return invalid("ratio: usa 2.39, 2, 1.85, 16:9 o un número");
        }
        r = *f;
    }
    if (r <= 0) {
        return invalid("ratio debe ser > 0");
    }
    Timeline out = tl;
    double end = 0.0;
    bool any = false;
    for (const auto& c : out.clips) {
        const double e = c->start + clip_span_duration(*c);
        end = any ? std::max(end, e) : e;
        any = true;
    }
    double dur = 0.0;
    if (!op.duration.is_null()) {
        GC_TRY(d, to_py_float(op.duration, "duration"));
        dur = d;
    } else {
        // `end - start`: aquí start tiene que ser un número (no vale texto)
        GC_TRY(start_num, opt_py_number(op.start, "start"));
        if (!start_num) {
            return invalid("start debe ser un número");
        }
        dur = end - *start_num > 0.05 ? end - *start_num : 5.0;
    }
    if (dur <= 0) {
        return invalid("duration debe ser > 0");
    }
    if (out.height == 0) {
        return invalid("formato inválido (alto 0)");
    }
    const Json bar = cinema_bar(static_cast<double>(out.width) / static_cast<double>(out.height), r);
    if (!(bar.get<double>() > 0)) {
        return invalid("con este formato esa proporción no deja barras (el vídeo ya es más ancho)");
    }
    GC_TRY(start_f, to_py_float(op.start, "start"));
    const double start = round3(std::max(0.0, start_f));
    dur = round3(dur);
    std::vector<std::string> created;
    const std::string tid = top_free_video_track(out, start, dur, created, ids);
    GC_TRY(shape, normalize_shape(Json{{"type", "letterbox"}, {"bar", bar}}));
    TimelineClip c = new_clip(ids, tid, "shape", "shape", "letterbox", "Barras de cine", start, dur);
    c.shape = std::move(shape);
    if (op.animate) { // las barras entran en 1 s
        upsert_keyframe_at(c, 0.0, Json{{"draw", 0.0}}, Interp::Linear, ids);
        upsert_keyframe_at(c, std::min(1.0, dur), Json{{"draw", 1.0}}, Interp::EaseInOut, ids);
    }
    created.push_back(c.id);
    out.clips.emplace_back(std::move(c));
    return EditResult{std::move(out), std::move(created), {}};
}

Result<EditResult> run(const Timeline& tl, const ops::AddAdjustmentLayer& op, IdSource& ids) {
    if (op.duration.is_null()) {
        return invalid("duration debe ser > 0");
    }
    GC_TRY(dur_raw, to_py_float(op.duration, "duration"));
    if (dur_raw <= 0) {
        return invalid("duration debe ser > 0");
    }
    GC_TRY(start_num, opt_py_number(op.start, "start"));
    if (!start_num) {
        return invalid("start debe ser un número");
    }
    if (*start_num < 0) {
        return invalid("start no puede ser negativo");
    }
    GC_TRY(intensity, to_py_float(op.intensity, "intensity"));
    if (!(0.0 <= intensity && intensity <= 1.0)) {
        return invalid("intensity debe estar en [0, 1]");
    }
    Timeline out = tl;
    const double start = round3(*start_num);
    const double dur = round3(dur_raw);
    std::vector<std::string> created;
    std::string tid;
    if (op.track_id) {
        const auto t = find_track(out, *op.track_id);
        if (!t) {
            return std::unexpected(t.error());
        }
        if (out.tracks[*t].kind != "video") {
            return invalid("una capa de ajuste va en una pista de vídeo");
        }
        tid = *op.track_id;
    } else {
        tid = top_free_video_track(out, start, dur, created, ids);
    }
    TimelineClip c = new_clip(ids, tid, "adjustment", "adjustment", "adjustment", "Capa de ajuste", start, dur);
    c.opacity = intensity;
    const std::string id = c.id;
    out.clips.emplace_back(std::move(c));
    created.push_back(id);
    if (!op.filters.is_null() || py_truthy(op.effects)) {
        auto r = run(out, ops::SetClipEffects{id, py_truthy(op.effects) ? op.effects : Json::object(), false, op.filters},
                     ids);
        if (!r) {
            return std::unexpected(r.error());
        }
        return EditResult{std::move(r->timeline), std::move(created), {}};
    }
    return EditResult{std::move(out), std::move(created), {}};
}

// --- Eliminar fondo ----------------------------------------------------------------------------

Result<EditResult> run(const Timeline& tl, const ops::SetClipBgRemoval& op, IdSource&) {
    Timeline out = tl;
    const auto ci = find_clip(out, op.clip_id);
    if (!ci) {
        return std::unexpected(ci.error());
    }
    if (op.bg_removal.is_null()) {
        out.clips[*ci].mut().bg_removal.reset();
        return EditResult{std::move(out), {op.clip_id}, {}};
    }
    if (!bg_capable(*out.clips[*ci])) {
        return invalid("bg_removal solo aplica a clips de vídeo o imagen");
    }
    if (!op.bg_removal.is_object()) {
        return invalid("bg_removal debe ser un objeto");
    }
    // MERGE por secciones: un parche de la tolerancia del croma no borra la clave de
    // caché del matte ya calculado.
    const auto& current = out.clips[*ci]->bg_removal;
    GC_TRY(base, op.replace ? Result<Json>(Json::object()) : dict_or_empty(current.value_or(Json()), "bg_removal"));
    Json merged = base;
    for (const auto& [key, value] : op.bg_removal.items()) {
        if ((key == "auto" || key == "chroma" || key == "outline") && value.is_object()) {
            GC_TRY(section, dict_or_empty(field(base, key), key));
            for (const auto& [k, v] : value.items()) {
                section[k] = v;
            }
            merged[key] = std::move(section);
        } else {
            merged[key] = value;
        }
    }
    const Json bg = normalize_bg(merged);
    out.clips[*ci].mut().bg_removal = bg.is_null() ? std::nullopt : std::optional<Json>(bg);
    return EditResult{std::move(out), {op.clip_id}, {}};
}

} // namespace gc::core::detail
