#include "gc/core/mask_edit.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

#include "gc/core/clip_pose_edit.hpp"
#include "gc/core/masks.hpp"
#include "gc/core/timeline_ui.hpp"

namespace gc::core {

namespace {

constexpr std::array<std::pair<std::string_view, std::string_view>, 8> kMaskFields{
    {{"mx", "x"}, {"my", "y"}, {"mw", "w"}, {"mh", "h"}, {"msx", "scale_x"}, {"msy", "scale_y"},
     {"mrot", "rotation"}, {"mfeather", "feather"}}};

Json with_id(Json mask, IdSource& ids) {
    if (!mask.contains("id") || mask.at("id").is_null() || mask.at("id") == "" || mask.at("id") == "m0") {
        mask["id"] = make_uid(ids, "m");
    }
    return mask;
}

void set_masks(TimelineClip& clip, std::vector<Json> masks) { clip.masks = std::move(masks); }

} // namespace

Json default_mask(std::string_view type, double out_aspect, IdSource& ids) {
    const double a = std::isfinite(out_aspect) && out_aspect > 0 ? out_aspect : 0.5625;
    const double wide = std::clamp(0.62 * a, 0.16, 1.6);
    const std::string kind =
        std::ranges::find(kMaskTypes, type) != kMaskTypes.end() ? std::string(type) : std::string("circle");
    Json m = {{"id", make_uid(ids, "m")}, {"type", kind}, {"enabled", true}, {"x", 0.5},     {"y", 0.5},
              {"w", wide},                {"h", 0.42},    {"scale_x", 1},    {"scale_y", 1}, {"rotation", 0},
              {"feather", 0},             {"invert", false}, {"opacity", 1}, {"radius", 0}};
    if (kind == "circle") {
        m["w"] = 0.5;
        m["h"] = 0.5;
    } else if (kind == "star" || kind == "heart") {
        m["w"] = 0.55;
        m["h"] = 0.55;
    } else if (kind == "linear") {
        m["w"] = 2.4;
        m["h"] = 2.4;
    } else if (kind == "film") {
        m["w"] = 2.4;
        m["h"] = 0.3;
    } else if (kind == "text") {
        m["h"] = 0.3;
        m["text"] = {{"content", "TEXTO"}, {"font", "Anton"}, {"size", 0.22}, {"weight", 700}, {"align", "center"}};
    } else if (kind == "brush") {
        m["w"] = 1;
        m["h"] = 1;
        m["brush"] = {{"size", 0.12}, {"points", Json::array()}};
    }
    return m;
}

void add_mask(TimelineClip& clip, std::string_view type, std::string_view target, double out_aspect, IdSource& ids) {
    Json mask = default_mask(type, out_aspect, ids);
    mask["target"] = target;
    std::vector<Json> masks{std::move(mask)};
    for (const Json& m : clip_masks(clip)) {
        masks.push_back(m);
    }
    set_masks(clip, std::move(masks));
}

void remove_mask(TimelineClip& clip) {
    auto masks = clip_masks(clip);
    if (!masks.empty()) {
        masks.erase(masks.begin());
        set_masks(clip, std::move(masks));
    }
}

void duplicate_mask(TimelineClip& clip, IdSource& ids) {
    auto masks = clip_masks(clip);
    if (masks.empty()) {
        return;
    }
    Json copy = masks.front();
    copy["id"] = make_uid(ids, "m");
    copy["x"] = copy.at("x").get<double>() + 0.06;
    copy["y"] = copy.at("y").get<double>() + 0.06;
    masks.insert(masks.begin() + 1, std::move(copy));
    set_masks(clip, std::move(masks));
}

void change_mask(TimelineClip& clip, const Json& patch, double out_aspect, IdSource& ids) {
    auto masks = clip_masks(clip);
    if (masks.empty() || !patch.is_object()) {
        return;
    }
    const Json& first = masks.front();
    const Json type = patch.value("type", Json());
    const bool truthy = type.is_string() ? !type.get_ref<const std::string&>().empty()
                        : type.is_boolean() ? type.get<bool>()
                                            : type.is_number() && type.get<double>() != 0;
    if (truthy && type != first.at("type")) {
        Json base = default_mask(type.is_string() ? type.get<std::string>() : "", out_aspect, ids);
        for (const char* key : {"id", "enabled", "x", "y", "rotation", "feather", "invert", "opacity", "target"}) {
            base[key] = first.at(key);
        }
        masks.front() = std::move(base);
        if (keyframes_enabled(clip) && clip.keyframes->contains("items") && clip.keyframes->at("items").is_array()) {
            Json kf = *clip.keyframes; // dropMaskSizeKfs
            for (Json& k : kf["items"]) {
                if (k.is_object() && k.contains("props") && k.at("props").is_object()) {
                    for (const char* key : {"mw", "mh", "msx", "msy"}) {
                        k["props"].erase(key);
                    }
                }
            }
            clip.keyframes = std::move(kf);
        }
    } else {
        Json merged = first;
        for (const auto& [key, value] : patch.items()) {
            merged[key] = value;
        }
        masks.front() = with_id(normalize_mask(merged), ids);
    }
    set_masks(clip, std::move(masks));
}

void commit_mask(TimelineClip& clip, const Json& patch, double local_t, double fps, IdSource& ids) {
    auto masks = clip_masks(clip);
    if (masks.empty() || !patch.is_object()) {
        return;
    }
    const bool animate = should_keyframe(clip, patch);
    Json first = masks.front(); // applyStaticMask
    for (const auto& [kf_key, field] : kMaskFields) {
        const auto it = patch.find(std::string(kf_key));
        if (it != patch.end() && !it->is_null() && std::isfinite(js_number(*it))) {
            first[std::string(field)] = js_number(*it);
        }
    }
    masks.front() = with_id(normalize_mask(first), ids);
    set_masks(clip, std::move(masks));
    if (animate) {
        upsert_keyframe_ui(clip, std::max(0.0, local_t), patch, std::nullopt, fps, ids);
    }
}

} // namespace gc::core
