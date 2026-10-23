#include "gc/core/heatmap.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>

#include "py.hpp"

namespace gc::core {

using detail::py_round;

std::vector<HeatSegment> pick_heatmap_segments(std::span<const HeatPoint> heatmap, double duration,
                                               double min_score, std::int64_t max_clips, double max_duration,
                                               double padding) {
    // sorted(key=start_time): estable, los empates quedan en el orden de llegada.
    std::vector<HeatPoint> points(heatmap.begin(), heatmap.end());
    std::ranges::stable_sort(points, [](const HeatPoint& a, const HeatPoint& b) { return a.start_time < b.start_time; });

    std::vector<std::vector<HeatPoint>> runs;
    std::vector<HeatPoint> current;
    for (const HeatPoint& p : points) {
        if (p.value >= min_score) {
            current.push_back(p);
        } else if (!current.empty()) {
            runs.push_back(std::move(current));
            current.clear();
        }
    }
    if (!current.empty()) {
        runs.push_back(std::move(current));
    }

    std::vector<HeatSegment> segments;
    for (const auto& run : runs) {
        const double a = run.front().start_time - padding;
        const double start = a > 0.0 ? a : 0.0; // max(0.0, …)
        const double b = run.back().end_time + padding;
        double end = b < duration ? b : duration; // min(duration, …)
        if (end - start > max_duration) {
            end = start + max_duration; // se recorta manteniendo el arranque
        }
        double score = run.front().value;
        for (const HeatPoint& p : run) {
            if (p.value > score) { // max(): se queda el primero de los iguales
                score = p.value;
            }
        }
        segments.push_back({0, py_round(start, 2), py_round(end, 2), py_round(score, 4), py_round(end - start, 2)});
    }

    // Los mejores por score (sort(reverse=True) también es estable) y luego por tiempo.
    std::ranges::stable_sort(segments, [](const HeatSegment& x, const HeatSegment& y) { return x.score > y.score; });
    const auto n = static_cast<std::int64_t>(segments.size());
    const std::int64_t keep = std::clamp(max_clips < 0 ? n + max_clips : max_clips, std::int64_t{0}, n);
    segments.resize(static_cast<std::size_t>(keep));
    std::ranges::stable_sort(segments, [](const HeatSegment& x, const HeatSegment& y) { return x.start < y.start; });
    for (std::size_t i = 0; i < segments.size(); ++i) {
        segments[i].index = static_cast<std::int64_t>(i) + 1;
    }
    return segments;
}

} // namespace gc::core
