#include "gc/core/text_style.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>

#include "gc/core/clip_layout.hpp"
#include "gc/core/fx_anims.hpp"
#include "gc/core/keyframes.hpp"
#include "gc/core/pose.hpp"
#include "gc/core/text_metrics.hpp"
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

// --- Geometría ------------------------------------------------------------------------------

namespace {

// min(a, b) y max(a, b) de Python: el primero salvo que el segundo sea menor (mayor).
double py_min(double a, double b) {
    return b < a ? b : a;
}
double py_max(double a, double b) {
    return b > a ? b : a;
}

// st.get("font") or "Arial" y bool(st.get("bold", True)), como text_metrics.
std::string font_of(const Json& style) {
    const Json& raw = field(style, "font");
    return py_truthy(raw) ? detail::py_str(raw) : std::string("Arial");
}
bool bold_of(const Json& style) {
    return !style.is_object() || !style.contains("bold") || py_truthy(style.at("bold"));
}

// Media anchura de la caja del texto en px: max(0.2, min(1.0, w)) · W / 2 · escala.
double half_box(const Json& style, double width) {
    return py_max(0.2, py_min(1.0, py_num(field(style, "w"), 0.88))) * width / 2.0 * text_style_scale(style);
}

std::string join_words(const std::vector<std::string>& row) {
    std::string out;
    for (std::size_t i = 0; i < row.size(); ++i) {
        out += (i == 0 ? "" : " ") + row[i];
    }
    return out;
}

// Desplazamiento de la línea i de n respecto al centro del bloque, en líneas: i − (n − 1) / 2.
double row_offset(std::size_t i, std::size_t n) {
    return static_cast<double>(i) - static_cast<double>(n - 1) / 2.0;
}

std::vector<std::string> code_points(std::string_view text) {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < text.size();) {
        const std::size_t from = i;
        (void)utf8_next(text, i);
        out.emplace_back(text.substr(from, i - from));
    }
    return out;
}

} // namespace

std::vector<BoxRect> box_rects(const Json& style, double width, double height,
                               const std::vector<std::vector<std::string>>& rows, double size_factor) {
    const double em = text_font_px(style, height);
    const std::string font = font_of(style);
    const bool bold = bold_of(style);
    const double ls = text_letter_spacing_px(style, em);
    const double lh = em * text_line_height(style);
    const double box_h = em * size_factor;
    const double half = half_box(style, width);
    const double inset = 0.2 * em;
    const Json& align = field(style, "align");
    std::vector<BoxRect> rects;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].empty()) {
            continue;
        }
        const double w = text_width(font, bold, em, join_words(rows[i]), ls);
        const double x = is_text(align, "left") ? -half + inset + w / 2 : is_text(align, "right") ? half - inset - w / 2 : 0.0;
        rects.push_back({x, row_offset(i, rows.size()) * lh, w, box_h});
    }
    if (rects.empty()) {
        return rects;
    }
    if (is_text(field(style, "bg_style"), "block")) {
        double x0 = rects[0].x - rects[0].w / 2;
        double x1 = rects[0].x + rects[0].w / 2;
        double y0 = rects[0].y - rects[0].h / 2;
        double y1 = rects[0].y + rects[0].h / 2;
        for (const auto& r : rects) {
            x0 = py_min(x0, r.x - r.w / 2);
            x1 = py_max(x1, r.x + r.w / 2);
            y0 = py_min(y0, r.y - r.h / 2);
            y1 = py_max(y1, r.y + r.h / 2);
        }
        rects = {{(x0 + x1) / 2, (y0 + y1) / 2, x1 - x0, y1 - y0}};
    }
    const double pad0 = py_max(0.0, py_num(field(style, "border_width"), 0.0));
    const double pad_x = pad0 + py_min(1.0, py_max(0.0, py_num(field(style, "bg_pad_x"), 0.0))) * em;
    const double pad_y = pad0 + py_min(1.0, py_max(0.0, py_num(field(style, "bg_pad_y"), 0.0))) * em;
    const double dx = py_min(1.0, py_max(-1.0, py_num(field(style, "bg_dx"), 0.0))) * em;
    const double dy = -py_min(1.0, py_max(-1.0, py_num(field(style, "bg_dy"), 0.0))) * em;
    for (auto& r : rects) {
        r = {r.x + dx, r.y + dy, r.w + 2 * pad_x, r.h + 2 * pad_y};
    }
    return rects;
}

std::vector<CurveGlyph> curve_glyphs(const Json& style, double width, double height,
                                     const std::vector<std::vector<std::string>>& rows) {
    const double k = curve_of(style);
    const double em = text_font_px(style, height);
    const std::string font = font_of(style);
    const bool bold = bold_of(style);
    const double ls = text_letter_spacing_px(style, em);
    const double lh = em * text_line_height(style);
    const double half = half_box(style, width);
    const double inset = 0.2 * em;
    const Json& align = field(style, "align");
    const double space = text_width(font, bold, em, " ", ls);
    std::vector<std::vector<double>> widths;
    std::vector<double> totals;
    for (const auto& row : rows) {
        auto& ws = widths.emplace_back();
        double sum = 0.0;
        for (const auto& w : row) {
            ws.push_back(text_width(font, bold, em, w, ls));
            sum += ws.back();
        }
        totals.push_back(sum + space * static_cast<double>(row.empty() ? 0 : row.size() - 1));
    }
    double widest = totals.empty() ? 0.0 : totals[0];
    for (const double t : totals) {
        widest = py_max(widest, t);
    }
    const double radius = py_max(1.0, widest) / (py_max(std::abs(k), 0.005) * std::numbers::pi);
    const double sign = k >= 0 ? 1.0 : -1.0;
    std::vector<CurveGlyph> out;
    std::int64_t gi = 0;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const double y0 = row_offset(i, rows.size()) * lh;
        const double tot = totals[i];
        double x = is_text(align, "left") ? -half + inset : is_text(align, "right") ? half - inset - tot : -tot / 2;
        const double mid = x + tot / 2;
        const double r = py_max(1.0, radius - sign * y0);
        for (std::size_t j = 0; j < rows[i].size(); ++j) {
            double cur = x;
            for (auto& ch : code_points(rows[i][j])) {
                const double a = text_width(font, bold, em, ch, ls);
                const double phi = (cur + a / 2 - mid) / r;
                out.push_back({gi, std::move(ch), mid + r * std::sin(phi), sign * radius - sign * r * std::cos(phi),
                               sign * phi});
                cur += a;
            }
            x += widths[i][j] + space;
            ++gi;
        }
    }
    return out;
}

// --- Un clip de texto en el tiempo ----------------------------------------------------------

double text_clip_duration(const TimelineClip& clip) {
    return py_max(0.0, clip.out_point - clip.in_point);
}

TextPose text_pose_at(const TimelineClip& clip, double local_t) {
    const Pose p = render_pose(clip, local_t);
    TextPose out{p.x, p.y, p.scale, p.rotation, p.opacity};
    if (keyframes_enabled(clip)) {
        const KfProps props = clip_props_at(clip, local_t);
        out.rot_x = props.get("rot_x");
        out.rot_y = props.get("rot_y");
    } else {
        const Json& st = clip.style ? *clip.style : Json();
        out.rot_x = py_num(field(st, "rot_x"), 0.0);
        out.rot_y = py_num(field(st, "rot_y"), 0.0);
    }
    return out;
}

Json posed_text_style(const TimelineClip& clip, const Json& style, double local_t) {
    Json out = with_text_style_kf(style.is_object() ? style : Json::object(), text_style_at(clip, local_t));
    const TextPose pose = text_pose_at(clip, local_t);
    out["x"] = pose.x;
    out["y"] = pose.y;
    out["scale"] = pose.scale;
    out["rotation"] = pose.rotation;
    out["rot_x"] = pose.rot_x;
    out["rot_y"] = pose.rot_y;
    const Flip flip = clip_flip(clip);
    out["flip_h"] = flip.h;
    out["flip_v"] = flip.v;
    if (keyframes_enabled(clip)) {
        out["opacity"] = pose.opacity;
    } else if (has_animations(clip)) {
        // sin keyframes manda la opacidad del estilo; las animaciones la multiplican
        out["opacity"] = text_clamp01(field(out, "opacity"), 1.0) * anim_opacity(clip, local_t);
    }
    return out;
}

bool text_animates(const TimelineClip& clip, double fps) {
    if (has_animations(clip)) {
        return true;
    }
    bool anim_any = false;
    if (clip.anim && clip.anim->is_object()) {
        for (const auto& [k, v] : clip.anim->items()) {
            anim_any = anim_any || py_truthy(v);
        }
    }
    if (!keyframes_enabled(clip) && !anim_any) {
        return false;
    }
    const double dur = text_clip_duration(clip);
    if (dur <= 0) {
        return false;
    }
    if (text_style_animates(clip)) {
        return true;
    }
    // max(2, min(2000, ceil(dur · max(1, fps or 30)) + 1)); un producto infinito hace fallar a
    // video-yt (OverflowError) y aquí vale 2000
    const double f = py_max(1.0, fps == 0 ? 30.0 : fps);
    const double steps = std::ceil(dur * f);
    const auto n = static_cast<std::int64_t>(std::isfinite(steps) ? std::max(2.0, std::min(2000.0, steps + 1)) : 2000.0);
    const TextPose first = text_pose_at(clip, 0.0);
    const auto moved = [&first](const TextPose& p) {
        return std::abs(p.x - first.x) > 5e-4 || std::abs(p.y - first.y) > 5e-4 ||
               std::abs(p.scale - first.scale) > 1e-3 || std::abs(p.rotation - first.rotation) > 0.05 ||
               std::abs(p.opacity - first.opacity) > 2e-3 || std::abs(p.rot_x - first.rot_x) > 0.05 ||
               std::abs(p.rot_y - first.rot_y) > 0.05;
    };
    for (std::int64_t i = 1; i < n; ++i) {
        if (moved(text_pose_at(clip, dur * static_cast<double>(i) / static_cast<double>(n - 1)))) {
            return true;
        }
    }
    return false;
}

std::vector<std::pair<double, double>> word_windows(const TimelineClip& clip) {
    const std::size_t n = split_words(clip.text.value_or("")).size();
    std::vector<std::pair<double, double>> wins;
    if (n == 0) {
        return wins;
    }
    const double start = py_max(0.0, clip.start);
    const double dur = text_clip_duration(clip);
    if (clip.words.size() == n) {
        std::vector<double> rels;
        for (const Word& w : clip.words) {
            rels.push_back(py_min(py_max(0.0, w.start), dur));
        }
        for (std::size_t i = 0; i < n; ++i) {
            const double t0 = start + rels[i];
            double t1 = start + (i + 1 < n ? rels[i + 1] : dur);
            if (t1 <= t0) {
                t1 = t0 + 0.04;
            }
            wins.emplace_back(t0, t1);
        }
        return wins;
    }
    const double slot = dur / static_cast<double>(n);
    for (std::size_t i = 0; i < n; ++i) {
        wins.emplace_back(start + static_cast<double>(i) * slot, start + static_cast<double>(i + 1) * slot);
    }
    return wins;
}

std::int64_t active_word_at(const TimelineClip& clip, std::int64_t word_count, double local_t, double duration) {
    if (word_count != 0 && clip.words.size() == static_cast<std::size_t>(word_count)) {
        std::int64_t idx = 0;
        for (std::size_t i = 0; i < clip.words.size(); ++i) {
            if (!(clip.words[i].start <= local_t)) {
                break;
            }
            idx = static_cast<std::int64_t>(i);
        }
        return idx;
    }
    return active_word_index(word_count, local_t, duration);
}

std::string typing_text(std::string_view text, double progress) {
    const auto chars = code_points(text);
    const auto k = static_cast<std::size_t>(std::ceil(text_clamp01(Json(progress), 0.0) * static_cast<double>(chars.size())));
    std::string out;
    for (std::size_t i = 0; i < k && i < chars.size(); ++i) {
        out += chars[i];
    }
    return out;
}

std::optional<TextContent> text_content_at(const TimelineClip& clip, const Json& posed_style, double local_t,
                                           double duration) {
    const std::string text = clip.text.value_or("");
    const Json& appear = field(posed_style, "block_appear");
    if (is_text(appear, "typing")) {
        std::string shown = typing_text(text, duration > 0 ? local_t / duration : 1.0);
        if (py_strip(shown).empty()) {
            return std::nullopt;
        }
        return TextContent{std::move(shown)};
    }
    const auto words = split_words(text);
    if (!word_fx_set(posed_style).any()) {
        return TextContent{text};
    }
    if (words.size() == 1) {
        return TextContent{text, -1, true};
    }
    return TextContent{text, active_word_at(clip, static_cast<std::int64_t>(words.size()), local_t, duration)};
}

} // namespace gc::core
