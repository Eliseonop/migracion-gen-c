// Operaciones de audio: volumen, silencio, fundidos y efectos de sonido
// (espejo de timeline_ops.set_clip_volume / set_track_audio / set_clip_audio_fx).

#include <algorithm>
#include <array>

#include "edit_ops.hpp"
#include "edit_util.hpp"
#include "gc/core/keyframes.hpp"
#include "gc/core/timeline_rules.hpp"

namespace gc::core::detail {

namespace {

constexpr std::array<std::string_view, 11> kAudioFxIds{"eq",         "compressor", "reverb", "echo",
                                                       "denoise",    "distortion", "underwater", "telephone",
                                                       "radio",      "megaphone",  "muffled"};

struct AudioPatch {
    const Json& volume;
    const Json& muted;
    const Json& audio_fx;
    bool replace_fx = false;
    const Json& fade;
    const Json& fade_dur;
};

// timeline_ops._apply_audio_to_clip
Result<void> apply_audio(TimelineClip& c, const AudioPatch& p, IdSource& ids) {
    if (c.kind != "video" && c.kind != "audio") {
        return invalid("volumen/audio_fx solo aplica a clips de vídeo o audio");
    }
    if (!p.volume.is_null()) {
        c.volume = clamp_volume(p.volume);
    }
    if (!p.muted.is_null()) {
        c.muted = py_truthy(p.muted);
    }
    if (!p.audio_fx.is_null()) {
        if (!p.audio_fx.is_object()) {
            return invalid("audio_fx debe ser un objeto");
        }
        std::string bad;
        for (const auto& [key, value] : p.audio_fx.items()) {
            if (std::ranges::find(kAudioFxIds, key) == kAudioFxIds.end()) {
                bad += (bad.empty() ? "'" : ", '") + key + "'";
            }
        }
        if (!bad.empty()) {
            return invalid("audio_fx: efectos no válidos [" + bad + "]");
        }
        Json base = !p.replace_fx && c.audio_fx && c.audio_fx->is_object() ? *c.audio_fx : Json::object();
        for (const auto& [key, value] : p.audio_fx.items()) {
            base[key] = value;
        }
        c.audio_fx = base.empty() ? std::nullopt : std::optional<Json>(std::move(base));
    }
    if (py_truthy(p.fade)) {
        if (p.fade != "in" && p.fade != "out") {
            return invalid("fade debe ser 'in' o 'out'");
        }
        return apply_volume_fade(c, clip_span_duration(c), p.fade.get<std::string>(), py_num(p.fade_dur, 0.5), ids);
    }
    return {};
}

} // namespace

Result<EditResult> run(const Timeline& tl, const ops::SetClipVolume& op, IdSource& ids) {
    Timeline out = tl;
    const auto ci = find_clip(out, op.clip_id);
    if (!ci) {
        return std::unexpected(ci.error());
    }
    if (op.volume.is_null() && op.muted.is_null() && !py_truthy(op.fade)) {
        return invalid("pasa volume, muted y/o fade");
    }
    const Json none;
    if (auto r = apply_audio(out.clips[*ci].mut(), {op.volume, op.muted, none, false, op.fade, op.fade_dur}, ids);
        !r) {
        return std::unexpected(r.error());
    }
    return EditResult{std::move(out), {op.clip_id}, {}};
}

Result<EditResult> run(const Timeline& tl, const ops::SetTrackAudio& op, IdSource& ids) {
    Timeline out = tl;
    if (const auto t = find_track(out, op.track_id); !t) {
        return std::unexpected(t.error());
    }
    if (op.volume.is_null() && op.muted.is_null() && op.audio_fx.is_null() && !py_truthy(op.fade)) {
        return invalid("pasa volume, muted, audio_fx y/o fade");
    }
    std::vector<std::string> changed;
    for (auto& c : out.clips) {
        if (c->track_id != op.track_id || (c->kind != "video" && c->kind != "audio")) {
            continue;
        }
        if (auto r = apply_audio(c.mut(), {op.volume, op.muted, op.audio_fx, op.replace_fx, op.fade, op.fade_dur}, ids);
            !r) {
            return std::unexpected(r.error());
        }
        changed.push_back(c->id);
    }
    if (changed.empty()) {
        return invalid("esa pista no tiene clips de audio o vídeo");
    }
    return EditResult{std::move(out), std::move(changed), {}};
}

Result<EditResult> run(const Timeline& tl, const ops::SetClipAudioFx& op, IdSource& ids) {
    Timeline out = tl;
    const auto ci = find_clip(out, op.clip_id);
    if (!ci) {
        return std::unexpected(ci.error());
    }
    TimelineClip& c = out.clips[*ci].mut();
    const Json none;
    const Json half = 0.5;
    if (auto r = apply_audio(c, {none, none, op.audio_fx, op.replace, none, half}, ids); !r) {
        return std::unexpected(r.error());
    }
    // Rampa: cada efecto sube desde `from` hasta su valor entre start y end (s locales).
    if (py_truthy(op.ramp)) {
        if (!op.ramp.is_object()) {
            return invalid("ramp debe ser {start, end, from?} en segundos del clip");
        }
        const double dur = clip_timeline_duration(c);
        const auto t0 = py_float(op.ramp.contains("start") ? op.ramp["start"] : Json(0.0));
        const auto t1 = op.ramp.contains("end") ? py_float(op.ramp["end"]) : std::nullopt;
        if (!t0 || !t1) {
            return invalid("ramp debe ser {start, end, from?} en segundos del clip");
        }
        GC_TRY(v0, to_py_float(op.ramp.contains("from") ? op.ramp["from"] : Json(0.0), "ramp.from"));
        if (!(0 <= *t0 && *t0 < *t1 && *t1 <= dur + 1e-6)) {
            return invalid("ramp: 0 <= start < end <= duración del clip");
        }
        if (!op.audio_fx.is_object()) {
            return invalid("ramp necesita audio_fx");
        }
        Json start_vals = Json::object();
        Json end_vals = Json::object();
        for (const auto& [key, value] : op.audio_fx.items()) {
            start_vals[key] = std::min(1.0, std::max(0.0, v0));
        }
        for (const auto& [key, value] : op.audio_fx.items()) {
            if (value.is_boolean() && value.get<bool>()) {
                end_vals[key] = 1.0;
                continue;
            }
            GC_TRY(v, to_py_float(value, key));
            end_vals[key] = std::min(1.0, std::max(0.0, v));
        }
        upsert_keyframe_at(c, *t0, start_vals, std::nullopt, ids);
        upsert_keyframe_at(c, *t1, end_vals, Interp::EaseInOut, ids);
    }
    return EditResult{std::move(out), {op.clip_id}, {}};
}

} // namespace gc::core::detail
