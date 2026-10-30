#include "gc/core/timeline_view.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <format>
#include <numbers>
#include <ranges>
#include <string>
#include <unordered_set>
#include <vector>

#include "gc/core/timeline_rules.hpp"
#include "gc/core/timeline_ui.hpp"

namespace gc::core {

namespace {

double or_zero(double x) {
    return std::isnan(x) ? 0.0 : x;
}

std::string pad2(long n) {
    return std::format("{:02}", n);
}

} // namespace

int normalize_fps(double fps) {
    constexpr std::array<int, 5> kChoices{24, 25, 30, 50, 60};
    for (const int c : kChoices) { // un bucle: en MSVC el iterador de std::array no es un puntero
        if (c == fps) {
            return c;
        }
    }
    return 30;
}

double frame_duration(double fps) {
    return 1.0 / normalize_fps(fps);
}

double max_pps(double fps, double duration) {
    const double by_frame = 80 / frame_duration(fps);
    const double d = or_zero(duration) == 0 ? 12 : duration;
    return std::min(by_frame, std::max(600.0, 16'000'000 / std::max(d, 12.0)));
}

double min_pps(double duration, double view_width) {
    const double span = std::max(or_zero(duration), 12.0);
    const double w = or_zero(view_width) == 0 ? 900 : view_width;
    return std::min(kPpsShortMin, std::max(kPpsFloor, std::max(80.0, w) / span));
}

double clamp_pps(double pps, double duration, double view_width, double fps) {
    const double lo = min_pps(duration, view_width);
    const double p = or_zero(pps) == 0 ? lo : pps;
    return std::min(max_pps(fps, duration), std::max(lo, p));
}

double zoom_by_drag(double start_pps, double dx, double duration, double view_width, double fps) {
    return clamp_pps(start_pps * std::pow(1.01, dx), duration, view_width, fps);
}

std::vector<double> tick_steps(double fps) {
    const double f = frame_duration(fps);
    std::vector<double> raw{f,  2 * f, 5 * f, 10 * f, 0.5, 1,   2,   5,    10,
                            15, 30,    60,    120,    300, 600, 900, 1800, 3600};
    std::ranges::stable_sort(raw);
    std::vector<double> out;
    for (const double s : raw) {
        if (out.empty() || s - out.back() > f * 0.35) {
            out.push_back(s);
        }
    }
    return out;
}

double tick_step(double pps, double fps, bool dense) {
    const double target = dense ? 48 : 72;
    const double p = or_zero(pps) == 0 ? 1 : pps;
    const double raw = target / std::max(p, 0.001);
    const auto steps = tick_steps(fps);
    const auto it = std::ranges::find_if(steps, [&](double s) { return s >= raw; });
    return it == steps.end() ? steps.back() * 2 : *it;
}

std::string fmt_ruler(double seconds, double fps, std::optional<double> step, bool long_form) {
    const double t = std::max(0.0, or_zero(seconds));
    const int f = normalize_fps(fps);
    const bool frames_shown = step && *step < 0.999;
    long whole = static_cast<long>(std::floor(t));
    long frames = 0;
    if (frames_shown) {
        const auto total = std::lround(t * f);
        whole = total / f;
        frames = total % f;
    }
    const long h = whole / 3600;
    const long m = (whole % 3600) / 60;
    const long s = whole % 60;
    if (frames_shown) {
        return h > 0 ? std::format("{}:{}:{}:{}", h, pad2(m), pad2(s), pad2(frames))
                     : std::format("{}:{}:{}", m, pad2(s), pad2(frames));
    }
    return long_form || h > 0 ? std::format("{}:{}:{}", h, pad2(m), pad2(s))
                              : std::format("{}:{}", m, pad2(s));
}

std::vector<Tick> build_ticks(double max_t, double pps, double fps, double scroll_x, double view_width,
                              bool dense) {
    const double step = tick_step(pps, fps, dense);
    const double frame = frame_duration(fps);
    const double max = std::max(0.0, or_zero(max_t));
    const double p = std::max(or_zero(pps) == 0 ? 1 : pps, 0.001);
    const double view_w = std::max(200.0, or_zero(view_width) == 0 ? 1200 : view_width);
    const double view_start = std::max(0.0, or_zero(scroll_x)) / p;
    const double pad = (view_w * 0.35) / p;
    const double a = std::max(0.0, view_start - pad);
    const double b = std::min(max, view_start + view_w / p + pad);
    std::vector<Tick> ticks;
    std::unordered_set<std::string> seen;
    const auto push = [&](double t, bool major) {
        const double n = js_fixed(t, 6);
        if (n < -1e-9 || n > max + 1e-9 || !seen.insert(js_to_fixed(n, 6)).second) {
            return;
        }
        ticks.push_back({n, major, step});
    };
    // Los bucles con un double copian el acumulado de rulerTicks (timelineView.js): con un contador
    // entero, las marcas no saldrían iguales.
    // NOLINTNEXTLINE(bugprone-float-loop-counter,clang-analyzer-security.FloatLoopCounter)
    for (double t = std::floor(a / step) * step; t <= b + step * 0.01; t += step) {
        push(t, true);
    }
    const double minor = step >= 1 ? step / 5 : (step > frame * 1.5 ? frame : 0);
    if (minor > 1e-9 && ticks.size() < 250) {
        // NOLINTNEXTLINE(bugprone-float-loop-counter,clang-analyzer-security.FloatLoopCounter)
        for (double t = std::floor(a / minor) * minor; t <= b + minor * 0.01; t += minor) {
            push(t, false);
        }
    }
    return ticks;
}

int clamp_row_height(std::optional<double> raw) {
    if (!raw || !std::isfinite(*raw)) {
        return kRowHeightDefault;
    }
    const double n = std::round(*raw);
    return static_cast<int>(std::clamp(n, double{kRowHeightMin}, double{kRowHeightMax}));
}

int track_row_height(std::string_view kind, std::optional<double> row_height) {
    const int base = clamp_row_height(row_height);
    if (kind != "text") {
        return base;
    }
    return std::max(28, std::min(base, static_cast<int>(std::round(base * 0.55))));
}

int step_row_height(std::optional<double> row_height, double delta_y) {
    return clamp_row_height(clamp_row_height(row_height) * (delta_y < 0 ? 1.1 : 0.9));
}

std::vector<const TimelineTrack*> display_tracks(const std::vector<TimelineTrack>& tracks) {
    std::vector<const TimelineTrack*> out;
    for (const auto& t : tracks | std::views::reverse) {
        if (is_stack_track(t)) {
            out.push_back(&t);
        }
    }
    for (const auto& t : tracks) {
        if (!is_stack_track(t)) {
            out.push_back(&t);
        }
    }
    return out;
}

namespace {

// Number::toString de ECMAScript: los dígitos más cortos que vuelven al mismo doble y la notación
// según el exponente (fija entre 1e-6 y 1e21; si no, «d.ddde±n»).
std::string js_number_string(double v) {
    if (std::isnan(v)) {
        return "NaN";
    }
    if (v == 0) {
        return "0";
    }
    if (std::isinf(v)) {
        return v < 0 ? "-Infinity" : "Infinity";
    }
    std::array<char, 64> buf{};
    const auto r = std::to_chars(buf.data(), buf.data() + buf.size(), std::abs(v), std::chars_format::scientific);
    const std::string s(buf.data(), r.ptr);
    const auto e = s.find('e');
    std::string digits;
    for (const char c : s.substr(0, e)) {
        if (c != '.') {
            digits += c;
        }
    }
    const auto k = static_cast<int>(digits.size());
    const int n = std::stoi(s.substr(e + 1)) + 1; // v = 0.dígitos × 10^n
    std::string out;
    if (k <= n && n <= 21) {
        out = digits + std::string(static_cast<std::size_t>(n - k), '0');
    } else if (0 < n && n <= 21) {
        out = digits.substr(0, static_cast<std::size_t>(n)) + "." + digits.substr(static_cast<std::size_t>(n));
    } else if (-6 < n && n <= 0) {
        out = "0." + std::string(static_cast<std::size_t>(-n), '0') + digits;
    } else {
        const int e1 = n - 1;
        out = digits.substr(0, 1) + (k > 1 ? "." + digits.substr(1) : "") + "e" + (e1 >= 0 ? "+" : "-") +
              std::to_string(std::abs(e1));
    }
    return v < 0 ? "-" + out : out;
}

// String(v) de JS para lo que puede llegar como semilla.
std::string js_string(const Json& v) {
    if (v.is_string()) {
        return v.get<std::string>();
    }
    if (v.is_boolean()) {
        return v.get<bool>() ? "true" : "false";
    }
    if (v.is_number()) {
        return js_number_string(v.get<double>());
    }
    if (v.is_null()) {
        return "null";
    }
    if (v.is_array()) { // Array.prototype.join(",")
        std::string out;
        for (std::size_t i = 0; i < v.size(); ++i) {
            out += (i > 0 ? "," : "") + (v[i].is_null() ? std::string() : js_string(v[i]));
        }
        return out;
    }
    return "[object Object]";
}

bool js_truthy(const Json& v) {
    if (v.is_null()) {
        return false;
    }
    if (v.is_boolean()) {
        return v.get<bool>();
    }
    if (v.is_number()) {
        const double d = v.get<double>();
        return d != 0 && !std::isnan(d);
    }
    if (v.is_string()) {
        return !v.get<std::string>().empty();
    }
    return true;
}

// Las unidades UTF-16 de un texto UTF-8 (charCodeAt).
std::vector<std::uint32_t> utf16_units(const std::string& s) {
    std::vector<std::uint32_t> out;
    std::size_t i = 0;
    while (i < s.size()) {
        const auto lead = static_cast<unsigned char>(s[i]);
        std::uint32_t cp = lead;
        std::size_t len = 1;
        if (lead >= 0xF0) {
            cp = lead & 0x07U;
            len = 4;
        } else if (lead >= 0xE0) {
            cp = lead & 0x0FU;
            len = 3;
        } else if (lead >= 0xC0) {
            cp = lead & 0x1FU;
            len = 2;
        }
        for (std::size_t j = 1; j < len && i + j < s.size(); ++j) {
            cp = (cp << 6U) | (static_cast<unsigned char>(s[i + j]) & 0x3FU);
        }
        if (cp >= 0x10000) {
            cp -= 0x10000;
            out.push_back(0xD800U + (cp >> 10U));
            out.push_back(0xDC00U + (cp & 0x3FFU));
        } else {
            out.push_back(cp);
        }
        i += len;
    }
    return out;
}

} // namespace

std::vector<double> pseudo_waveform(const Json& seed, const Json& bars) {
    const double n = bars.is_null() ? 48.0 : js_number(bars);
    std::uint32_t h = 0;
    for (const std::uint32_t c : utf16_units(js_truthy(seed) ? js_string(seed) : std::string("a"))) {
        h = (h * 31U) + c; // (h * 31 + c) >>> 0: exacto en doble y después módulo 2³²
    }
    std::vector<double> out;
    double hd = h;
    // Las vueltas de `for (i = 0; i < bars; i++)`: ⌈bars⌉ si es positivo (acotado: con Infinity, JS
    // no acaba nunca); con 0, negativo o NaN, ninguna.
    const auto count = n > 0 ? static_cast<std::int64_t>(std::ceil(std::min(n, 1e6))) : std::int64_t{0};
    for (std::int64_t k = 0; k < count; ++k) {
        const auto i = static_cast<double>(k);
        // (h * 1103515245 + 12345) & 0x7fffffff: el producto en doble (se redondea), ToInt32.
        const double p = (hd * 1103515245.0) + 12345.0;
        const auto wrapped = static_cast<std::uint64_t>(std::fmod(p, 4294967296.0));
        hd = static_cast<double>(wrapped & 0x7fffffffULL);
        const double base = 0.25 + ((std::fmod(hd, 1000.0) / 1000) * 0.75);
        const double env = 0.6 + (0.4 * std::sin((i / n) * std::numbers::pi * 3));
        out.push_back(std::max(0.12, std::min(1.0, base * env)));
    }
    return out;
}

} // namespace gc::core
