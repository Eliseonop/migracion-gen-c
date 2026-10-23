#include "gc/core/fx_bake.hpp"

#include <algorithm>
#include <array>
#include <string>

#include "gc/core/clip_layout.hpp"
#include "gc/core/fx_catalog.hpp"
#include "gc/core/timeline_rules.hpp"
#include "json_reader.hpp"
#include "py.hpp"

namespace gc::core {

using detail::py_float;
using detail::py_int;
using detail::py_round;
using detail::py_str;
using detail::py_truthy;

namespace {

constexpr std::array<std::string_view, 3> kFxKinds{"video", "image", "motion"};

const Json& get(const Json& obj, std::string_view key) {
    static const Json kNull;
    const auto it = obj.find(key);
    return it == obj.end() ? kNull : *it;
}

} // namespace

Json active_vfx(const TimelineClip& clip) {
    Json out = Json::array();
    if (!clip.vfx || !clip.vfx->is_array()) {
        return out;
    }
    const auto cat = fx_catalog();
    for (const Json& e : *clip.vfx) {
        if (!e.is_object() || get(e, "enabled") == Json(false)) {
            continue;
        }
        const Json* man = cat ? cat->effect(get(e, "effect")) : nullptr;
        if (man == nullptr || get(*man, "engine") == Json("generator")) {
            continue;
        }
        // max(0.0, float(from or 0.0)); si no es un número, 0. Con NaN, max() se queda el 0.
        const Json& raw_from = get(e, "from");
        const double f = py_truthy(raw_from) ? py_float(raw_from).value_or(0.0) : 0.0;
        const double from = f > 0.0 ? f : 0.0;
        Json to;
        if (const Json& raw_to = get(e, "to"); !raw_to.is_null()) {
            if (const auto t = py_float(raw_to)) {
                to = *t > from ? *t : from; // max(frm, float(to))
            }
        }
        // int(e.get("v") or man.get("v") or 1): lo que no se pasa a entero es la versión del catálogo.
        std::int64_t v = 1;
        if (const Json& raw_v = get(e, "v"); py_truthy(raw_v)) {
            v = py_int(raw_v).value_or(1);
        } else if (const Json& man_v = get(*man, "v"); py_truthy(man_v)) {
            v = py_int(man_v).value_or(1);
        }
        auto params = FxCatalog::resolve_params(*man, get(e, "params"));
        Json entry = Json::object();
        entry["uid"] = py_truthy(get(e, "uid")) ? py_str(get(e, "uid")) : std::string();
        entry["effect"] = get(*man, "id");
        entry["v"] = v;
        entry["enabled"] = true;
        entry["params"] = params ? *params : Json::object();
        entry["from"] = from;
        entry["to"] = to;
        out.push_back(std::move(entry));
    }
    return out;
}

bool has_vfx(const TimelineClip& clip) {
    return std::ranges::find(kFxKinds, clip.kind) != kFxKinds.end() && !active_vfx(clip).empty();
}

double vfx_pad_fraction(const Json& entries) {
    double total = 0.0;
    const auto cat = fx_catalog();
    for (const Json& e : entries) {
        const Json* man = cat ? cat->effect(get(e, "effect")) : nullptr;
        if (man != nullptr && py_truthy(get(*man, "pad"))) {
            total += py_float(get(*man, "pad")).value_or(0.0);
        }
    }
    return total < 0.5 ? total : 0.5;
}

bool vfx_pad_allowed(const TimelineClip& clip, double iw, double ih, double W, double H) {
    if (!is_overlay(clip) || iw <= 0 || ih <= 0 || W <= 0 || H <= 0) {
        return false;
    }
    const double dur = clip_timeline_duration(clip);
    const double spd = clip_speed(clip);
    const double inp = clip.in_point;
    constexpr std::array<double, 3> kAt{0.0, 0.5, 1.0}; // principio, mitad y final
    return std::ranges::all_of(kAt, [&](double f) {
        const double lt = dur * f;
        const CropWindow w = crop_window(clip, iw / ih, W / H, inp + (lt * spd), lt);
        return w.wf >= 0.999 && w.hf >= 0.999;
    });
}

std::optional<Reframe> remap_reframe_to_local(const TimelineClip& clip) {
    if (!clip.reframe || clip.reframe->keyframes.empty()) {
        return clip.reframe;
    }
    Reframe rf = *clip.reframe;
    const double inp = clip.in_point;
    const double out = clip.out_point;
    const double spd = clip_speed(clip);
    for (Keyframe& k : rf.keyframes) {
        const double t = clip.reverse ? (out - k.t) / spd : (k.t - inp) / spd;
        k.t = py_round(t > 0.0 ? t : 0.0, 4);
    }
    return rf;
}

TimelineClip baked_clip(const TimelineClip& clip, std::string_view baked_filename) {
    const double dur = clip_timeline_duration(clip);
    TimelineClip c = clip;
    c.in_point = 0.0;
    c.out_point = dur;
    c.source_duration = dur;
    c.speed = 1.0;
    c.reverse = false;
    c.bg_removal = std::nullopt; // el alfa ya va en el horneado
    c.vfx = std::nullopt;
    c.reframe = remap_reframe_to_local(clip);
    c.filename = std::string(baked_filename);
    if (clip.kind == "image") {
        c.kind = "video";
        c.asset_kind = "clips";
        c.loop = std::nullopt;
    }
    return c;
}

} // namespace gc::core
