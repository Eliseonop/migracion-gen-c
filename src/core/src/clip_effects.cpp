#include "gc/core/clip_effects.hpp"

#include <algorithm>
#include <cmath>
#include <string_view>

#include "json_reader.hpp"
#include "py.hpp"

namespace gc::core {

namespace {

const Json& field(const Json& effects, std::string_view key) {
    static const Json kNull;
    if (!effects.is_object()) {
        return kNull;
    }
    const auto it = effects.find(key);
    return it == effects.end() ? kNull : *it;
}

// _fx_num de clip_fx.py: True = 1; float(val or 0); lo que no se puede leer, 0 (NaN e inf se
// quedan como están).
double fx_num(const Json& effects, std::string_view key) {
    const Json& v = field(effects, key);
    if (v.is_boolean() && v.get<bool>()) {
        return 1.0;
    }
    if (!detail::py_truthy(v)) {
        return 0.0;
    }
    return detail::py_float(v).value_or(0.0);
}

// _fx_on: True; False o None no; si no, float(val) > 0.
bool fx_on(const Json& effects, std::string_view key) {
    const Json& v = field(effects, key);
    if (v.is_boolean()) {
        return v.get<bool>();
    }
    if (v.is_null()) {
        return false;
    }
    const auto n = detail::py_float(v);
    return n && *n > 0;
}

// El número tal como lo lee FFmpeg tras escribirlo con `digits` decimales (inf se queda en inf).
double written(double v, int digits) { return std::isfinite(v) ? detail::py_round(v, digits) : v; }

} // namespace

bool ClipEffects::any() const noexcept {
    const bool eq_changes = eq && (eq->brightness != 0.0 || eq->contrast != 1.0 || eq->saturation != 1.0);
    return blur > 0 || sharpen > 0 || grayscale || sepia || vhs || grain > 0 || pixelate >= 2 || eq_changes ||
           adjust.has_value();
}

ClipEffects clip_effects(const Json& effects) {
    ClipEffects fx;
    if (const double blur = fx_num(effects, "blur"); blur > 0) {
        fx.blur = written(std::min(20.0, blur), 2);
    }
    if (const double sharpen = fx_num(effects, "sharpen"); sharpen > 0) {
        fx.sharpen = written(std::min(5.0, sharpen), 2);
    }
    fx.grayscale = fx_on(effects, "grayscale");
    fx.sepia = fx_on(effects, "sepia");
    fx.vhs = fx_on(effects, "vhs");
    if (const double grain = fx_num(effects, "grain"); grain > 0) {
        fx.grain = static_cast<int>(written(std::min(60.0, grain), 0));
    }
    if (const double px = fx_num(effects, "pixelate"); px >= 2) {
        fx.pixelate = static_cast<int>(written(std::min(32.0, std::max(2.0, px)), 0));
    }
    // `if b:`: un 0 no escribe nada. Lo que no es un número (NaN) tampoco: FFmpeg no lo leería.
    // FFmpeg acota cada valor (set_brightness, set_contrast y set_saturation de vf_eq.c).
    const double b = fx_num(effects, "brightness");
    const double c = fx_num(effects, "contrast");
    const double s = fx_num(effects, "saturation");
    EqParams eq;
    bool any_eq = false;
    if (b != 0 && !std::isnan(b)) {
        eq.brightness = std::clamp(written(b, 3), -1.0, 1.0);
        any_eq = true;
    }
    if (c != 0 && !std::isnan(c)) {
        eq.contrast = std::clamp(written(1 + c, 3), -1000.0, 1000.0);
        any_eq = true;
    }
    if (s != 0 && !std::isnan(s)) {
        eq.saturation = std::clamp(written(std::max(0.0, 1 + s), 3), 0.0, 3.0);
        any_eq = true;
    }
    if (any_eq) {
        fx.eq = eq;
    }
    // colorchannelmixer(e) de clip_adjust: `_effects` entra en un «effects» anidado.
    const Json& nested = field(effects, "effects");
    const auto m = color_matrix(effects.is_object() && effects.contains("effects")
                                    ? (nested.is_object() ? nested : Json::object())
                                    : effects);
    if (m) {
        ColorMatrix w = *m;
        for (auto& row : w) {
            for (double& v : row) {
                v = written(v, 5);
            }
        }
        fx.adjust = w;
    }
    return fx;
}

ClipEffects clip_effects(const TimelineClip& clip) {
    static const Json kEmpty = Json::object();
    return clip_effects(clip.effects && clip.effects->is_object() ? *clip.effects : kEmpty);
}

} // namespace gc::core
