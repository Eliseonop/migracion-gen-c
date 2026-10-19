#include "gc/core/timeline_view.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <ranges>
#include <unordered_set>

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
    const auto it = std::ranges::find_if(kChoices, [&](int c) { return c == fps; });
    return it == kChoices.end() ? 30 : *it;
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
    for (double t = std::floor(a / step) * step; t <= b + step * 0.01; t += step) {
        push(t, true);
    }
    const double minor = step >= 1 ? step / 5 : (step > frame * 1.5 ? frame : 0);
    if (minor > 1e-9 && ticks.size() < 250) {
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

} // namespace gc::core
