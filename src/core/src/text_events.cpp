#include "gc/core/text_events.hpp"

#include <algorithm>
#include <cmath>
#include <format>

#include "py.hpp"

namespace gc::core {

using detail::py_mod;

namespace {

// x // y de Python con reales (float_divmod de CPython).
double py_floordiv(double x, double y) {
    const double mod = std::fmod(x, y);
    double div = (x - mod) / y;
    if (mod != 0 && ((y < 0) != (mod < 0))) {
        div -= 1.0;
    }
    if (div == 0) {
        return std::copysign(0.0, x / y);
    }
    double floordiv = std::floor(div);
    if (div - floordiv > 0.5) {
        floordiv += 1.0;
    }
    return floordiv;
}

std::vector<std::string> code_points(std::string_view text) {
    std::vector<std::string> out;
    for (std::size_t i = 0; i < text.size();) {
        const std::size_t from = i;
        (void)detail::utf8_next(text, i);
        out.emplace_back(text.substr(from, i - from));
    }
    return out;
}

} // namespace

std::int64_t ass_time_ms(double t) {
    t = t > 0.0 ? std::min(t, 1e12) : 0.0; // max(0.0, t); acotado para que quepa en ms
    const auto h = static_cast<std::int64_t>(py_floordiv(t, 3600));
    const auto m = static_cast<std::int64_t>(py_floordiv(py_mod(t, 3600), 60));
    // "{:05.2f}" de los segundos: centésimas redondeadas al par sobre el valor exacto
    const std::string s = std::format("{:.2f}", py_mod(t, 60));
    const auto dot = s.find('.');
    const std::int64_t cs = std::stoll(s.substr(0, dot)) * 100 + std::stoll(s.substr(dot + 1));
    return (h * 3600 + m * 60) * 1000 + cs * 10;
}

std::int64_t ass_frame_ms(std::int64_t k, double fps) {
    return static_cast<std::int64_t>(static_cast<double>(k) * (1.0 / fps) * 1000);
}

bool text_in_ass(const TimelineClip& clip) {
    return text_clip_duration(clip) > 0.02 && !split_words(clip.text.value_or("")).empty() && !clip.disabled;
}

std::vector<TextEvent> static_text_events(const TimelineClip& clip, const Json& posed_style) {
    const std::string text = clip.text.value_or("");
    const auto words = split_words(text);
    std::vector<TextEvent> events;
    if (words.empty()) {
        return events;
    }
    const double dur = text_clip_duration(clip);
    const double start = std::max(0.0, clip.start);
    const Json appear = posed_style.is_object() && posed_style.contains("block_appear") ? posed_style.at("block_appear")
                                                                                         : Json();
    if (appear.is_string() && appear.get_ref<const std::string&>() == "typing") {
        // _typing_dialogues: un evento por paso con lo escrito (solo si ya hay alguna palabra)
        const auto chars = code_points(text);
        const auto n = static_cast<double>(chars.size());
        if (dur <= 0) {
            return events;
        }
        const auto steps = static_cast<std::int64_t>(
            std::min({n, std::max(1.0, std::nearbyint(dur * 30)), 240.0}));
        for (std::int64_t i = 0; i < steps; ++i) {
            const double t0 = start + dur * static_cast<double>(i) / static_cast<double>(steps);
            const double t1 = start + dur * static_cast<double>(i + 1) / static_cast<double>(steps);
            const auto k = static_cast<std::size_t>(
                std::max(1.0, std::ceil(static_cast<double>(i + 1) / static_cast<double>(steps) * n)));
            std::string shown;
            for (std::size_t j = 0; j < k && j < chars.size(); ++j) {
                shown += chars[j];
            }
            if (split_words(shown).empty()) {
                continue; // sin palabras no hay líneas
            }
            events.push_back({ass_time_ms(t0), ass_time_ms(t1), TextContent{std::move(shown)}, true});
        }
        return events;
    }
    const WordFx fx = word_fx_set(posed_style);
    if (!fx.any() || words.size() == 1) {
        // un solo vocablo con karaoke está activo todo el clip
        events.push_back({ass_time_ms(start), ass_time_ms(start + std::max(0.04, dur)), TextContent{text, -1, fx.any()},
                          true});
        return events;
    }
    const auto windows = word_windows(clip);
    for (std::size_t i = 0; i < windows.size(); ++i) {
        events.push_back({ass_time_ms(windows[i].first), ass_time_ms(windows[i].second),
                          TextContent{text, static_cast<std::int64_t>(i)}, i == 0});
    }
    return events;
}

} // namespace gc::core
