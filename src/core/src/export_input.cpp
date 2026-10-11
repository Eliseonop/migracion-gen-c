#include "gc/core/export_input.hpp"

#include <algorithm>
#include <cmath>
#include <format>

#include "gc/core/timeline_rules.hpp"
#include "py.hpp"

namespace gc::core {

namespace {

// `f"{x:.3f}"` de Python: "nan" sin signo. std::format pondría "-nan" con el NaN negativo que
// da x86 en `inf − inf` (una imagen con entrada y salida infinitas).
std::string fixed3(double x) { return std::isnan(x) ? std::string("nan") : std::format("{:.3f}", x); }

bool ends_with(std::string_view s, std::string_view tail) {
    return s.size() >= tail.size() && s.substr(s.size() - tail.size()) == tail;
}

} // namespace

bool is_still_clip(const TimelineClip& clip) {
    return clip.kind == "image" || clip.kind == "shape" || clip.asset_kind == "images";
}

std::pair<double, double> ffmpeg_trim_window(const TimelineClip& clip, int fps) {
    const double inp = clip.in_point;
    double out = clip.out_point;
    if (is_still_clip(clip)) {
        return {0.0, std::max(out - inp, 0.1)};
    }
    const double src = clip.source_duration;
    if (src > 0 && out >= src - 1e-4) {
        // 1.0 / max(1, int(fps) or 30)
        const double frame = 1.0 / static_cast<double>(std::max(1, fps != 0 ? fps : 30));
        const double pull = std::max(kLastFramePull, frame);
        out = std::max(inp + 1e-3, src - pull);
    }
    return {inp, out};
}

std::string ffmpeg_trim_filter(const TimelineClip& clip, int fps) {
    const auto [tin, tout] = ffmpeg_trim_window(clip, fps);
    const std::string trim = "trim=" + fixed3(tin) + ":" + fixed3(tout);
    return is_still_clip(clip) ? trim : "fps=" + std::to_string(fps) + "," + trim;
}

std::vector<std::string> ffmpeg_input_args(const TimelineClip& clip, std::string_view path, int fps) {
    const std::string p(path);
    if (clip.kind == "motion" || ends_with(detail::py_lower(p), ".webm")) {
        return {"-c:v", "libvpx-vp9", "-i", p};
    }
    if (p.contains("%0")) {
        return {"-framerate", std::to_string(fps), "-start_number", "0", "-i", p};
    }
    if (is_still_clip(clip)) {
        const double t = ffmpeg_trim_window(clip).second + 0.05;
        if (ends_with(detail::py_lower(p), ".gif")) {
            const std::string ignore = clip.loop == false ? "1" : "0"; // `loop is False`
            return {"-ignore_loop", ignore, "-t", fixed3(t), "-i", p};
        }
        return {"-loop", "1", "-framerate", std::to_string(fps), "-t", fixed3(t), "-i", p};
    }
    return {"-i", p};
}

double export_total_duration(const Timeline& timeline) {
    double total = 0.0;
    for (const auto& c : timeline.clips) {
        const bool has_track = std::ranges::any_of(timeline.tracks, [&](const TimelineTrack& tr) { return tr.id == c->track_id; });
        if (has_track) {
            total = std::max(total, c->start + clip_timeline_duration(*c));
        }
    }
    return detail::py_round(total, 3);
}

} // namespace gc::core
