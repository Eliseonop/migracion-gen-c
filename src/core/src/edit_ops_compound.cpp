// Operaciones compuestas: pegar atributos, seguir un objeto, sonorizar y animar con
// un preset (espejo de backend/app/timeline_ops.py).

#include <algorithm>

#include "edit_ops.hpp"
#include "edit_util.hpp"
#include "gc/core/clip_attrs.hpp"
#include "gc/core/clip_motion.hpp"
#include "gc/core/follow.hpp"
#include "gc/core/keyframes.hpp"
#include "gc/core/timeline_rules.hpp"
#include "gc/core/serialize.hpp"

namespace gc::core::detail {

namespace {

// repr() de una lista de textos: ['crop', 'speed']
std::string repr_list(const std::vector<std::string>& items) {
    Json list = Json::array();
    for (const auto& s : items) {
        list.push_back(s);
    }
    return py_repr(list);
}

// `[str(x) for x in v]`; nullopt si `v` no se puede recorrer.
std::optional<std::vector<std::string>> str_list(const Json& v) {
    const auto items = py_iter(v);
    if (!items) {
        return std::nullopt;
    }
    std::vector<std::string> out;
    for (const Json& x : *items) {
        out.push_back(py_str(x));
    }
    return out;
}

// _SFX_TRACK_RE = re.compile(r"^sfx\b", re.I): «SFX», «sfx 2», «Sfx-x»; no «SFX_1» ni «sfx1».
bool sfx_track_name(std::string_view name) {
    std::size_t i = 0;
    if (name.starts_with("\xC5\xBF")) { // «ſ»: con re.I también vale por «s»
        i = 2;
    } else if (!name.empty() && (name[0] == 's' || name[0] == 'S')) {
        i = 1;
    } else {
        return false;
    }
    if (name.size() < i + 2 || (name[i] != 'f' && name[i] != 'F') || (name[i + 1] != 'x' && name[i + 1] != 'X')) {
        return false;
    }
    i += 2;
    return i == name.size() || !py_word_char(utf8_next(name, i));
}

constexpr double kAmbienceFade = 0.4;

} // namespace

Result<EditResult> run(const Timeline& tl, const ops::PasteClipAttributes& op, IdSource& ids) {
    std::vector<std::string> wanted;
    if (op.groups.is_null()) {
        wanted.assign(kAttrGroupIds.begin(), kAttrGroupIds.end());
    } else {
        auto list = str_list(op.groups);
        if (!list) {
            return invalid("groups debe ser una lista");
        }
        wanted = std::move(*list);
    }
    std::vector<std::string> bad;
    for (const auto& g : wanted) {
        if (std::ranges::find(kAttrGroupIds, g) == kAttrGroupIds.end()) {
            bad.push_back(g);
        }
    }
    if (!bad.empty()) {
        return invalid("groups: grupos no válidos " + repr_list(bad) + " (usa " +
                       repr_list({kAttrGroupIds.begin(), kAttrGroupIds.end()}) + ")");
    }
    if (wanted.empty()) {
        return invalid("groups vacío: indica al menos un grupo");
    }

    std::vector<std::string> targets; // sin repetir, en orden (dict.fromkeys)
    if (py_truthy(op.target_clip_ids)) {
        auto list = str_list(op.target_clip_ids);
        if (!list) {
            return invalid("target_clip_ids debe ser una lista");
        }
        for (auto& t : *list) {
            if (t != op.source_clip_id && std::ranges::find(targets, t) == targets.end()) {
                targets.push_back(std::move(t));
            }
        }
    }
    if (targets.empty()) {
        return invalid("target_clip_ids: indica al menos un clip destino distinto del origen");
    }

    Timeline out = tl;
    GC_TRY(si, find_clip(out, op.source_clip_id));
    const TimelineClip src = *out.clips[si];
    std::vector<std::string> changed;
    std::vector<std::string> warnings;
    for (const auto& tid : targets) {
        GC_TRY(i, find_clip(out, tid));
        const TimelineClip cur = *out.clips[i];
        std::vector<std::string> ok;
        std::vector<std::string> skipped;
        for (const auto& g : wanted) {
            (group_applies(g, src, cur) ? ok : skipped).push_back(g);
        }
        if (!skipped.empty() && !op.groups.is_null()) {
            warnings.push_back(tid + " (" + cur.kind + "): no admite " + repr_list(skipped));
        }
        if (ok.empty()) {
            continue;
        }
        GC_TRY(pasted, paste_attributes(cur, src, ok, ids));
        GC_TRY(clip, clip_from_json(pasted));
        out.clips[i] = std::move(clip);
        changed.push_back(tid);
    }
    if (changed.empty()) {
        return invalid("ningún clip destino admite esos grupos");
    }
    return EditResult{std::move(out), std::move(changed), std::move(warnings)};
}

Result<EditResult> run(const Timeline& tl, const ops::FollowObject& op, IdSource& ids) {
    Timeline out = tl;
    GC_TRY(vi, find_clip(out, op.video_clip_id));
    GC_TRY(fi, find_clip(out, op.follower_clip_id));
    const TimelineClip& video = *out.clips[vi];
    TimelineClip follower = *out.clips[fi];
    if (video.kind != "video") {
        return invalid("solo se siguen objetos de un clip de vídeo");
    }
    if (follower.id == video.id || follower.kind == "audio") {
        return invalid("el clip que acompaña debe ser otro clip visual (texto, figura, imagen…)");
    }

    // Python solo usa estos valores si el recorrido coincide con el vídeo; si no coincide,
    // la operación falla igual, así que se pueden comprobar ya. min_gap, en cambio, solo
    // se usa a partir del segundo keyframe.
    const auto box_w = py_number(op.box_w);
    const auto anchor_t = py_number(op.anchor_t);
    if (!box_w || !anchor_t) {
        return invalid("box_w y anchor_t deben ser números");
    }
    if (!op.dims.is_object()) {
        return invalid("dims debe ser {srcW, srcH, outW, outH}");
    }
    FollowDims dims;
    for (const auto& [key, dst] : {std::pair<const char*, double*>{"srcW", &dims.src_w}, {"srcH", &dims.src_h},
                                   {"outW", &dims.out_w}, {"outH", &dims.out_h}}) {
        const auto v = op.dims.contains(key) ? py_number(op.dims.at(key)) : std::nullopt;
        if (!v) {
            return invalid(std::string("dims.") + key + " debe ser un número");
        }
        *dst = *v;
    }
    const std::string mode = op.mode.is_string() ? op.mode.get<std::string>() : std::string();
    GC_TRY(keys, follow_keys(video, follower, op.track, dims, *box_w, *anchor_t, mode, py_number(op.min_gap)));
    if (keys.empty()) {
        return invalid("el objeto seguido no coincide en el tiempo con ese clip");
    }

    // Fuera los keyframes que había dentro del tramo seguido.
    const double lo = keys.front().t - kKfSnap;
    const double hi = keys.back().t + kKfSnap;
    const Json kf = follower.keyframes && follower.keyframes->is_object() ? *follower.keyframes : Json::object();
    if (py_truthy(field(kf, "enabled"))) {
        Json kept = Json::array();
        const Json& raw = field(kf, "items");
        if (py_truthy(raw)) {
            const auto items = py_iter(raw);
            if (!items) {
                return invalid("keyframes.items debe ser una lista");
            }
            for (const Json& k : *items) {
                if (!k.is_object()) {
                    return invalid("keyframes.items: cada keyframe debe ser un objeto");
                }
                GC_TRY(t, to_py_float(k.contains("t") ? k.at("t") : Json(0), "keyframes.items[].t"));
                if (!(lo <= t && t <= hi)) {
                    kept.push_back(k);
                }
            }
        }
        Json next = kf;
        next["items"] = std::move(kept);
        follower.keyframes = std::move(next);
    }
    for (const auto& k : keys) {
        Json patch = Json::object();
        patch["x"] = k.x;
        patch["y"] = k.y;
        if (k.scale) {
            patch["scale"] = *k.scale;
        }
        if (k.rotation) {
            patch["rotation"] = *k.rotation;
        }
        upsert_keyframe_at(follower, k.t, patch, Interp::Linear, ids);
    }
    out.clips[fi] = std::move(follower);
    return EditResult{std::move(out), {op.follower_clip_id}, {}};
}

Result<EditResult> run(const Timeline& tl, const ops::AddSoundDesign& op, IdSource& ids) {
    Timeline out = tl;
    GC_TRY(si, find_clip(out, op.clip_id));
    const double s0 = out.clips[si]->start;
    const double scene_dur = clip_timeline_duration(*out.clips[si]);
    std::vector<std::string> changed;

    auto overlaps = [&](const std::string& track_id, double start, double dur) {
        return std::ranges::any_of(out.clips, [&](const Cow<TimelineClip>& c) {
            return c->track_id == track_id && c->start < start + dur - 1e-6 &&
                   c->start + clip_span_duration(*c) > start + 1e-6;
        });
    };
    // `x or defecto` antes de float(x)
    auto num_or = [](const Json& v, double fallback, std::string_view what) {
        return to_py_float(py_truthy(v) ? v : Json(fallback), what);
    };

    std::vector<Json> sounds;
    if (py_truthy(op.sounds)) {
        auto list = py_iter(op.sounds);
        if (!list) {
            return invalid("sounds debe ser una lista");
        }
        sounds = std::move(*list);
    }
    for (const Json& raw : sounds) {
        const Json s = py_truthy(raw) ? raw : Json::object();
        if (!s.is_object()) {
            return invalid("cada sonido debe ser un objeto");
        }
        const Json sfx = py_truthy(field(s, "sfx")) ? s.at("sfx") : Json::object();
        if (!sfx.is_object()) {
            return invalid("sfx debe ser un objeto");
        }
        if (!py_truthy(field(sfx, "id"))) {
            continue;
        }
        GC_TRY(src, num_or(field(sfx, "duration"), 0.0, "sfx.duration"));
        GC_TRY(rel, num_or(field(s, "start"), 0.0, "start"));
        const double start = round3(s0 + std::min(std::max(0.0, rel), std::max(0.0, scene_dur - 0.1)));
        GC_TRY(asked, num_or(field(s, "duration"), 0.1, "duration"));
        const double want = std::max(0.1, std::min(asked, s0 + scene_dur - start));
        const double dur = round3(src > 0 ? std::min(want, src) : want);

        // La primera pista «SFX» libre; si no hay, una nueva al final.
        std::string track_id;
        for (const auto& t : out.tracks) {
            if (t.kind == "audio" && !t.locked && sfx_track_name(t.name) && !overlaps(t.id, start, dur)) {
                track_id = t.id;
                break;
            }
        }
        if (track_id.empty()) {
            const auto n = std::ranges::count_if(out.tracks, [](const TimelineTrack& t) {
                return t.kind == "audio" && sfx_track_name(t.name);
            });
            TimelineTrack track;
            track.id = make_uid(ids, "K");
            track.kind = "audio";
            track.name = n > 0 ? "SFX " + std::to_string(n + 1) : "SFX";
            track_id = track.id;
            changed.push_back(track.id);
            out.tracks.push_back(std::move(track));
        }

        const std::string what = py_strip(py_str(py_truthy(field(s, "what")) ? s.at("what") : Json("")));
        const std::string id = py_str(sfx.at("id"));
        Json clip = Json::object();
        clip["id"] = make_uid(ids, "c");
        clip["track_id"] = track_id;
        clip["kind"] = "audio";
        clip["asset_kind"] = "sfx";
        clip["asset_id"] = id;
        clip["filename"] = id;
        clip["name"] = py_truthy(field(sfx, "name")) ? sfx.at("name") : Json(py_path_stem(id));
        clip["start"] = start;
        clip["in_point"] = 0.0;
        clip["out_point"] = dur;
        clip["source_duration"] = round3(src > 0 ? src : dur);
        GC_TRY(volume, num_or(field(s, "volume"), 0.8, "volume"));
        clip["volume"] = std::min(1.0, std::max(0.05, volume));
        clip["note"] = what.empty() ? Json() : Json(what);
        clip["note_source"] = what.empty() ? Json() : Json("ai");
        GC_TRY(c, clip_from_json(clip));
        if (field(s, "kind") == "ambience" && dur > 1) {
            for (const char* side : {"in", "out"}) {
                if (auto r = apply_volume_fade(c, dur, side, kAmbienceFade, ids); !r) {
                    return std::unexpected(r.error());
                }
            }
        }
        changed.push_back(c.id);
        out.clips.emplace_back(std::move(c));
    }
    if (changed.empty()) {
        return invalid("no hay sonidos que colocar");
    }
    return EditResult{std::move(out), std::move(changed), {}};
}

Result<EditResult> run(const Timeline& tl, const ops::AnimateClip& op, IdSource& ids) {
    Timeline out = tl;
    GC_TRY(ci, find_clip(out, op.clip_id));
    TimelineClip c = *out.clips[ci];
    if (c.kind != "video" && c.kind != "image" && c.kind != "text" && c.kind != "shape") {
        return invalid("animate_clip solo aplica a vídeo, imagen, texto o figura");
    }
    GC_TRY(items, build_motion_items(c, op.motion, op.duration, op.intensity, op.turns, op.envelope, ids));
    c.keyframes = merge_visual_keyframes(c, items);
    GC_TRY(motion, normalize_motion(op.motion));
    // En los clips de relleno el zoom es un recorte: también va al reframe (en tiempo de
    // la FUENTE) para que el export coincida con la vista previa.
    if ((motion == "zoom_in" || motion == "zoom_out") && !uses_scale_zoom(c)) {
        const double spd = clip_speed(c);
        std::vector<Keyframe> kfs;
        double last_zoom = 1.0;
        auto clamp01 = [](const std::optional<double>& v) {
            return v ? std::max(0.0, std::min(1.0, *v)) : 0.5;
        };
        for (const auto& it : items) {
            const auto& zoom = it.props[7];
            const double z = std::max(0.1, std::min(1.0, zoom && *zoom != 0.0 ? *zoom : 1.0));
            last_zoom = z;
            Keyframe k;
            k.t = round3(c.in_point + it.t * spd);
            k.cx = clamp01(it.props[5]);
            k.cy = clamp01(it.props[6]);
            k.zoom = z;
            kfs.push_back(std::move(k));
        }
        Reframe rf = c.reframe.value_or(Reframe{});
        rf.zoom = last_zoom;
        rf.keyframes = std::move(kfs);
        c.reframe = std::move(rf);
    }
    out.clips[ci] = std::move(c);
    return EditResult{std::move(out), {op.clip_id}, {}};
}

} // namespace gc::core::detail
