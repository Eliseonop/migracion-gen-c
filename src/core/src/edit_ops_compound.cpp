// Operaciones compuestas: pegar atributos y seguir un objeto (espejo de
// backend/app/timeline_ops.py).

#include <algorithm>

#include "edit_ops.hpp"
#include "edit_util.hpp"
#include "gc/core/clip_attrs.hpp"
#include "gc/core/follow.hpp"
#include "gc/core/keyframes.hpp"
#include "gc/core/serialize.hpp"

namespace gc::core::detail {

namespace {

// repr() de una lista de grupos (ids sencillos): ['crop', 'speed']
std::string repr_list(const std::vector<std::string>& items) {
    std::string out = "[";
    for (std::size_t i = 0; i < items.size(); ++i) {
        out += (i == 0 ? "'" : ", '") + items[i] + "'";
    }
    return out + "]";
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

} // namespace gc::core::detail
