#include "gc/core/text_clip_ui.hpp"

#include <algorithm>
#include <cmath>

#include "gc/core/clip_pose_edit.hpp"
#include "gc/core/keyframes.hpp"
#include "gc/core/timeline_ui.hpp"
#include "gc/core/timeline_view.hpp"

namespace gc::core {

namespace {

constexpr double kTextDefaultDuration = 3; // TEXT_DEFAULT_DUR

const Json& field(const Json& obj, std::string_view key) {
    static const Json kNull;
    if (!obj.is_object()) {
        return kNull;
    }
    const auto it = obj.find(std::string(key));
    return it == obj.end() ? kNull : *it;
}

} // namespace

Json default_text_style() {
    return {{"font", "Arial"},
            {"size", 0.009375},
            {"color", "#ffffff"},
            {"bold", true},
            {"align", "center"},
            {"x", 0.5},
            {"y", 0.5},
            {"w", 0.8},
            {"border_width", 6},
            {"border_color", "#000000"},
            {"shadow", false},
            {"shadow_color", "#000000"},
            {"glow", false},
            {"bg", "none"},
            {"bg_opacity", 0.55},
            {"highlight_color", "#ffe566"},
            {"word_fx", "none"},
            {"block_appear", "none"},
            {"inactive_opacity", 1},
            {"active_opacity", 1},
            {"opacity", 1},
            {"max_words", 8},
            {"preset", "outline"}};
}

Json subtitle_style() {
    return {{"size", 0.048},          {"bold", true},           {"align", "center"},      {"x", 0.5},
            {"y", 0.86},              {"w", 0.88},              {"bg", "none"},           {"bg_opacity", 0.55},
            {"shadow", false},        {"glow", false},          {"inactive_opacity", 0.55}, {"active_opacity", 1},
            {"max_words", 8},         {"font", "Arial Black"},  {"color", "#ffffff"},     {"border_width", 5},
            {"border_color", "#111111"}, {"highlight_color", "#ffe566"}, {"word_fx", "highlight"},
            {"block_appear", "fade"}, {"theme", "classic"},     {"preset", "classic"},    {"opacity", 1}};
}

Json new_text_style(const Json& track_style) {
    Json style = default_text_style();
    if (track_style.is_object()) {
        for (const auto& [key, value] : track_style.items()) {
            style[key] = value;
        }
        return style;
    }
    const Json sub = subtitle_style();
    for (const char* key : {"x", "y", "w", "size", "opacity"}) {
        style[key] = sub.at(key);
    }
    return style;
}

Json make_text_clip_json(std::string_view track_id, double start, double duration, std::string_view text,
                         const Json& style, IdSource& ids) {
    const std::string body = text.empty() ? "Texto" : std::string(text);
    const double span = js_fixed3(std::max(0.5, duration));
    Json c = Json::object();
    c["id"] = make_uid(ids, "c");
    c["track_id"] = track_id;
    c["kind"] = "text";
    c["asset_kind"] = "text";
    c["asset_id"] = make_uid(ids, "t");
    c["filename"] = "";
    c["name"] = slice_utf16(body, 22);
    c["start"] = js_fixed3(std::max(0.0, start));
    c["in_point"] = 0;
    c["out_point"] = span;
    c["source_duration"] = span;
    c["volume"] = 1;
    c["muted"] = false;
    c["speed"] = 1;
    c["keep_pitch"] = true;
    c["reverse"] = false;
    c["speed_curve"] = nullptr;
    c["reframe"] = nullptr;
    c["text"] = body;
    c["style"] = style.is_object() ? style : default_text_style();
    c["words"] = Json::array();
    c["text_role"] = "free";
    c["description"] = nullptr;
    c["dup_of"] = nullptr;
    return c;
}

double new_text_duration(const Timeline& tl, double playhead) {
    double end = 0;
    for (const auto& c : tl.clips) {
        end = std::max(end, ui_clip_end(*c));
    }
    return std::max(kTextDefaultDuration, js_fixed3(end - playhead));
}

void change_text(TimelineClip& clip, std::string_view text) {
    clip.text = std::string(text);
    clip.name = slice_utf16(text.empty() ? "Texto" : std::string(text), 22);
}

void upsert_style_kf(TimelineClip& clip, double local_t, const Json& patch, double fps, IdSource& ids) {
    Json vals = Json::object();
    for (const auto key : kTextStyleKfKeys) {
        if (kf_value_valid(key, field(patch, key))) {
            vals[std::string(key)] = field(patch, key);
        }
    }
    if (vals.empty()) {
        return;
    }
    const double t = js_fixed(snap_to_frame(local_t, fps), 6);
    std::vector<Json> items = normalize_kf_items(clip.keyframes ? field(*clip.keyframes, "items") : Json(), ids);
    const auto hit = std::ranges::find_if(
        items, [&](const Json& k) { return std::abs(k.at("t").get<double>() - t) < frame_duration(fps) / 2; });
    if (hit != items.end()) {
        for (const auto& [key, value] : vals.items()) {
            (*hit)["props"][key] = value;
        }
    } else {
        const auto before = std::ranges::find_if(items.rbegin(), items.rend(),
                                                 [&](const Json& k) { return k.at("t").get<double>() < t; });
        const Json* prev = before != items.rend() ? &*before : items.empty() ? nullptr : &items.front();
        items.push_back({{"id", make_uid(ids, "k")},
                         {"t", t},
                         {"interpolation", interp_name(normalize_interp(prev ? prev->at("interpolation") : Json("linear")))},
                         {"props", vals}});
    }
    std::ranges::stable_sort(items, {}, [](const Json& k) { return k.at("t").get<double>(); });
    Json kf = clip.keyframes && clip.keyframes->is_object() ? *clip.keyframes : Json::object();
    kf["enabled"] = true;
    kf["items"] = items;
    clip.keyframes = std::move(kf);
}

void change_text_style(TimelineClip& clip, const Json& patch, double local_t, double fps, bool fixed, IdSource& ids) {
    if (clip.kind != "text" || !patch.is_object()) {
        return;
    }
    const std::vector<std::string> animated = fixed ? std::vector<std::string>{} : text_style_kf_keys(clip);
    Json style = clip.style && clip.style->is_object() ? *clip.style : Json::object();
    for (const auto& [key, value] : patch.items()) {
        style[key] = value;
    }
    clip.style = std::move(style);
    const double t = std::max(0.0, local_t);
    const Json pose = flatten_pose_patch(patch);
    if (!pose.empty() && should_keyframe(clip, pose)) {
        upsert_keyframe_ui(clip, t, pose, std::nullopt, fps, ids);
    }
    Json kf_style = Json::object();
    for (const auto& [key, value] : patch.items()) {
        if (std::ranges::find(animated, key) != animated.end()) {
            kf_style[key] = value;
        }
    }
    if (!kf_style.empty()) {
        upsert_style_kf(clip, t, kf_style, fps, ids);
    }
}

} // namespace gc::core
