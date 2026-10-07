#include "gc/core/clip_motion.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

#include "edit_util.hpp"
#include "gc/core/clip_layout.hpp"
#include "gc/core/keyframes.hpp"
#include "gc/core/timeline_rules.hpp"

namespace gc::core {

using detail::field;
using detail::invalid;
using detail::py_iter;
using detail::py_num;
using detail::py_round;
using detail::py_strip;
using detail::py_truthy;

namespace {

// Índices de kMotionKeys; los 8 primeros son la pose (_POSE).
enum Key : std::size_t { X, Y, SCALE, ROT, OPACITY, CX, CY, ZOOM, DRAW };
using Pose = std::array<double, 8>;
using Props = std::array<std::optional<double>, kMotionKeys.size()>;
using EnvPoint = std::pair<double, double>; // (t local, amp 0–1)

bool in(std::string_view v, std::initializer_list<std::string_view> list) {
    return std::ranges::find(list, v) != list.end();
}

// Movimientos de aparición: con envolvente, el clip se queda tras el golpe.
bool is_appear(std::string_view m) {
    return in(m, {"zoom_in", "spin_in", "slide_left", "slide_right", "slide_up", "slide_down", "fade_in", "pop"});
}

double lerp(double a, double b, double t) { return a + (b - a) * std::max(0.0, std::min(1.0, t)); }

Props all_of(const Pose& p) {
    Props out{};
    for (std::size_t i = 0; i < p.size(); ++i) {
        out[i] = p[i];
    }
    return out;
}

// _item: id nuevo, t ≥ 0 a 3 decimales y las propiedades a 4.
MotionItem make_item(IdSource& ids, double t, const Props& props, std::string interpolation) {
    MotionItem it;
    it.id = make_uid(ids, "k");
    it.t = py_round(std::max(0.0, t), 3);
    it.interpolation = std::move(interpolation);
    for (std::size_t i = 0; i < props.size(); ++i) {
        if (props[i]) {
            it.props[i] = py_round(std::isnan(*props[i]) ? 0.0 : *props[i], 4);
        }
    }
    return it;
}

Pose rest_of(const TimelineClip& clip) {
    const KfProps p = static_props(clip);
    Pose r{};
    for (std::size_t i = 0; i < r.size(); ++i) {
        r[i] = p.get(kMotionKeys[i]);
    }
    return r;
}

double zoom_punch(const Pose& rest, double intensity) {
    return std::max(0.1, std::min(1.0, rest[ZOOM] * (1.0 - 0.28 * intensity)));
}

// La pose del preset con la amplitud `amp` (0 = inicio del movimiento, 1 = reposo).
Pose from_amp(bool scale_zoom, std::string_view m, const Pose& rest, double amp, double intensity, double turns) {
    const double a = std::max(0.0, std::min(1.0, amp));
    Pose p = rest;
    if (m == "zoom_in" || m == "zoom_out") {
        const double target = zoom_punch(rest, intensity);
        const double small = rest[SCALE] * std::max(0.15, 0.4 / intensity);
        if (m == "zoom_in") {
            if (scale_zoom) {
                p[SCALE] = lerp(small, rest[SCALE], a);
            } else {
                p[ZOOM] = lerp(1.0, target, a);
            }
        } else if (scale_zoom) {
            p[SCALE] = lerp(rest[SCALE], small, a);
        } else {
            p[ZOOM] = lerp(target, 1.0, a);
        }
        p[OPACITY] = m == "zoom_in" ? rest[OPACITY] * (0.15 + 0.85 * a) : rest[OPACITY];
    } else if (m == "spin" || m == "spin_in") {
        p[ROT] = rest[ROT] + (1.0 - a) * 360.0 * turns;
        p[SCALE] = lerp(rest[SCALE] * 0.35, rest[SCALE], a);
        p[OPACITY] = rest[OPACITY] * a;
    } else if (m == "slide_left" || m == "slide_right") {
        const double off = (1.0 - a) * 0.9 * intensity;
        p[X] = m == "slide_left" ? rest[X] - off : rest[X] + off;
        p[OPACITY] = rest[OPACITY] * a;
    } else if (m == "slide_up" || m == "slide_down") {
        const double off = (1.0 - a) * 0.9 * intensity;
        p[Y] = m == "slide_up" ? rest[Y] - off : rest[Y] + off;
        p[OPACITY] = rest[OPACITY] * a;
    } else if (m == "fade_in") {
        p[OPACITY] = rest[OPACITY] * a;
    } else if (m == "fade_out") {
        p[OPACITY] = rest[OPACITY] * (1.0 - a);
    } else if (m == "pop") {
        p[SCALE] = lerp(rest[SCALE] * 0.2, rest[SCALE], a);
        p[OPACITY] = rest[OPACITY] * a;
    } else if (m == "pulse") {
        p[SCALE] = rest[SCALE] * (0.72 + 0.28 * a);
        p[OPACITY] = rest[OPACITY] * (0.2 + 0.8 * a);
    }
    return p;
}

Result<std::vector<EnvPoint>> parse_envelope(const Json& raw) {
    std::vector<EnvPoint> out;
    if (!py_truthy(raw)) {
        return out;
    }
    const auto list = py_iter(raw);
    if (!list) {
        return invalid("envelope debe ser una lista");
    }
    for (const Json& p : *list) {
        Json t;
        Json a;
        if (p.is_object()) {
            t = field(p, "t");
            a = p.contains("amp") ? p.at("amp") : field(p, "v");
        } else if (p.is_array() && p.size() >= 2) {
            t = p[0];
            a = p[1];
        } else {
            continue;
        }
        out.emplace_back(py_round(py_num(t, 0.0), 3), std::max(0.0, std::min(1.0, py_num(a, 0.0))));
    }
    std::ranges::stable_sort(out, {}, &EnvPoint::first);
    return out;
}

// Corta la cola de silencio: el clip aparece con el golpe y se queda.
std::vector<EnvPoint> hold_after_peak(std::vector<EnvPoint> pts) {
    if (pts.empty()) {
        return pts;
    }
    std::size_t peak_i = 0;
    for (std::size_t i = 1; i < pts.size(); ++i) {
        if (pts[i].second > pts[peak_i].second) {
            peak_i = i;
        }
    }
    const double peak = pts[peak_i].second;
    if (peak < 0.05) {
        return pts;
    }
    std::size_t cut = peak_i;
    const double thresh = std::max(0.35, peak * 0.8);
    while (cut + 1 < pts.size() && pts[cut + 1].second >= thresh) {
        ++cut;
    }
    pts.resize(cut + 1);
    return pts;
}

} // namespace

Result<std::string> normalize_motion(const Json& motion) {
    // (motion or "").strip().lower().replace("-", "_")
    std::string key;
    if (py_truthy(motion)) {
        if (!motion.is_string()) {
            return invalid("motion debe ser texto");
        }
        key = py_strip(motion.get_ref<const std::string&>());
        for (char& ch : key) {
            if (ch >= 'A' && ch <= 'Z') {
                ch = static_cast<char>(ch - 'A' + 'a');
            } else if (ch == '-') {
                ch = '_';
            }
        }
    }
    static constexpr std::pair<std::string_view, std::string_view> kAliases[] = {
        {"zoom", "zoom_in"},     {"fade", "fade_in"}, {"slide", "slide_left"}, {"spin_out", "spin"},
        {"whoosh", "slide_left"}, {"hit", "pop"},      {"draw", "draw_in"}};
    for (const auto& [alias, name] : kAliases) {
        if (key == alias) {
            key = name;
            break;
        }
    }
    if (std::ranges::find(kMotions, key) == kMotions.end()) {
        std::string all;
        for (const auto m : kMotions) {
            all += (all.empty() ? "'" : ", '") + std::string(m) + "'";
        }
        return invalid("motion inválido: " + (motion.is_string() ? motion.get<std::string>() : detail::py_str(motion)) +
                       " (usa [" + all + "])");
    }
    return key;
}

bool uses_scale_zoom(const TimelineClip& clip) {
    return is_overlay(clip) || clip.kind == "text" || clip.kind == "shape";
}

Result<std::vector<MotionItem>> build_motion_items(const TimelineClip& clip, const Json& motion,
                                                   const Json& duration, const Json& intensity, const Json& turns,
                                                   const Json& envelope, IdSource& ids) {
    auto norm = normalize_motion(motion);
    if (!norm) {
        return std::unexpected(norm.error());
    }
    const std::string& m = *norm;
    const Pose rest = rest_of(clip);
    const double clip_dur = std::max(0.05, clip_timeline_duration(clip));
    std::vector<MotionItem> items;

    if (m == "draw_in") {
        if (clip.kind != "shape") {
            return invalid("draw_in solo aplica a figuras (trazado, línea, flecha…)");
        }
        const double window =
            std::min(std::max(0.1, duration.is_null() ? 1.5 : py_num(duration, 1.5)), clip_dur);
        Props from{};
        Props to{};
        from[DRAW] = 0.0;
        to[DRAW] = 1.0;
        items.push_back(make_item(ids, 0.0, from, "linear"));
        items.push_back(make_item(ids, window, to, "ease-in-out"));
        return items;
    }
    const double inten = std::max(0.15, std::min(2.0, py_num(intensity, 1.0)));
    const double nturns = std::max(0.25, std::min(4.0, py_num(turns, 1.0)));
    const bool scale_zoom = uses_scale_zoom(clip);
    auto pose_at = [&](double amp) { return from_amp(scale_zoom, m, rest, amp, inten, nturns); };

    auto env_r = parse_envelope(envelope);
    if (!env_r) {
        return std::unexpected(env_r.error());
    }
    std::vector<EnvPoint> env = std::move(*env_r);
    if (!env.empty()) {
        if (is_appear(m)) {
            env = hold_after_peak(std::move(env));
        }
        for (std::size_t i = 0; i < env.size(); ++i) {
            const auto [t, amp] = env[i];
            if (t > clip_dur + 0.02) {
                continue;
            }
            items.push_back(make_item(ids, std::min(clip_dur, t), all_of(pose_at(amp)), i != 0 ? "linear" : "ease-out"));
        }
        if (!items.empty() && items.back().t < clip_dur - 0.04) {
            if (is_appear(m)) {
                const Props hold = items.back().props;
                items.push_back(make_item(ids, clip_dur, hold, "linear"));
            } else {
                items.push_back(make_item(ids, clip_dur, all_of(pose_at(env.back().second)), "ease-out"));
            }
        }
        if (items.empty()) {
            items.push_back(make_item(ids, 0.0, all_of(rest), "linear"));
        }
        return items;
    }

    double window = duration.is_null() ? 0.45 : py_num(duration, 0.45);
    window = std::min(std::max(0.08, window), std::max(0.08, clip_dur * 0.9));
    Pose start = pose_at(0.0);
    Pose end = pose_at(1.0);
    if (m == "spin") {
        start = rest;
        end = rest;
        end[ROT] = rest[ROT] + 360.0 * nturns;
    } else if (m == "pulse") {
        start = rest;
        start[SCALE] = rest[SCALE] * 0.72;
        end = rest;
    }
    if (m == "pop") {
        Pose mid = rest;
        mid[SCALE] = rest[SCALE] * (1.0 + 0.08 * inten);
        items.push_back(make_item(ids, 0.0, all_of(start), "ease-out"));
        items.push_back(make_item(ids, window * 0.55, all_of(mid), "ease-out"));
        items.push_back(make_item(ids, window, all_of(rest), "ease-in"));
        return items;
    }
    if (m == "spin") {
        window = clip_dur;
    }
    if (m == "fade_out") {
        const double t0 = std::max(0.0, clip_dur - window);
        items.push_back(make_item(ids, 0.0, all_of(rest), "linear"));
        items.push_back(make_item(ids, t0, all_of(rest), "ease-in"));
        items.push_back(make_item(ids, clip_dur, all_of(end), "ease-in"));
        return items;
    }
    items.push_back(make_item(ids, 0.0, all_of(start), "ease-out"));
    items.push_back(make_item(ids, window, all_of(end), "ease-out"));
    if (window < clip_dur - 0.05 && !in(m, {"spin", "fade_out", "zoom_in", "zoom_out"})) {
        items.push_back(make_item(ids, clip_dur, all_of(rest), "linear"));
    }
    return items;
}

Json merge_visual_keyframes(const TimelineClip& clip, const std::vector<MotionItem>& items) {
    Json merged = Json::array();
    for (const auto& it : items) {
        Json props = clip_props_at(clip, it.t).to_json();
        for (std::size_t i = 0; i < kMotionKeys.size(); ++i) {
            if (it.props[i]) {
                props[std::string(kMotionKeys[i])] = *it.props[i];
            }
        }
        Json item = Json::object();
        item["id"] = it.id;
        item["t"] = py_round(it.t, 3);
        item["interpolation"] = it.interpolation.empty() ? "ease-out" : it.interpolation;
        item["props"] = std::move(props);
        merged.push_back(std::move(item));
    }
    Json out = Json::object();
    out["enabled"] = true;
    out["items"] = std::move(merged);
    return out;
}

} // namespace gc::core
