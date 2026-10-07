#include "gc/core/fx_anims.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numbers>
#include <string>

#include "gc/core/timeline_rules.hpp"
#include "json_reader.hpp"
#include "py.hpp"

namespace gc::core {

using detail::py_float;
using detail::py_mod;
using detail::py_truthy;

namespace {

constexpr std::array<std::string_view, 4> kSlots{"in", "out", "loop", "combo"};

constexpr double kC1 = 1.70158;
constexpr double kC3 = kC1 + 1;
const double kC4 = (2 * std::numbers::pi) / 3;

const Json& field(const Json& obj, std::string_view key) {
    static const Json kNull;
    if (!obj.is_object()) {
        return kNull;
    }
    const auto it = obj.find(key);
    return it == obj.end() ? kNull : *it;
}

double bounce_out(double x) {
    const double n1 = 7.5625;
    const double d1 = 2.75;
    if (x < 1 / d1) {
        return n1 * x * x;
    }
    if (x < 2 / d1) {
        const double y = x - 1.5 / d1;
        return n1 * y * y + 0.75;
    }
    if (x < 2.5 / d1) {
        const double y = x - 2.25 / d1;
        return n1 * y * y + 0.9375;
    }
    const double y = x - 2.625 / d1;
    return n1 * y * y + 0.984375;
}

double elastic_out(double x) {
    if (x <= 0) {
        return 0.0;
    }
    if (x >= 1) {
        return 1.0;
    }
    return std::pow(2.0, -10 * x) * std::sin((x * 10 - 0.75) * kC4) + 1;
}

// Componente `i` de una clave [u, v, curva] como float() de Python (el catálogo es un
// recurso de la app: si viene roto, NaN en vez de excepción).
double key_at(const Json& key, std::size_t i) {
    if (!key.is_array() || key.size() <= i) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    return py_float(key[i]).value_or(std::numeric_limits<double>::quiet_NaN());
}

std::string_view key_ease(const Json& key) {
    if (key.is_array() && key.size() > 2 && key[2].is_string() && !key[2].get_ref<const std::string&>().empty()) {
        return key[2].get_ref<const std::string&>();
    }
    return "linear";
}

PoseDelta delta_at(const Json& anim, double u) {
    const Json& tracks = field(anim, "tracks");
    auto v = [&](std::string_view k, double d) { return eval_anim_track(field(tracks, k), u).value_or(d); };
    return {v("x", 0.0), v("y", 0.0), v("scale", 1.0), v("rotation", 0.0), v("opacity", 1.0)};
}

PoseDelta combine(const PoseDelta& a, const PoseDelta& b) {
    return {a.x + b.x, a.y + b.y, a.scale * b.scale, a.rotation + b.rotation, a.opacity * b.opacity};
}

// min(1.0, max(0.0, v)) de Python (con NaN da 0).
double unit(double v) { return std::min(1.0, std::max(0.0, v)); }

} // namespace

double anim_ease(std::string_view name, double x) {
    if (name == "inQuad") {
        return x * x;
    }
    if (name == "outQuad") {
        return 1 - (1 - x) * (1 - x);
    }
    if (name == "inOutQuad") {
        return x < 0.5 ? 2 * x * x : 1 - std::pow(-2 * x + 2, 2.0) / 2;
    }
    if (name == "inCubic") {
        return x * x * x;
    }
    if (name == "outCubic") {
        return 1 - std::pow(1 - x, 3.0);
    }
    if (name == "inOutCubic") {
        return x < 0.5 ? 4 * x * x * x : 1 - std::pow(-2 * x + 2, 3.0) / 2;
    }
    if (name == "inOutSine") {
        return -(std::cos(std::numbers::pi * x) - 1) / 2;
    }
    if (name == "inBack") {
        return kC3 * x * x * x - kC1 * x * x;
    }
    if (name == "outBack") {
        return 1 + kC3 * std::pow(x - 1, 3.0) + kC1 * std::pow(x - 1, 2.0);
    }
    if (name == "outElastic") {
        return elastic_out(x);
    }
    if (name == "outBounce") {
        return bounce_out(x);
    }
    if (name == "hold") {
        return x >= 1 ? 1.0 : 0.0;
    }
    return x; // linear y los desconocidos
}

std::optional<double> eval_anim_track(const Json& keys, double u) {
    if (!py_truthy(keys) || !keys.is_array()) {
        return std::nullopt;
    }
    if (u <= key_at(keys.front(), 0)) {
        return key_at(keys.front(), 1);
    }
    const Json& last = keys.back();
    if (u >= key_at(last, 0)) {
        return key_at(last, 1);
    }
    for (std::size_t i = 0; i + 1 < keys.size(); ++i) {
        const Json& a = keys[i];
        const Json& b = keys[i + 1];
        const double a0 = key_at(a, 0);
        const double b0 = key_at(b, 0);
        if (a0 <= u && u <= b0) {
            const double span = b0 - a0;
            const double f = span > 0 ? (u - a0) / span : 1.0;
            const double av = key_at(a, 1);
            return av + (key_at(b, 1) - av) * anim_ease(key_ease(b), f);
        }
    }
    return key_at(last, 1);
}

ClipAnimations clip_animations(const TimelineClip& clip, double clip_dur) {
    ClipAnimations out;
    if (!clip.animations || !clip.animations->is_object() || clip.animations->empty()) {
        return out;
    }
    out.catalog = fx_catalog();
    if (!out.catalog) {
        return out;
    }
    const Json& raw = *clip.animations;
    const double d_total = std::max(0.0, clip_dur);
    for (const std::string_view slot : kSlots) {
        const Json& entry = field(raw, slot);
        if (!entry.is_object()) {
            continue;
        }
        const Json* anim = out.catalog->anim(field(entry, "id"));
        if (anim == nullptr || field(*anim, "slot") != slot) {
            continue;
        }
        double dur = py_float(field(entry, "dur")).value_or(std::numeric_limits<double>::quiet_NaN());
        if (!std::isfinite(dur) || dur <= 0) {
            const Json& def = field(*anim, "dur");
            dur = py_truthy(def) ? py_float(def).value_or(0.0) : 0.0;
        }
        if (slot == "combo") {
            dur = d_total;
        }
        out.items.push_back({slot, anim, std::max(0.05, dur)});
    }
    // Entrada + salida no pueden pasar del clip: se reparten.
    auto find = [&](std::string_view slot) {
        const auto it = std::ranges::find(out.items, slot, &ClipAnimation::slot);
        return it == out.items.end() ? nullptr : &*it;
    };
    ClipAnimation* inn = find("in");
    ClipAnimation* outt = find("out");
    const double total = (inn != nullptr ? inn->dur : 0.0) + (outt != nullptr ? outt->dur : 0.0);
    if (d_total > 0 && total > d_total) {
        const double k = d_total / total;
        if (inn != nullptr) {
            inn->dur *= k;
        }
        if (outt != nullptr) {
            outt->dur *= k;
        }
    }
    return out;
}

bool has_animations(const TimelineClip& clip) { return !clip_animations(clip, 1.0).items.empty(); }

std::optional<PoseDelta> animation_delta(const TimelineClip& clip, double t, double clip_dur) {
    const ClipAnimations list = clip_animations(clip, clip_dur);
    if (list.items.empty()) {
        return std::nullopt;
    }
    const double d_total = std::max(0.0, clip_dur);
    PoseDelta d;
    for (const ClipAnimation& a : list.items) {
        double u = 0.0;
        if (a.slot == "in") {
            u = unit(t / a.dur);
        } else if (a.slot == "out") {
            u = unit((t - (d_total - a.dur)) / a.dur);
        } else if (a.slot == "loop") {
            u = py_mod(t / a.dur, 1.0);
        } else {
            u = d_total > 0 ? unit(t / d_total) : 0.0;
        }
        d = combine(d, delta_at(*a.anim, u));
    }
    return d;
}

Pose apply_delta(const Pose& pose, const PoseDelta& d) {
    // float(props.get("x") or 0.0): un cero (también -0.0) cuenta como 0.0.
    auto or_zero = [](double v) { return v != 0.0 ? v : 0.0; };
    return {or_zero(pose.x) + d.x, or_zero(pose.y) + d.y, pose.scale * d.scale, or_zero(pose.rotation) + d.rotation,
            pose.opacity * d.opacity};
}

// Python mira antes has_animations(clip); no hace falta: qué animaciones valen no
// depende de la duración, así que animation_delta ya devuelve nullopt en ese caso.
Pose render_pose(const TimelineClip& clip, double local_t) {
    const Pose pose = clip_pose(clip, local_t);
    const auto d = animation_delta(clip, local_t, clip_timeline_duration(clip));
    return d ? apply_delta(pose, *d) : pose;
}

double anim_opacity(const TimelineClip& clip, double local_t) {
    const auto d = animation_delta(clip, local_t, clip_timeline_duration(clip));
    return d ? d->opacity : 1.0;
}

std::vector<double> animation_sample_times(const TimelineClip& clip, double clip_dur) {
    const ClipAnimations list = clip_animations(clip, clip_dur);
    const double d_total = std::max(0.0, clip_dur);
    const double step = 1.0 / kAnimSampleFps;
    std::vector<double> times;
    auto add = [&](double a, double b) {
        // max(1, math.ceil(...)): con un tramo vacío o al revés, un solo intervalo.
        const double steps = std::ceil((b - a) / step - 1e-9);
        const std::int64_t n = steps >= 1 ? static_cast<std::int64_t>(steps) : 1;
        for (std::int64_t i = 0; i <= n; ++i) {
            times.push_back(detail::py_round(a + (b - a) * static_cast<double>(i) / static_cast<double>(n), 6));
        }
    };
    for (const ClipAnimation& a : list.items) {
        if (a.slot == "in") {
            add(0.0, std::min(d_total, a.dur));
        } else if (a.slot == "out") {
            add(std::max(0.0, d_total - a.dur), d_total);
        } else {
            add(0.0, d_total);
        }
    }
    // sorted(set(...)): 0.0 y -0.0 son el mismo elemento (no salen: los tramos empiezan en ≥ 0).
    std::ranges::sort(times);
    const auto [first, last] = std::ranges::unique(times);
    times.erase(first, last);
    return times;
}

} // namespace gc::core
