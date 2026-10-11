#include "gc/core/text_style.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>

#include "json_reader.hpp"
#include "py.hpp"

namespace gc::core {

using detail::py_float;
using detail::py_is_space;
using detail::py_lower;
using detail::py_num;
using detail::py_split;
using detail::py_strip;
using detail::py_truthy;
using detail::py_upper;
using detail::utf8_next;

namespace {

const Json& field(const Json& obj, std::string_view key) {
    static const Json kNull;
    if (!obj.is_object()) {
        return kNull;
    }
    const auto it = obj.find(key);
    return it == obj.end() ? kNull : *it;
}

// st.get(key, default): el defecto solo si falta la clave (un null presente es None).
Json field_or(const Json& obj, std::string_view key, Json fallback) {
    if (obj.is_object()) {
        if (const auto it = obj.find(key); it != obj.end()) {
            return *it;
        }
    }
    return fallback;
}

bool is_text(const Json& v, std::string_view text) {
    return v.is_string() && v.get_ref<const std::string&>() == text;
}

// min(hi, max(lo, v)) de Python, en ese orden (un NaN acaba en hi).
double py_clamp(double v, double lo, double hi) {
    const double m = v > lo ? v : lo;
    return m < hi ? m : hi;
}

// math.radians de CPython: x · (π / 180).
double radians(double deg) {
    return deg * (std::numbers::pi / 180.0);
}

} // namespace

Json effective_text_style(const Json& track_style, const Json& clip_style) {
    Json out = Json::object();
    for (const Json* src : {&track_style, &clip_style}) {
        if (src->is_object()) {
            for (const auto& [k, v] : src->items()) {
                out[k] = v;
            }
        }
    }
    return out;
}

WordFx word_fx_set(const Json& style) {
    WordFx fx;
    const auto add = [&fx](const Json& x) {
        fx.glow = fx.glow || is_text(x, "glow");
        fx.highlight = fx.highlight || is_text(x, "highlight");
        fx.pop = fx.pop || is_text(x, "pop");
    };
    const Json& raw = field(style, "word_fx");
    if (raw.is_array()) {
        for (const Json& x : raw) {
            add(x);
        }
    } else if (py_truthy(raw)) {
        add(raw); // "none" y lo desconocido no cuentan
    }
    return fx;
}

double text_clamp01(const Json& value, double fallback) {
    const auto v = py_float(value);
    if (!v) {
        return fallback;
    }
    // max(0.0, min(1.0, v)) de Python: min(1.0, nan) es 1.0
    const double m = *v < 1.0 ? *v : 1.0;
    return m > 0.0 ? m : 0.0;
}

double style_opacity(const Json& style) {
    return text_clamp01(field_or(style, "opacity", 1), 1.0);
}

double word_opacity(const Json& style, bool active) {
    const double base = style_opacity(style);
    const double word = active || !word_fx_set(style).any()
                            ? text_clamp01(field_or(style, "active_opacity", 1), 1.0)
                            : text_clamp01(field_or(style, "inactive_opacity", 1), 1.0);
    return text_clamp01(Json(base * word), 0.0);
}

std::string alpha_hex(double opacity) {
    const double op = text_clamp01(Json(opacity), 1.0);
    const auto v = static_cast<int>(std::nearbyint((1.0 - op) * 255)); // round() de Python: al par
    char buf[8];
    std::snprintf(buf, sizeof buf, "%02X", v);
    return buf;
}

std::string ass_bgr(std::string_view hex_color, std::string_view alpha) {
    std::string h = py_strip(hex_color.empty() ? std::string_view("#ffffff") : hex_color);
    h.erase(0, h.find_first_not_of('#') == std::string::npos ? h.size() : h.find_first_not_of('#'));
    // Por caracteres, no por bytes (como los índices de Python).
    std::vector<std::string> chars;
    for (std::size_t i = 0; i < h.size();) {
        const std::size_t from = i;
        (void)utf8_next(h, i);
        chars.emplace_back(h.substr(from, i - from));
    }
    if (chars.size() == 3) {
        chars = {chars[0], chars[0], chars[1], chars[1], chars[2], chars[2]};
    }
    if (chars.size() < 6) {
        chars.assign(6, "f");
    }
    return "&H" + std::string(alpha) + py_upper(chars[4] + chars[5]) + py_upper(chars[2] + chars[3]) +
           py_upper(chars[0] + chars[1]) + "&";
}

std::string style_color(const Json& style, std::string_view key, std::string_view fallback) {
    const Json& v = field(style, key);
    return v.is_string() && !v.get_ref<const std::string&>().empty() ? v.get<std::string>() : std::string(fallback);
}

std::pair<double, double> stretch_of(const Json& style) {
    if (!py_truthy(field(style, "scale_split"))) {
        return {1.0, 1.0};
    }
    return {py_clamp(py_num(field(style, "stretch_x"), 1.0), 0.01, 100.0),
            py_clamp(py_num(field(style, "stretch_y"), 1.0), 0.01, 100.0)};
}

std::optional<std::string> box_color(const Json& style) {
    const Json& bg = field(style, "bg");
    if (!py_truthy(bg) || is_text(bg, "none") || !bg.is_string()) {
        return std::nullopt;
    }
    const double op = text_clamp01(field_or(style, "bg_opacity", 0.55), 0.55) * style_opacity(style);
    return ass_bgr(bg.get_ref<const std::string&>(), alpha_hex(op));
}

bool box_drawn(const Json& style) {
    if (!box_color(style)) {
        return false;
    }
    if (py_truthy(field(style, "_bg_draw")) || is_text(field(style, "bg_style"), "block")) {
        return true;
    }
    for (const char* key : {"bg_radius", "bg_pad_x", "bg_pad_y", "bg_dx", "bg_dy"}) {
        if (py_num(field(style, key), 0.0) != 0) {
            return true;
        }
    }
    return false;
}

std::int64_t active_word_index(std::int64_t word_count, double local_t, double duration) {
    if (word_count <= 0) {
        return -1;
    }
    if (duration <= 0) {
        return 0;
    }
    if (local_t < 0) {
        return -1;
    }
    if (local_t >= duration) {
        return word_count - 1;
    }
    const auto i = static_cast<std::int64_t>(local_t / duration * static_cast<double>(word_count));
    return std::min(word_count - 1, i);
}

std::vector<std::string> split_words(std::string_view text) {
    return py_split(text);
}

std::string apply_text_case(std::string_view text, const Json& mode) {
    if (is_text(mode, "upper")) {
        return py_upper(text);
    }
    if (is_text(mode, "lower")) {
        return py_lower(text);
    }
    if (!is_text(mode, "title")) {
        return std::string(text);
    }
    // re.sub(r"\S+", w[:1].upper() + w[1:].lower())
    std::string out;
    std::size_t i = 0;
    while (i < text.size()) {
        std::size_t j = i;
        if (py_is_space(utf8_next(text, j))) {
            out.append(text.substr(i, j - i));
            i = j;
            continue;
        }
        const std::size_t first_end = j;
        std::size_t k = j;
        while (k < text.size()) {
            std::size_t next = k;
            if (py_is_space(utf8_next(text, next))) {
                break;
            }
            k = next;
        }
        out += py_upper(text.substr(i, first_end - i));
        out += py_lower(text.substr(first_end, k - first_end));
        i = k;
    }
    return out;
}

std::string glow_color(const Json& style) {
    const Json& own = field(style, "glow_color");
    if (own.is_string() && !own.get_ref<const std::string&>().empty()) {
        return own.get<std::string>();
    }
    return style_color(style, "shadow_color", "#ffffff");
}

std::optional<TextShadow> text_glow(const Json& style, double font_px) {
    if (!py_truthy(field(style, "glow"))) {
        return std::nullopt;
    }
    const double opacity = text_clamp01(field_or(style, "glow_intensity", 0.9), 0.9);
    if (opacity <= 0) {
        return std::nullopt;
    }
    const double range = py_clamp(py_num(field(style, "glow_range"), 0.5), 0.0, 1.0);
    TextShadow sh;
    sh.color = glow_color(style);
    sh.opacity = opacity;
    sh.dx = py_clamp(py_num(field(style, "glow_dx"), 0.0), -1.0, 1.0) * font_px;
    sh.dy = -py_clamp(py_num(field(style, "glow_dy"), 0.0), -1.0, 1.0) * font_px;
    sh.sigma = range * 0.5 * font_px;
    sh.spread = is_text(field(style, "glow_style"), "strong") ? 0.08 * font_px : 0.0;
    return sh;
}

std::optional<TextShadow> text_shadow(const Json& style, double font_px) {
    if (py_truthy(field(style, "glow"))) {
        return text_glow(style, font_px);
    }
    if (!py_truthy(field(style, "shadow"))) {
        return std::nullopt;
    }
    const double opacity = text_clamp01(field_or(style, "shadow_opacity", 0.6), 0.6);
    if (opacity <= 0) {
        return std::nullopt;
    }
    const double angle = py_num(field(style, "shadow_angle"), 45.0);
    if (!std::isfinite(angle)) {
        return std::nullopt; // math.cos(inf) lanza ValueError en video-yt
    }
    const double dist = py_clamp(py_num(field(style, "shadow_distance"), 0.06), 0.0, 1.0) * font_px;
    const double ang = radians(angle);
    TextShadow sh;
    sh.color = style_color(style, "shadow_color", "#000000");
    sh.opacity = opacity;
    sh.dx = std::cos(ang) * dist;
    sh.dy = std::sin(ang) * dist;
    sh.sigma = py_clamp(py_num(field(style, "shadow_blur"), 0.05), 0.0, 1.0) * font_px;
    return sh;
}

std::pair<std::int64_t, std::int64_t> text_xy(const Json& style, double width, double height) {
    const auto pos = [&](std::string_view key, double size) {
        const double frac = py_clamp(py_num(field(style, key), 0.5), -10.0, 11.0);
        return static_cast<std::int64_t>(std::nearbyint(frac * size));
    };
    return {pos("x", width), pos("y", height)};
}

double curve_of(const Json& style) {
    const Json& bg = field(style, "bg");
    if (!py_truthy(field(style, "curve_on")) || (py_truthy(bg) && !is_text(bg, "none"))) {
        return 0.0;
    }
    const double k = py_clamp(py_num(field(style, "curve"), 0.0), -1.0, 1.0);
    return std::abs(k) < 0.005 ? 0.0 : k;
}

} // namespace gc::core
