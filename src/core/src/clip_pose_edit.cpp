#include "gc/core/clip_pose_edit.hpp"

#include <algorithm>
#include <cmath>

#include "gc/core/keyframes.hpp"
#include "gc/core/pose.hpp"
#include "gc/core/timeline_ui.hpp"
#include "gc/core/timeline_view.hpp"

namespace gc::core {

namespace {

constexpr std::array<std::string_view, 11> kAudioFxKeys{"eq",      "compressor", "reverb",     "echo",
                                                        "denoise", "distortion", "underwater", "telephone",
                                                        "radio",   "megaphone",  "muffled"};

const Json& field(const Json& obj, std::string_view key) {
    static const Json none;
    if (!obj.is_object()) {
        return none;
    }
    const auto it = obj.find(std::string(key));
    return it == obj.end() ? none : *it;
}

bool truthy(const Json& v) {
    if (v.is_null()) {
        return false;
    }
    if (v.is_boolean()) {
        return v.get<bool>();
    }
    if (v.is_number()) {
        const double n = v.get<double>();
        return n != 0 && !std::isnan(n);
    }
    if (v.is_string()) {
        return !v.get_ref<const std::string&>().empty();
    }
    return true;
}

// `patch[key] != null` de JS.
bool given(const Json& patch, std::string_view key) {
    return !field(patch, key).is_null();
}

bool has_pose_props(const Json& k) {
    const Json& props = field(k, "props");
    return std::ranges::any_of(kKfPropKeys, [&](std::string_view key) {
        const Json& v = field(props, key);
        return !v.is_null() && std::isfinite(js_number(v));
    });
}

// {...obj, key: value} sobre un dict opcional del clip.
void set_in(std::optional<Json>& dict, std::string_view key, const Json& value) {
    if (!dict || !dict->is_object()) {
        dict = Json::object();
    }
    (*dict)[std::string(key)] = value;
}

} // namespace

double snap_to_frame(double t, double fps) {
    const int f = normalize_fps(fps);
    return std::isfinite(t) ? std::floor(t * f + 0.5) / f : 0; // Math.round
}

std::vector<Json> normalize_kf_items(const Json& items, IdSource& ids) {
    std::vector<Json> out;
    if (!items.is_array()) {
        return out;
    }
    for (const Json& k : items) {
        const double t = k.is_object() && k.contains("t") ? js_number(k.at("t")) : std::nan("");
        if (!std::isfinite(t)) {
            continue;
        }
        const Json& interp = truthy(field(k, "interpolation")) ? field(k, "interpolation") : field(k, "ease");
        Json item = Json::object();
        item["id"] = truthy(field(k, "id")) ? field(k, "id") : Json(make_uid(ids, "k"));
        item["t"] = t;
        item["interpolation"] = interp_name(normalize_interp(interp));
        if (const auto bez = normalize_bezier(field(k, "bezier"))) {
            item["bezier"] = Json::array({(*bez)[0], (*bez)[1], (*bez)[2], (*bez)[3]});
        }
        item["props"] = field(k, "props").is_object() ? field(k, "props") : Json::object();
        out.push_back(std::move(item));
    }
    std::ranges::stable_sort(out, {}, [](const Json& k) { return k.at("t").get<double>(); });
    return out;
}

bool pose_keyframed(const TimelineClip& clip) {
    if (!clip.keyframes) {
        return false;
    }
    const Json& items = field(*clip.keyframes, "items");
    return items.is_array() && std::ranges::any_of(items, [](const Json& k) {
               return k.is_object() && k.contains("t") && std::isfinite(js_number(k.at("t"))) &&
                      has_pose_props(k);
           });
}

bool should_keyframe(const TimelineClip& clip, const Json& patch) {
    if (keyframes_enabled(clip) && pose_keyframed(clip)) {
        return true;
    }
    return given(patch, "cx") || given(patch, "cy");
}

void upsert_keyframe_ui(TimelineClip& clip, double local_t, const Json& patch,
                        const std::optional<std::string>& interpolation, double fps, IdSource& ids) {
    const double t = js_fixed(snap_to_frame(local_t, fps), 6);
    const double snap = frame_duration(fps) / 2;
    std::vector<Json> items =
        normalize_kf_items(clip.keyframes ? field(*clip.keyframes, "items") : Json(), ids);
    TimelineClip enabled = clip;
    enabled.keyframes = Json{{"enabled", true}, {"items", Json(items)}};
    KfProps base = static_props(enabled); // snapshotProps: el encuadre animado manda en cx, cy y zoom
    if (enabled.reframe) {
        const Reframe& rf = *enabled.reframe;
        const Frame fr = frame_at(rf.keyframes, enabled.in_point + t, rf.zoom, rf.pan_mode);
        base.set("cx", std::isfinite(fr.cx) ? fr.cx : 0.5);
        base.set("cy", std::isfinite(fr.cy) ? fr.cy : 0.5);
        base.set("zoom", std::isfinite(fr.zoom) ? fr.zoom : rf.zoom);
    }
    KfProps current = clip_props_over(enabled, t, base).value_or(base);
    for (const auto key : kKfPropKeys) {
        const Json& v = field(patch, key);
        if (!v.is_null() && std::isfinite(js_number(v))) {
            current.set(key, js_number(v));
        }
    }
    const Json props = current.to_json();
    const auto hit = std::ranges::find_if(
        items, [&](const Json& k) { return std::abs(k.at("t").get<double>() - t) < snap; });
    if (hit != items.end()) {
        (*hit)["t"] = t;
        for (const auto& [key, value] : props.items()) {
            (*hit)["props"][key] = value;
        }
        if (interpolation && !interpolation->empty()) {
            (*hit)["interpolation"] = interp_name(normalize_interp(*interpolation));
        }
    } else {
        const Json* prev = items.empty() ? nullptr : &items.back();
        const bool own = interpolation && !interpolation->empty();
        Json item = Json::object();
        item["id"] = make_uid(ids, "k");
        item["t"] = t;
        item["interpolation"] = interp_name(normalize_interp(own    ? Json(*interpolation)
                                                             : prev ? prev->at("interpolation")
                                                                    : Json("linear")));
        if (!own && prev && prev->contains("bezier")) {
            item["bezier"] = prev->at("bezier");
        }
        item["props"] = props;
        items.push_back(std::move(item));
    }
    std::ranges::stable_sort(items, {}, [](const Json& k) { return k.at("t").get<double>(); });
    clip.keyframes = Json{{"enabled", true}, {"items", Json(items)}};
}

void apply_static_pose(TimelineClip& clip, const Json& patch) {
    if (clip.kind == "shape" || clip.kind == "text") {
        std::optional<Json>& dict = clip.kind == "shape" ? clip.shape : clip.style;
        if (!dict || !dict->is_object()) {
            dict = Json::object();
        }
        const auto keys =
            clip.kind == "shape"
                ? std::vector<std::string_view>{"x", "y", "scale", "rotation", "opacity"}
                : std::vector<std::string_view>{"x", "y", "scale", "rotation", "opacity", "rot_x", "rot_y"};
        for (const auto key : keys) {
            if (given(patch, key)) {
                (*dict)[std::string(key)] = patch.at(std::string(key));
            }
        }
        if (clip.kind == "shape" && given(patch, "draw")) {
            (*dict)["draw"] = std::min(1.0, std::max(0.0, js_number(patch.at("draw"))));
        }
        return;
    }
    if (given(patch, "volume")) {
        clip.volume = clamp_volume(patch.at("volume"));
    }
    for (const auto key : kAudioFxKeys) {
        const Json& v = field(patch, key);
        if (!v.is_null() && std::isfinite(js_number(v))) {
            set_in(clip.audio_fx, key, std::min(1.0, std::max(0.0, js_number(v))));
        }
    }
    if (given(patch, "opacity")) {
        clip.opacity = js_number(patch.at("opacity"));
    }
    if (clip.layout == "overlay" || clip.transform) {
        Json tr{{"x", 0.5}, {"y", 0.5}, {"scale", 1}, {"rotation", 0}};
        if (clip.transform && clip.transform->is_object()) {
            for (const auto& [key, value] : clip.transform->items()) {
                tr[key] = value;
            }
        }
        for (const char* key : {"x", "y", "scale", "rotation"}) {
            if (given(patch, key)) {
                tr[key] = patch.at(key);
            }
        }
        clip.transform = std::move(tr);
        if (clip.frame.empty() || clip.frame == "free") {
            clip.frame = "free";
        }
    }
    if (given(patch, "cx") || given(patch, "cy") || given(patch, "zoom")) {
        if (!clip.reframe) {
            clip.reframe = Reframe{};
        }
        if (given(patch, "zoom")) {
            clip.reframe->zoom = js_number(patch.at("zoom"));
        }
    }
}

Json flatten_pose_patch(const Json& patch) {
    Json out = Json::object();
    for (const char* key : {"x", "y", "scale", "rotation", "opacity"}) {
        const Json& v = field(patch, key);
        if (!v.is_null() && std::isfinite(js_number(v))) {
            out[key] = js_number(v);
        }
    }
    for (const char* key : {"cx", "cy", "zoom"}) {
        if (!field(patch, key).is_null()) {
            out[key] = js_number(field(patch, key));
        }
    }
    return out;
}

void change_shape(TimelineClip& clip, const Json& patch, double local_t, double fps, IdSource& ids) {
    if (clip.kind != "shape" || !patch.is_object()) {
        return;
    }
    Json shape = clip.shape && clip.shape->is_object() ? *clip.shape : Json::object();
    for (const auto& [key, value] : patch.items()) {
        shape[key] = value;
    }
    clip.shape = std::move(shape);
    const Json pose = flatten_pose_patch(patch);
    if (!pose.empty() && should_keyframe(clip, pose)) {
        upsert_keyframe_ui(clip, std::max(0.0, local_t), pose, std::nullopt, fps, ids);
    }
}

void commit_pose(TimelineClip& clip, const Json& patch, double local_t, double fps, IdSource& ids) {
    if (clip.kind != "video" && clip.kind != "image" && clip.kind != "shape" && clip.kind != "text" &&
        clip.kind != "audio") {
        return; // canKeyframe
    }
    apply_static_pose(clip, patch);
    if (should_keyframe(clip, patch)) {
        upsert_keyframe_ui(clip, std::max(0.0, local_t), patch, std::nullopt, fps, ids);
    }
}

namespace {

double timeline_to_source(const TimelineClip& c, double local_t) {
    const double local = local_t * ui_clip_speed(c);
    return c.reverse ? c.out_point - local : c.in_point + local;
}

std::vector<Json> items_of(const TimelineClip& clip, IdSource& ids) {
    return normalize_kf_items(clip.keyframes ? field(*clip.keyframes, "items") : Json(), ids);
}

std::optional<std::string> reframe_kf_id_near(const TimelineClip& clip, double t, double snap) {
    if (!clip.reframe) {
        return std::nullopt;
    }
    for (const Keyframe& k : clip.reframe->keyframes) {
        if (std::abs(k.t - t) < snap) {
            const Json& id = field(k.extra, "id");
            return truthy(id) ? std::optional(id.is_string() ? id.get<std::string>() : id.dump())
                              : std::optional<std::string>("");
        }
    }
    return std::nullopt;
}

} // namespace

std::vector<std::string> text_style_kf_keys(const TimelineClip& clip) {
    SequentialIdSource ids;
    std::vector<std::string> out;
    const auto items = items_of(clip, ids);
    for (const auto key : kTextStyleKfKeys) {
        const bool used = std::ranges::any_of(
            items, [&](const Json& k) { return kf_value_valid(key, field(k.at("props"), key)); });
        if (used) {
            out.emplace_back(key);
        }
    }
    return out;
}

std::string kf_state(const TimelineClip& clip, double local_t, double fps) {
    SequentialIdSource ids;
    const double t = js_fixed(snap_to_frame(local_t, fps), 6);
    const double snap = frame_duration(fps) / 2;
    std::vector<Json> posed;
    std::ranges::copy_if(items_of(clip, ids), std::back_inserter(posed), has_pose_props);
    if (std::ranges::any_of(posed,
                            [&](const Json& k) { return std::abs(k.at("t").get<double>() - t) < snap; }) ||
        reframe_kf_id_near(clip, t, snap)) {
        return "on";
    }
    if (!keyframes_enabled(clip)) {
        return "off";
    }
    return !posed.empty() || text_style_kf_keys(clip).empty() ? "empty" : "off";
}

std::optional<std::string> keyframe_id_at(const TimelineClip& clip, double local_t, double fps) {
    SequentialIdSource ids;
    const double t = js_fixed(snap_to_frame(local_t, fps), 6);
    const double snap = frame_duration(fps) / 2;
    for (const Json& k : items_of(clip, ids)) {
        if (std::abs(k.at("t").get<double>() - t) < snap) {
            return k.at("id").is_string() ? k.at("id").get<std::string>() : k.at("id").dump();
        }
    }
    auto rf = reframe_kf_id_near(clip, t, snap);
    return rf && !rf->empty() ? rf : std::nullopt;
}

void toggle_keyframe_at(TimelineClip& clip, double local_t, double fps, IdSource& ids) {
    if (clip.kind != "video" && clip.kind != "image" && clip.kind != "shape" && clip.kind != "text" &&
        clip.kind != "audio") {
        return;
    }
    const double t = std::min(ui_clip_duration(clip), std::max(0.0, local_t));
    if (kf_state(clip, t, fps) == "on") {
        const auto id = keyframe_id_at(clip, t, fps);
        std::vector<Json> items;
        for (Json& k : items_of(clip, ids)) {
            if (k.at("id") != Json(id.value_or(""))) {
                items.push_back(std::move(k));
                continue;
            }
            if (clip.kind == "text" &&
                !text_style_kf_keys(clip).empty()) { // removePoseKf: se queda el estilo
                Json style = Json::object();
                for (const auto key : kTextStyleKfKeys) {
                    if (kf_value_valid(key, field(k.at("props"), key))) {
                        style[std::string(key)] = k.at("props").at(std::string(key));
                    }
                }
                if (!style.empty()) {
                    k["props"] = std::move(style);
                    items.push_back(std::move(k));
                }
            }
        }
        if (clip.kind == "text" && !text_style_kf_keys(clip).empty()) { // removePoseKf
            Json prev = clip.keyframes && clip.keyframes->is_object() ? *clip.keyframes : Json::object();
            prev["items"] = items;
            clip.keyframes = std::move(prev);
        } else if (keyframes_enabled(clip)) { // deleteKeyframeItem
            const bool enabled = !items.empty();
            clip.keyframes = Json{{"enabled", enabled}, {"items", items}};
        } else if (clip.reframe && id) { // el rombo marcaba un keyframe del encuadre
            const auto other = [&](const Keyframe& k) { return field(k.extra, "id") == Json(*id); };
            std::erase_if(clip.reframe->keyframes, other);
            std::erase_if(clip.reframe->keyframes2, other);
        }
        return;
    }
    // enableKeyframes y luego upsertKeyframeAt con el parche vacío.
    const Json prev = clip.keyframes && clip.keyframes->is_object() ? *clip.keyframes : Json::object();
    std::vector<Json> items = items_of(clip, ids);
    if (truthy(field(prev, "enabled")) && !items.empty()) {
        Json next = prev;
        next["enabled"] = true;
        next["items"] = items;
        clip.keyframes = std::move(next);
    } else {
        if (items.empty()) {
            TimelineClip snap = clip;
            snap.keyframes.reset();
            KfProps base = static_props(snap);
            if (snap.reframe) {
                const double src = std::min(
                    clip.out_point, std::max(clip.in_point, timeline_to_source(clip, local_t))); // srcTOf
                const Frame fr =
                    frame_at(snap.reframe->keyframes, src, snap.reframe->zoom, snap.reframe->pan_mode);
                base.set("cx", std::isfinite(fr.cx) ? fr.cx : 0.5);
                base.set("cy", std::isfinite(fr.cy) ? fr.cy : 0.5);
                base.set("zoom", std::isfinite(fr.zoom) ? fr.zoom : snap.reframe->zoom);
            }
            items.push_back({{"id", make_uid(ids, "k")},
                             {"t", js_fixed(snap_to_frame(t, 30), 6)},
                             {"interpolation", "linear"},
                             {"props", base.to_json()}});
        }
        clip.keyframes = Json{{"enabled", true}, {"items", items}};
    }
    upsert_keyframe_ui(clip, t, Json::object(), std::nullopt, fps, ids);
}

void apply_volume_fade_ui(TimelineClip& clip, double duration, const std::string& side, IdSource& ids,
                          double fade) {
    const double dur = std::max(0.05, std::isfinite(duration) ? duration : 0);
    const double window = std::min(std::max(0.05, std::isfinite(fade) ? fade : 0.5), dur / 2);
    const double peak = std::min(2.0, std::max(0.0, std::isfinite(clip.volume) ? clip.volume : 1.0));
    const double target = peak > 0.001 ? peak : 1;
    clip.volume = target;
    if (side == "in") {
        upsert_keyframe_ui(clip, 0, {{"volume", 0}}, "ease-out", 30, ids);
        upsert_keyframe_ui(clip, window, {{"volume", target}}, "ease-out", 30, ids);
    } else {
        upsert_keyframe_ui(clip, std::max(0.0, dur - window), {{"volume", target}}, "ease-in", 30, ids);
        upsert_keyframe_ui(clip, dur, {{"volume", 0}}, "ease-in", 30, ids);
    }
}

} // namespace gc::core
