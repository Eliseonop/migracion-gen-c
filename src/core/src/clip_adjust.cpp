#include "gc/core/clip_adjust.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

#include "gc/core/masks.hpp"
#include "json_reader.hpp"
#include "py.hpp"

namespace gc::core {

namespace {

const Json kEmptyObject = Json::object();

const Json& effects_of(const TimelineClip& clip) {
    return clip.effects && clip.effects->is_object() ? *clip.effects : kEmptyObject;
}

// `_num` de clip_adjust: True = 1; float(v or 0); lo que falla o no es finito, 0.
double adjust_num(const Json& v) {
    if (v.is_boolean() && v.get<bool>()) {
        return 1.0;
    }
    if (!detail::py_truthy(v)) {
        return 0.0;
    }
    const auto n = detail::py_float(v);
    return n && std::isfinite(*n) ? *n : 0.0;
}

const Json& effect(const Json& effects, std::string_view key) {
    static const Json kNull;
    if (!effects.is_object()) {
        return kNull;
    }
    const auto it = effects.find(key);
    return it == effects.end() ? kNull : *it;
}

std::pair<double, double> range_of(std::string_view key) {
    return key == "hue" ? std::pair{-180.0, 180.0} : std::pair{-1.0, 1.0};
}

double gain(double exposure, double whites) {
    const double k = std::pow(2.0, exposure);
    return whites > 0 ? k / (1.0 - 0.6 * whites) : k * (1.0 + 0.4 * whites);
}

// Rotación de tono de la especificación de filtros CSS/SVG (hueRotate).
ColorMatrix hue_matrix(double deg) {
    const double rad = deg * (std::numbers::pi / 180.0); // math.radians
    const double a = std::cos(rad);
    const double b = std::sin(rad);
    return {{
        {0.213 + 0.787 * a - 0.213 * b, 0.715 - 0.715 * a - 0.715 * b, 0.072 - 0.072 * a + 0.928 * b},
        {0.213 - 0.213 * a + 0.143 * b, 0.715 + 0.285 * a + 0.140 * b, 0.072 - 0.072 * a - 0.283 * b},
        {0.213 - 0.213 * a - 0.787 * b, 0.715 - 0.715 * a + 0.715 * b, 0.072 + 0.928 * a + 0.072 * b},
    }};
}

} // namespace

double adjust_value(const Json& effects, std::string_view key) {
    const auto [lo, hi] = range_of(key);
    return std::min(hi, std::max(lo, adjust_num(effect(effects, key))));
}

std::optional<ColorMatrix> color_matrix(const Json& effects) {
    const double ex = adjust_value(effects, "exposure");
    const double wh = adjust_value(effects, "whites");
    const double te = adjust_value(effects, "temperature");
    const double hu = adjust_value(effects, "hue");
    if (ex == 0 && wh == 0 && te == 0 && hu == 0) {
        return std::nullopt;
    }
    const double k = gain(ex, wh);
    const std::array<double, 3> diag{k * (1.0 + 0.18 * te), k, k * (1.0 - 0.18 * te)};
    ColorMatrix m = hue_matrix(hu);
    for (auto& row : m) {
        for (std::size_t c = 0; c < 3; ++c) {
            row[c] *= diag[c];
        }
    }
    return m;
}

std::optional<ColorMatrix> color_matrix(const TimelineClip& clip) { return color_matrix(effects_of(clip)); }

bool has_color_adjust(const Json& effects) {
    return std::ranges::any_of(kColorKeys, [&](std::string_view k) { return adjust_num(effect(effects, k)) != 0; });
}

bool has_color_adjust(const TimelineClip& clip) { return has_color_adjust(effects_of(clip)); }

Json strip_color(const Json& effects) {
    Json out = Json::object();
    if (!effects.is_object()) {
        return out;
    }
    for (const auto& [key, value] : effects.items()) {
        if (std::ranges::find(kColorKeys, key) == kColorKeys.end()) {
            out[key] = value;
        }
    }
    return out;
}

std::vector<ClipPass> adjust_passes(const Cow<TimelineClip>& clip) {
    const std::string& kind = clip->kind;
    if ((kind != "video" && kind != "image" && kind != "shape") || !has_adjust_mask(*clip) ||
        !has_color_adjust(*clip)) {
        return {{clip, false}};
    }
    TimelineClip base = *clip;
    base.effects = strip_color(effects_of(*clip));
    TimelineClip ghost = *clip;
    ghost.id += kGhostSuffix;
    ghost.masks.clear();
    for (Json m : clip_masks(*clip)) {
        m["target"] = "clip";
        ghost.masks.push_back(std::move(m));
    }
    ghost.muted = true;
    ghost.volume = 0.0;
    return {{Cow<TimelineClip>(std::move(base)), false}, {Cow<TimelineClip>(std::move(ghost)), true}};
}

} // namespace gc::core
