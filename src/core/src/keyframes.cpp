#include "gc/core/keyframes.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <limits>
#include <set>
#include <variant>
#include <vector>

#include "gc/core/masks.hpp"
#include "json_reader.hpp"
#include "py.hpp"

namespace gc::core {

using detail::py_iter;
using detail::py_num;
using detail::py_round;
using detail::py_truthy;

namespace {

constexpr std::array<std::string_view, 10> kInterpNames{"linear",   "ease-in",      "ease-out", "ease-in-out", "cubic-in",
                                                        "cubic-out", "cubic-in-out", "back-out", "bezier",      "hold"};

constexpr std::array<std::string_view, 5> kColorKeys{"color", "border_color", "bg", "shadow_color", "glow_color"};

bool is_color_key(std::string_view key) { return std::ranges::find(kColorKeys, key) != kColorKeys.end(); }

const Json& field(const Json& obj, std::string_view key) {
    static const Json kNull;
    if (!obj.is_object()) {
        return kNull;
    }
    const auto it = obj.find(key);
    return it == obj.end() ? kNull : *it;
}

// `a or b` de Python sobre JSON.
const Json& py_or(const Json& a, const Json& b) { return py_truthy(a) ? a : b; }

constexpr double kNan = std::numeric_limits<double>::quiet_NaN();

// --- Colores ---------------------------------------------------------------------------

// int(s, 16) de Python sobre un trozo corto: espacios alrededor, signo opcional, dígitos hex.
std::optional<int> py_hex_int(const std::string& s) {
    std::string_view v = s;
    while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) {
        v.remove_prefix(1);
    }
    while (!v.empty() && (v.back() == ' ' || v.back() == '\t')) {
        v.remove_suffix(1);
    }
    int sign = 1;
    if (!v.empty() && (v.front() == '+' || v.front() == '-')) {
        sign = v.front() == '-' ? -1 : 1;
        v.remove_prefix(1);
    }
    if (v.empty()) {
        return std::nullopt;
    }
    int n = 0;
    for (const char c : v) {
        int d = 0;
        if (c >= '0' && c <= '9') {
            d = c - '0';
        } else if (c >= 'a' && c <= 'f') {
            d = c - 'a' + 10;
        } else if (c >= 'A' && c <= 'F') {
            d = c - 'A' + 10;
        } else {
            return std::nullopt;
        }
        n = n * 16 + d;
    }
    return sign * n;
}

using Rgb = std::array<double, 3>;

// hex_rgb: "#abc", "aabbcc"… → (r, g, b); nullopt si no es un color.
std::optional<Rgb> hex_rgb(const Json& v) {
    const std::string text = v.is_null() ? std::string() : detail::py_strip(detail::py_str(v));
    std::size_t skip = 0;
    while (skip < text.size() && text[skip] == '#') {
        ++skip;
    }
    auto chars = py_iter(Json(text.substr(skip))).value_or(std::vector<Json>{});
    if (chars.size() == 3) {
        std::vector<Json> doubled;
        for (const auto& c : chars) {
            doubled.push_back(c);
            doubled.push_back(c);
        }
        chars = std::move(doubled);
    }
    if (chars.size() != 6) {
        return std::nullopt;
    }
    Rgb out{};
    for (std::size_t i = 0; i < 3; ++i) {
        const auto n = py_hex_int(chars[2 * i].get<std::string>() + chars[2 * i + 1].get<std::string>());
        if (!n) {
            return std::nullopt;
        }
        out[i] = *n;
    }
    return out;
}

std::string rgb_hex(const Rgb& rgb) {
    std::string out = "#";
    for (const double c : rgb) {
        const auto v = static_cast<int>(py_round(std::min(255.0, std::max(0.0, c)), 0));
        out += std::format("{:02x}", v);
    }
    return out;
}

bool valid_kf_value(std::string_view key, const Json& v) {
    if (is_color_key(key)) {
        return hex_rgb(v).has_value();
    }
    if (v.is_null() || (v.is_string() && v.get_ref<const std::string&>().empty())) {
        return false;
    }
    const auto n = detail::py_float(v);
    return n && std::isfinite(*n);
}

// Valor de keyframe: número o color "#rrggbb".
using KfValue = std::variant<double, std::string>;

KfValue kf_value(std::string_view key, const Json& v) {
    if (is_color_key(key)) {
        return rgb_hex(*hex_rgb(v));
    }
    return *detail::py_float(v);
}

KfValue mix_kf_value(std::string_view key, const KfValue& a, const KfValue& b, double u) {
    if (is_color_key(key)) {
        const Rgb ca = *hex_rgb(Json(std::get<std::string>(a)));
        const Rgb cb = *hex_rgb(Json(std::get<std::string>(b)));
        Rgb mixed{};
        for (std::size_t i = 0; i < 3; ++i) {
            mixed[i] = ca[i] + (cb[i] - ca[i]) * u;
        }
        return rgb_hex(mixed);
    }
    const double x = std::get<double>(a);
    const double y = std::get<double>(b);
    return x + (y - x) * u;
}

// --- Items --------------------------------------------------------------------------------

const Json& items_of(const TimelineClip& clip) {
    static const Json kEmpty = Json::array();
    if (!clip.keyframes || !clip.keyframes->is_object()) {
        return kEmpty;
    }
    return py_or(field(*clip.keyframes, "items"), kEmpty);
}

// Items con t finito, ordenados por t (orden estable).
std::vector<const Json*> sorted_items(const std::vector<Json>& items) {
    std::vector<const Json*> out;
    for (const Json& k : items) {
        if (k.is_object() && std::isfinite(py_num(field(k, "t"), kNan))) {
            out.push_back(&k);
        }
    }
    std::ranges::stable_sort(out, {}, [](const Json* k) { return py_num(field(*k, "t"), 0.0); });
    return out;
}

double item_t(const Json& k) { return py_num(field(k, "t"), 0.0); }

// Valor de `key` en `time` entre los items que la tienen; nullopt si ninguno.
std::optional<KfValue> interp_key(const std::vector<const Json*>& s, std::string_view key, double time) {
    std::vector<const Json*> ks;
    for (const Json* k : s) {
        const Json& props = field(*k, "props");
        if (props.is_object() && valid_kf_value(key, field(props, key))) {
            ks.push_back(k);
        }
    }
    if (ks.empty()) {
        return std::nullopt;
    }
    auto val = [&](const Json* k) { return kf_value(key, field(field(*k, "props"), key)); };
    if (time <= item_t(*ks.front())) {
        return val(ks.front());
    }
    if (time >= item_t(*ks.back())) {
        return val(ks.back());
    }
    for (std::size_t i = 0; i + 1 < ks.size(); ++i) {
        const Json& a = *ks[i];
        const Json& b = *ks[i + 1];
        const double ta = item_t(a);
        const double tb = item_t(b);
        if (ta <= time && time <= tb) {
            const Json& kind = py_or(field(b, "interpolation"), field(b, "ease"));
            const Interp interp = normalize_interp(kind);
            if (interp == Interp::Hold) {
                return val(&a);
            }
            const double span = tb - ta;
            const double u = ease_t((time - ta) / (span != 0.0 ? span : 1.0), interp, field(b, "bezier"));
            return mix_kf_value(key, val(&a), val(&b), u);
        }
    }
    return val(ks.back());
}

std::vector<Json> iter_items(const Json& items) { return py_iter(items).value_or(std::vector<Json>{}); }

} // namespace

KfProps interp_items(const std::vector<Json>& items, double t, const KfProps& fallback) {
    // `_merge(static_props({}), fallback)`: un NaN del respaldo vale lo de un clip vacío.
    static const KfProps kEmpty = static_props(TimelineClip{});
    KfProps out = fallback;
    for (std::size_t i = 0; i < out.values.size(); ++i) {
        if (std::isnan(out.values[i])) {
            out.values[i] = kEmpty.values[i];
        }
    }
    const auto s = sorted_items(items);
    if (s.empty()) {
        return out;
    }
    const double time = std::isnan(t) ? 0.0 : t;
    for (std::size_t i = 0; i < kKfPropKeys.size(); ++i) {
        if (const auto v = interp_key(s, kKfPropKeys[i], time)) {
            out.values[i] = std::get<double>(*v);
        }
    }
    return out;
}

// --- Curvas -----------------------------------------------------------------------------

std::string_view interp_name(Interp interp) { return kInterpNames[static_cast<std::size_t>(interp)]; }

Interp normalize_interp(const Json& value) {
    if (value.is_string()) {
        const auto& s = value.get_ref<const std::string&>();
        for (std::size_t i = 0; i < kInterpNames.size(); ++i) {
            if (kInterpNames[i] == s) {
                return static_cast<Interp>(i);
            }
        }
        if (s == "direct" || s == "step") {
            return Interp::Hold;
        }
    }
    return Interp::Linear;
}

std::optional<Bezier> normalize_bezier(const Json& value) {
    if (!value.is_array() || value.size() != 4) {
        return std::nullopt;
    }
    Bezier n{};
    for (std::size_t i = 0; i < 4; ++i) {
        n[i] = py_num(value[i], kNan);
        if (std::isnan(n[i])) {
            return std::nullopt;
        }
    }
    return Bezier{std::min(1.0, std::max(0.0, n[0])), std::min(2.0, std::max(-1.0, n[1])),
                  std::min(1.0, std::max(0.0, n[2])), std::min(2.0, std::max(-1.0, n[3]))};
}

double bezier_y(const std::optional<Bezier>& bezier, double u) {
    const auto [x1, y1, x2, y2] = bezier.value_or(kBezierDefault);
    if (u <= 0) {
        return 0.0;
    }
    if (u >= 1) {
        return 1.0;
    }
    const double cx = 3 * x1;
    const double bx = 3 * (x2 - x1) - cx;
    const double ax = 1 - cx - bx;
    const double cy = 3 * y1;
    const double by = 3 * (y2 - y1) - cy;
    const double ay = 1 - cy - by;
    auto x_at = [&](double s) { return ((ax * s + bx) * s + cx) * s; };

    // Newton y, si no converge, bisección (igual que bezierY de clipKeyframes.js).
    double s = u;
    bool ok = false;
    for (int i = 0; i < 8; ++i) {
        const double err = x_at(s) - u;
        if (std::abs(err) < 1e-7) {
            ok = true;
            break;
        }
        const double d = (3 * ax * s + 2 * bx) * s + cx;
        if (std::abs(d) < 1e-6) {
            break;
        }
        s -= err / d;
    }
    if (!ok && !(0 <= s && s <= 1 && std::abs(x_at(s) - u) < 1e-5)) {
        double lo = 0.0;
        double hi = 1.0;
        s = u;
        for (int i = 0; i < 40; ++i) {
            const double x = x_at(s);
            if (std::abs(x - u) < 1e-7) {
                break;
            }
            if (x < u) {
                lo = s;
            } else {
                hi = s;
            }
            s = (lo + hi) / 2;
        }
    }
    return ((ay * s + by) * s + cy) * s;
}

double ease_t(double u, Interp kind, const Json& bezier) {
    const double t = std::min(1.0, std::max(0.0, std::isnan(u) ? 0.0 : u));
    switch (kind) {
    case Interp::Hold: return 0.0;
    case Interp::EaseIn: return t * t;
    case Interp::EaseOut: return 1.0 - (1.0 - t) * (1.0 - t);
    case Interp::EaseInOut: return t < 0.5 ? 2 * t * t : 1.0 - 2 * (1.0 - t) * (1.0 - t);
    case Interp::CubicIn: return t * t * t;
    case Interp::CubicOut: return 1.0 - std::pow(1.0 - t, 3.0);
    case Interp::CubicInOut: return t < 0.5 ? 4 * t * t * t : 1.0 - std::pow(-2 * t + 2, 3.0) / 2;
    case Interp::BackOut: return bezier_y(kBackOut, t);
    case Interp::Bezier: return bezier_y(normalize_bezier(bezier), t);
    case Interp::Linear: return t;
    }
    return t;
}

// --- Propiedades --------------------------------------------------------------------------

std::optional<std::size_t> kf_prop_index(std::string_view key) {
    for (std::size_t i = 0; i < kKfPropKeys.size(); ++i) {
        if (kKfPropKeys[i] == key) {
            return i;
        }
    }
    return std::nullopt;
}

double KfProps::get(std::string_view key) const { return values[*kf_prop_index(key)]; }

void KfProps::set(std::string_view key, double value) { values[*kf_prop_index(key)] = value; }

Json KfProps::to_json() const {
    Json out = Json::object();
    for (std::size_t i = 0; i < kKfPropKeys.size(); ++i) {
        out[std::string(kKfPropKeys[i])] = values[i];
    }
    return out;
}

bool keyframes_enabled(const TimelineClip& clip) {
    return clip.keyframes && clip.keyframes->is_object() && !clip.keyframes->empty() &&
           py_truthy(field(*clip.keyframes, "enabled"));
}

double clamp_volume(const Json& value) { return std::min(kVolumeMax, std::max(kVolumeMin, py_num(value, 1.0))); }

KfProps static_props(const TimelineClip& clip) {
    KfProps p;
    auto set = [&](std::string_view key, double v) { p.set(key, v); };
    const Json empty = Json::object();
    auto dict = [&](const std::optional<Json>& v) -> const Json& {
        return v && v->is_object() ? *v : empty;
    };

    if (clip.kind == "shape" || clip.kind == "text") {
        const Json& st = clip.kind == "shape" ? dict(clip.shape) : dict(clip.style);
        set("x", py_num(field(st, "x"), 0.5));
        set("y", py_num(field(st, "y"), 0.5));
        set("scale", py_num(field(st, "scale"), 1.0));
        set("rotation", py_num(field(st, "rotation"), 0.0));
        set("opacity", py_num(field(st, "opacity"), 1.0));
        set("cx", 0.5);
        set("cy", 0.5);
        set("zoom", 1.0);
        if (clip.kind == "shape") {
            set("rot_x", 0.0);
            set("rot_y", 0.0);
            set("draw", std::max(0.0, std::min(1.0, py_num(field(st, "draw"), 1.0))));
        } else {
            set("rot_x", py_num(field(st, "rot_x"), 0.0));
            set("rot_y", py_num(field(st, "rot_y"), 0.0));
            set("draw", 1.0);
        }
    } else {
        const Json& tr = dict(clip.transform);
        set("x", py_num(field(tr, "x"), 0.5));
        set("y", py_num(field(tr, "y"), 0.5));
        set("scale", py_num(field(tr, "scale"), 1.0));
        set("rotation", py_num(field(tr, "rotation"), 0.0));
        set("opacity", clip.opacity ? (std::isnan(*clip.opacity) ? 1.0 : *clip.opacity) : 1.0);
        set("cx", 0.5);
        set("cy", 0.5);
        set("zoom", clip.reframe ? (std::isnan(clip.reframe->zoom) ? 1.0 : clip.reframe->zoom) : 1.0);
        set("rot_x", 0.0);
        set("rot_y", 0.0);
        set("draw", 1.0);
    }

    // Máscara: los campos de masks[0] (o los valores por defecto).
    const auto masks = clip_masks(clip);
    if (masks.empty()) {
        for (const auto& [key, v] : std::initializer_list<std::pair<std::string_view, double>>{
                 {"mx", 0.5}, {"my", 0.5}, {"mw", 0.5}, {"mh", 0.5},
                 {"msx", 1.0}, {"msy", 1.0}, {"mrot", 0.0}, {"mfeather", 0.0}}) {
            set(key, v);
        }
    } else {
        const Json& m = masks.front();
        set("mx", m["x"].get<double>());
        set("my", m["y"].get<double>());
        set("mw", m["w"].get<double>());
        set("mh", m["h"].get<double>());
        set("msx", m["scale_x"].get<double>());
        set("msy", m["scale_y"].get<double>());
        set("mrot", m["rotation"].get<double>());
        set("mfeather", m["feather"].get<double>());
    }

    // Volumen y efectos de audio (true = 1, false/ausente = 0).
    set("volume", std::min(kVolumeMax, std::max(kVolumeMin, std::isnan(clip.volume) ? 1.0 : clip.volume)));
    const Json& fx = dict(clip.audio_fx);
    for (std::size_t i = kKfPropKeys.size() - 11; i < kKfPropKeys.size(); ++i) {
        const Json& v = field(fx, kKfPropKeys[i]);
        if (v.is_boolean()) {
            p.values[i] = v.get<bool>() ? 1.0 : 0.0;
        } else if (v.is_null()) {
            p.values[i] = 0.0;
        } else {
            p.values[i] = std::max(0.0, std::min(1.0, py_num(v, 0.0)));
        }
    }
    return p;
}

std::optional<KfProps> clip_props_over(const TimelineClip& clip, double local_t, const KfProps& base) {
    if (!keyframes_enabled(clip)) {
        return std::nullopt;
    }
    const Json& items = items_of(clip);
    if (!py_truthy(items)) {
        return std::nullopt;
    }
    return interp_items(iter_items(items), local_t, base);
}

KfProps clip_props_at(const TimelineClip& clip, double local_t) {
    const KfProps base = static_props(clip);
    return clip_props_over(clip, local_t, base).value_or(base);
}

double clip_volume_at(const TimelineClip& clip, double local_t) {
    const double v = clip_props_at(clip, local_t).get("volume");
    return std::min(kVolumeMax, std::max(kVolumeMin, std::isnan(v) ? 1.0 : v));
}

Json text_style_at(const TimelineClip& clip, double local_t) {
    Json out = Json::object();
    if (!keyframes_enabled(clip)) {
        return out;
    }
    const auto items = iter_items(items_of(clip));
    const auto s = sorted_items(items);
    const double time = std::isnan(local_t) ? 0.0 : local_t;
    for (const auto key : kTextStyleKfKeys) {
        if (const auto v = interp_key(s, key, time)) {
            out[std::string(key)] = std::visit([](const auto& x) { return Json(x); }, *v);
        }
    }
    return out;
}

bool text_style_animates(const TimelineClip& clip) {
    if (!keyframes_enabled(clip)) {
        return false;
    }
    const auto items = iter_items(items_of(clip));
    const auto s = sorted_items(items);
    for (const auto key : kTextStyleKfKeys) {
        std::set<std::string> seen;
        for (const Json* k : s) {
            const Json& props = field(*k, "props");
            if (props.is_object() && valid_kf_value(key, field(props, key))) {
                const KfValue v = kf_value(key, field(props, key));
                seen.insert(std::holds_alternative<double>(v) ? Json(std::get<double>(v)).dump() : std::get<std::string>(v));
            }
        }
        if (seen.size() > 1) {
            return true;
        }
    }
    return false;
}

Json with_text_style_kf(const Json& style, const Json& animated) {
    const Json st = style.is_object() ? style : Json::object();
    Json out = st;
    if (animated.is_object()) {
        for (const auto& [key, value] : animated.items()) {
            out[key] = value;
        }
    }
    const Json& bg = field(st, "bg");
    if (!py_truthy(bg) || bg == "none") {
        out["bg"] = st.contains("bg") ? bg : Json("none");
    }
    if (!(py_num(field(st, "border_width"), 0.0) > 0)) {
        out["border_width"] = st.contains("border_width") ? st["border_width"] : Json(0);
    }
    if (!py_truthy(field(st, "scale_split"))) {
        out.erase("stretch_x");
        out.erase("stretch_y");
    }
    return out;
}

// --- Edición ------------------------------------------------------------------------------

void upsert_keyframe_at(TimelineClip& clip, double local_t, const Json& patch, std::optional<Interp> interpolation,
                        IdSource& ids) {
    const double t = py_round(std::isnan(local_t) ? 0.0 : local_t, 6);
    const Json prev = clip.keyframes && clip.keyframes->is_object() ? *clip.keyframes : Json::object();
    std::vector<Json> items;
    if (py_truthy(field(prev, "items"))) {
        for (const Json& k : iter_items(field(prev, "items"))) {
            if (k.is_object()) {
                items.push_back(k);
            }
        }
    }

    // Valores actuales en t (con estos items activos) y el parche encima.
    TimelineClip enabled = clip;
    enabled.keyframes = Json{{"enabled", true}, {"items", Json(items)}};
    KfProps props = clip_props_at(enabled, t);
    if (patch.is_object()) {
        for (std::size_t i = 0; i < kKfPropKeys.size(); ++i) {
            const Json& v = field(patch, kKfPropKeys[i]);
            if (!v.is_null()) {
                props.values[i] = py_num(v, props.values[i]);
            }
        }
    }

    const auto idx = std::ranges::find_if(items, [&](const Json& k) { return std::abs(item_t(k) - t) < kKfSnap; });
    if (idx != items.end()) {
        const Interp keep = interpolation.value_or(normalize_interp(field(*idx, "interpolation")));
        (*idx)["t"] = t;
        (*idx)["props"] = props.to_json();
        (*idx)["interpolation"] = interp_name(keep);
    } else {
        const Interp last = items.empty() ? Interp::Linear : normalize_interp(field(items.back(), "interpolation"));
        Json item = Json::object();
        item["id"] = make_uid(ids, "k");
        item["t"] = t;
        item["interpolation"] = interp_name(interpolation.value_or(last));
        item["props"] = props.to_json();
        // Un keyframe nuevo hereda también la curva personalizada del anterior.
        if (!items.empty() && !interpolation) {
            if (const auto bez = normalize_bezier(field(items.back(), "bezier"))) {
                item["bezier"] = Json::array({(*bez)[0], (*bez)[1], (*bez)[2], (*bez)[3]});
            }
        }
        items.push_back(std::move(item));
    }
    std::ranges::stable_sort(items, {}, item_t);
    clip.keyframes = Json{{"enabled", true}, {"items", Json(items)}};
}

Result<void> apply_volume_fade(TimelineClip& clip, double duration, std::string_view side, double fade_dur,
                               IdSource& ids) {
    if (side != "in" && side != "out") {
        return fail(ErrorCode::InvalidArgument, "side debe ser 'in' o 'out'");
    }
    const double dur = std::max(0.05, std::isnan(duration) ? 0.0 : duration);
    const double window = std::min(std::max(0.05, std::isnan(fade_dur) ? 0.5 : fade_dur), dur / 2.0);
    const double peak = std::min(kVolumeMax, std::max(kVolumeMin, std::isnan(clip.volume) ? 1.0 : clip.volume));
    const double target = peak > 0.001 ? peak : 1.0;
    clip.volume = target;
    if (side == "in") {
        upsert_keyframe_at(clip, 0.0, Json{{"volume", 0.0}}, Interp::EaseOut, ids);
        upsert_keyframe_at(clip, window, Json{{"volume", target}}, Interp::EaseOut, ids);
    } else {
        upsert_keyframe_at(clip, std::max(0.0, dur - window), Json{{"volume", target}}, Interp::EaseIn, ids);
        upsert_keyframe_at(clip, dur, Json{{"volume", 0.0}}, Interp::EaseIn, ids);
    }
    return {};
}

} // namespace gc::core
