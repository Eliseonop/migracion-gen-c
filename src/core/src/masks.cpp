#include "gc/core/masks.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <utility>

#include "gc/core/keyframes.hpp"
#include "json_reader.hpp"
#include "py.hpp"

namespace gc::core {

using detail::py_iter;
using detail::py_num;
using detail::py_truthy;

namespace {

// Keyframe → campo de la máscara (solo anima masks[0]).
constexpr std::array<std::pair<std::string_view, std::string_view>, 8> kKfToField{{
    {"mx", "x"},
    {"my", "y"},
    {"mw", "w"},
    {"mh", "h"},
    {"msx", "scale_x"},
    {"msy", "scale_y"},
    {"mrot", "rotation"},
    {"mfeather", "feather"},
}};

// min(hi, max(lo, v)) de Python (con NaN se queda en lo, como Python).
double clamp(double v, double lo, double hi) { return std::min(hi, std::max(lo, v)); }

const Json& field(const Json& obj, std::string_view key) {
    static const Json kNull;
    if (!obj.is_object()) {
        return kNull;
    }
    const auto it = obj.find(key);
    return it == obj.end() ? kNull : *it;
}

bool one_of(const Json& v, std::initializer_list<std::string_view> options) {
    return v.is_string() && std::ranges::find(options, v.get<std::string>()) != options.end();
}

} // namespace

Json normalize_mask(const Json& raw) {
    const Json& m = raw;
    const Json& type_raw = field(m, "type");
    const std::string type =
        type_raw.is_string() && std::ranges::find(kMaskTypes, type_raw.get<std::string>()) != kMaskTypes.end()
            ? type_raw.get<std::string>()
            : "circle";
    const Json& enabled = field(m, "enabled");
    const Json& target = field(m, "target");

    Json out = Json::object();
    out["id"] = py_truthy(field(m, "id")) ? field(m, "id") : Json("m0");
    out["type"] = type;
    out["enabled"] = !(enabled.is_boolean() && !enabled.get<bool>()); // `is not False`
    out["x"] = py_num(field(m, "x"), 0.5);
    out["y"] = py_num(field(m, "y"), 0.5);
    out["w"] = clamp(py_num(field(m, "w"), 0.5), 0.01, 8.0);
    out["h"] = clamp(py_num(field(m, "h"), 0.5), 0.01, 8.0);
    out["scale_x"] = clamp(py_num(field(m, "scale_x"), 1.0), 0.02, 12.0);
    out["scale_y"] = clamp(py_num(field(m, "scale_y"), 1.0), 0.02, 12.0);
    out["rotation"] = py_num(field(m, "rotation"), 0.0);
    out["feather"] = clamp(py_num(field(m, "feather"), 0.0), 0.0, kMaskFeatherMax);
    out["invert"] = py_truthy(field(m, "invert"));
    out["opacity"] = clamp(py_num(field(m, "opacity"), 1.0), 0.0, 1.0);
    out["radius"] = clamp(py_num(field(m, "radius"), 0.0), 0.0, 0.5);
    out["target"] = one_of(target, {"clip", "adjust"}) ? target : Json("clip");

    if (type == "text") {
        const Json& t = field(m, "text").is_object() ? field(m, "text") : Json::object();
        const Json& align = field(t, "align");
        const Json& content = field(t, "content");
        const Json& font = field(t, "font");
        // Python: `_clamp(_num(weight, 700), 100, 900)` con límites ENTEROS: un valor
        // en el límite (o ausente) sale como entero; uno dentro, como real.
        Json weight;
        const auto w = detail::py_float(field(t, "weight"));
        if (!w || std::isnan(*w)) {
            weight = 700;
        } else if (!(*w > 100.0)) {
            weight = 100;
        } else if (!(*w < 900.0)) {
            weight = 900;
        } else {
            weight = *w;
        }
        Json text = Json::object();
        text["content"] = content.is_string() ? content : Json("TEXTO");
        text["font"] = py_truthy(font) ? font : Json("Anton");
        text["size"] = clamp(py_num(field(t, "size"), 0.22), 0.02, 1.5);
        text["weight"] = std::move(weight);
        text["align"] = one_of(align, {"left", "center", "right"}) ? align : Json("center");
        out["text"] = std::move(text);
    }
    if (type == "brush") {
        const Json& b = field(m, "brush").is_object() ? field(m, "brush") : Json::object();
        Json points = Json::array();
        const Json& raw_points = field(b, "points");
        if (py_truthy(raw_points)) {
            for (const Json& p : py_iter(raw_points).value_or(std::vector<Json>{})) {
                if (!p.is_object()) {
                    continue;
                }
                const double nan = std::numeric_limits<double>::quiet_NaN();
                const double x = py_num(field(p, "x"), nan);
                const double y = py_num(field(p, "y"), nan);
                if (std::isnan(x) || std::isnan(y)) {
                    continue;
                }
                Json pt = {{"x", x}, {"y", y}};
                if (py_truthy(field(p, "m"))) {
                    pt["m"] = 1;
                }
                points.push_back(std::move(pt));
            }
        }
        out["brush"] = {{"size", clamp(py_num(field(b, "size"), 0.12), 0.005, 1.0)}, {"points", std::move(points)}};
    }
    return out;
}

std::vector<Json> clip_masks(const TimelineClip& clip) {
    std::vector<Json> out;
    for (const Json& m : clip.masks) {
        if (m.is_object()) {
            out.push_back(normalize_mask(m));
        }
    }
    return out;
}

bool maskable(const TimelineClip& clip) {
    return clip.kind == "video" || clip.kind == "image" || clip.kind == "shape" || clip.kind == "text";
}

bool has_mask(const TimelineClip& clip) {
    return std::ranges::any_of(clip_masks(clip), [](const Json& m) {
        return m["enabled"].get<bool>() && m["target"] == "clip";
    });
}

bool has_adjust_mask(const TimelineClip& clip) {
    return std::ranges::any_of(clip_masks(clip), [](const Json& m) {
        return m["enabled"].get<bool>() && m["target"] == "adjust";
    });
}

namespace {

Json mask_from_props(const Json& mask, const KfProps& props) {
    Json patched = mask;
    for (const auto& [key, name] : kKfToField) {
        patched[std::string(name)] = props.get(key);
    }
    return normalize_mask(patched);
}

} // namespace

std::vector<Json> clip_masks_at(const TimelineClip& clip, double local_t, bool include_adjust) {
    std::vector<Json> masks;
    for (Json& m : clip_masks(clip)) {
        if (m["enabled"].get<bool>()) {
            masks.push_back(std::move(m));
        }
    }
    if (masks.empty()) {
        return masks;
    }
    if (keyframes_enabled(clip)) {
        masks[0] = mask_from_props(masks[0], clip_props_at(clip, local_t));
    }
    if (!include_adjust) {
        std::erase_if(masks, [](const Json& m) { return m["target"] != "clip"; });
    }
    return masks;
}

bool mask_animates(const TimelineClip& clip, double duration, double eps) {
    if (!has_mask(clip) || !keyframes_enabled(clip)) {
        return false;
    }
    const Json& items = field(*clip.keyframes, "items");
    if (!py_truthy(items)) {
        return false;
    }
    const double dur = std::max(0.0, duration);
    std::set<double> times{0.0, dur};
    for (const Json& it : py_iter(items).value_or(std::vector<Json>{})) {
        if (it.is_object()) {
            times.insert(clamp(py_num(field(it, "t"), 0.0), 0.0, dur));
        }
    }
    std::vector<KfProps> samples;
    for (const double t : times) {
        samples.push_back(clip_props_at(clip, t));
    }
    for (const auto& [key, name] : kKfToField) {
        double lo = samples.front().get(key);
        double hi = lo;
        for (const auto& s : samples) {
            lo = std::min(lo, s.get(key));
            hi = std::max(hi, s.get(key));
        }
        if (hi - lo > eps) {
            return true;
        }
    }
    return false;
}

} // namespace gc::core
