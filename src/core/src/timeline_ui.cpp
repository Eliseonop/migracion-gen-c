#include "gc/core/timeline_ui.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <map>
#include <ranges>

namespace gc::core {

namespace {

double or_zero(double x) {
    return std::isnan(x) ? 0.0 : x;
}

bool excluded(std::span<const std::string> exclude, std::string_view id) {
    return std::ranges::find(exclude, id) != exclude.end();
}

struct Range {
    double start;
    double end;
};

std::vector<Range> busy_ranges(const ClipList& clips, std::string_view track_id,
                               std::span<const std::string> exclude) {
    std::vector<Range> out;
    for (const auto& c : clips) {
        if (c->track_id == track_id && !excluded(exclude, c->id)) {
            const Range r{std::max(0.0, or_zero(c->start)), ui_clip_end(*c)};
            if (r.end > r.start) {
                out.push_back(r);
            }
        }
    }
    std::ranges::stable_sort(out, {}, &Range::start);
    return out;
}

ClipPatch resize_free_clip(const TimelineClip& c, TrimEdge edge, double delta) {
    const double start = or_zero(c.start);
    const double in = or_zero(c.in_point);
    const double out = or_zero(c.out_point);
    const double src = c.source_duration > 0 ? c.source_duration : out;
    const double d = or_zero(delta);
    if (edge == TrimEdge::Right) {
        const double new_out = in + std::max(kMinClipDuration, std::max(0.0, out - in) + d);
        return {.out_point = js_fixed3(new_out), .source_duration = js_fixed3(std::max(src, new_out))};
    }
    if (in <= 1e-9 && d < 0) {
        const double ns = std::max(0.0, start + d);
        const double new_out = out + (start - ns);
        return {.start = js_fixed3(ns),
                .in_point = 0.0,
                .out_point = js_fixed3(new_out),
                .source_duration = js_fixed3(std::max(src, new_out))};
    }
    const double ni = std::min(out - kMinClipDuration, std::max(0.0, in + d));
    return {.start = js_fixed3(std::max(0.0, start + (ni - in))), .in_point = js_fixed3(ni)};
}

const TimelineClip* find_clip(const ClipList& clips, std::string_view id) {
    const auto it = std::ranges::find_if(clips, [&](const auto& c) { return c->id == id; });
    return it == clips.end() ? nullptr : &**it;
}

bool contains(const std::vector<std::string>& ids, std::string_view id) {
    return std::ranges::find(ids, id) != ids.end();
}

} // namespace

std::string js_to_fixed(double x, int digits) {
    const double scale = std::pow(10.0, digits);
    const double scaled = x * scale;
    const double lo = std::floor(scaled);
    char text[400];
    if (scaled - lo == 0.5 && std::fma(x, scale, -scaled) == 0.0) { // empate exacto: lejos del cero
        std::snprintf(text, sizeof text, "%.*f", digits, (x < 0 ? lo : lo + 1) / scale);
    } else {
        std::snprintf(text, sizeof text, "%.*f", digits, x);
    }
    return text;
}

double js_fixed(double x, int digits) {
    return std::isfinite(x) ? std::strtod(js_to_fixed(x, digits).c_str(), nullptr) : x;
}

double js_fixed3(double x) {
    return js_fixed(x, 3);
}

double js_number(const Json& v) {
    if (v.is_number()) {
        return v.get<double>();
    }
    if (v.is_null()) {
        return 0;
    }
    if (v.is_boolean()) {
        return v.get<bool>() ? 1 : 0;
    }
    if (v.is_string()) {
        const std::string& s = v.get_ref<const std::string&>();
        const auto first = s.find_first_not_of(" \t\n\r");
        if (first == std::string::npos) {
            return 0;
        }
        char* end = nullptr;
        const double n = std::strtod(s.c_str() + first, &end);
        const bool whole = end != s.c_str() + first &&
                           std::string_view(end).find_first_not_of(" \t\n\r") == std::string_view::npos;
        return whole ? n : std::numeric_limits<double>::quiet_NaN();
    }
    return std::numeric_limits<double>::quiet_NaN();
}

std::string lane_kind(std::string_view clip_kind) {
    if (clip_kind == "image" || clip_kind == "video" || clip_kind == "shape" || clip_kind == "adjustment") {
        return "video";
    }
    return std::string(clip_kind);
}

bool has_free_duration(const TimelineClip& clip) {
    const std::string& k = clip.kind;
    return k == "text" || k == "image" || k == "shape" || k == "adjustment";
}

double ui_clip_speed(const TimelineClip& clip) {
    if (has_free_duration(clip) || !std::isfinite(clip.speed) || clip.speed <= 0) {
        return 1.0;
    }
    return std::clamp(clip.speed, 0.1, 10.0);
}

double ui_clip_duration(const TimelineClip& clip) {
    return std::max(0.0, clip.out_point - clip.in_point) / ui_clip_speed(clip);
}

double ui_clip_end(const TimelineClip& clip) {
    return clip.start + ui_clip_duration(clip);
}

std::vector<const TimelineClip*> clips_on_track_sorted(const ClipList& clips, std::string_view track_id) {
    std::vector<const TimelineClip*> out;
    for (const auto& c : clips) {
        if (c->track_id == track_id) {
            out.push_back(&*c);
        }
    }
    std::ranges::stable_sort(out, [](const TimelineClip* a, const TimelineClip* b) {
        return a->start != b->start ? a->start < b->start : a->id < b->id;
    });
    return out;
}

double free_start_on_track(const ClipList& clips, std::string_view track_id, double desired, double duration,
                           std::span<const std::string> exclude) {
    const double span = std::max(0.0, or_zero(duration));
    double t = std::max(0.0, or_zero(desired));
    if (span <= 0) {
        return js_fixed3(t);
    }
    for (const Range& r : busy_ranges(clips, track_id, exclude)) {
        if (r.end <= t) {
            continue;
        }
        if (t + span <= r.start) {
            break;
        }
        t = r.end;
    }
    return js_fixed3(t);
}

double clamp_start_no_overlap(const ClipList& clips, std::string_view track_id, double desired,
                              double duration, std::span<const std::string> exclude) {
    const double span = std::max(0.0, or_zero(duration));
    const double want = std::max(0.0, or_zero(desired));
    std::vector<Range> busy;
    for (const Range& r : busy_ranges(clips, track_id, exclude)) {
        if (!busy.empty() && r.start <= busy.back().end) {
            busy.back().end = std::max(busy.back().end, r.end);
        } else {
            busy.push_back(r);
        }
    }
    if (span <= 0 || busy.empty()) {
        return js_fixed3(want);
    }
    std::vector<Range> gaps;
    double cursor = 0.0;
    for (const Range& r : busy) {
        if (r.start > cursor) {
            gaps.push_back({cursor, r.start});
        }
        cursor = std::max(cursor, r.end);
    }
    gaps.push_back({cursor, std::numeric_limits<double>::infinity()});
    std::optional<std::pair<double, double>> best; // (inicio, distancia)
    for (const Range& g : gaps) {
        const double hi = g.end - span;
        if (hi < g.start) {
            continue;
        }
        const double cand = std::min(std::max(want, g.start), hi);
        const double d = std::abs(cand - want);
        if (!best || d < best->second) {
            best = {cand, d};
        }
    }
    return js_fixed3(best ? best->first : cursor);
}

TrimBounds trim_bounds(const ClipList& clips, const TimelineClip& clip) {
    const double s = std::max(0.0, or_zero(clip.start));
    const double e = ui_clip_end(clip);
    TrimBounds b;
    for (const auto& c : clips) {
        if (c->id == clip.id || c->track_id != clip.track_id) {
            continue;
        }
        const double ce = ui_clip_end(*c);
        if (ce <= s) {
            b.left = std::max(b.left, ce);
        } else if (c->start >= e) {
            b.right = std::min(b.right, c->start);
        }
    }
    return b;
}

double clamp_trim_delta(const ClipList& clips, const TimelineClip& clip, TrimEdge edge, double delta) {
    const double d = or_zero(delta);
    const TrimBounds b = trim_bounds(clips, clip);
    if (edge == TrimEdge::Left) {
        return std::max(d, b.left - std::max(0.0, or_zero(clip.start)));
    }
    return std::isfinite(b.right) ? std::min(d, b.right - ui_clip_end(clip)) : d;
}

void ClipPatch::apply(TimelineClip& clip) const {
    clip.start = start.value_or(clip.start);
    clip.in_point = in_point.value_or(clip.in_point);
    clip.out_point = out_point.value_or(clip.out_point);
    clip.source_duration = source_duration.value_or(clip.source_duration);
}

ClipPatch trim_clip_patch(const TimelineClip& clip, TrimEdge edge, double delta) {
    if (has_free_duration(clip)) {
        return resize_free_clip(clip, edge, delta);
    }
    const double sp = ui_clip_speed(clip);
    const double min_src = kMinClipDuration * sp;
    if (edge == TrimEdge::Left) {
        const double ni = std::min(clip.out_point - min_src, std::max(0.0, clip.in_point + delta * sp));
        return {.start = js_fixed3(std::max(0.0, clip.start + (ni - clip.in_point) / sp)),
                .in_point = js_fixed3(ni)};
    }
    const double max_out = clip.source_duration > 0 ? clip.source_duration : clip.out_point + 3600;
    return {.out_point =
                js_fixed3(std::min(max_out, std::max(clip.in_point + min_src, clip.out_point + delta * sp)))};
}

std::vector<std::string> range_select_on_track(const ClipList& clips, std::string_view track_id,
                                               std::string_view from_id, std::string_view to_id) {
    const auto row = clips_on_track_sorted(clips, track_id);
    const auto index = [&](std::string_view id) {
        const auto it = std::ranges::find_if(row, [&](const TimelineClip* c) { return c->id == id; });
        return it == row.end() ? -1L : static_cast<long>(it - row.begin());
    };
    const long i0 = index(from_id);
    const long i1 = index(to_id);
    if (i1 < 0) {
        return {};
    }
    if (i0 < 0) {
        return {std::string(to_id)};
    }
    std::vector<std::string> out;
    for (long i = std::min(i0, i1); i <= std::max(i0, i1); ++i) {
        out.push_back(row[static_cast<std::size_t>(i)]->id);
    }
    return out;
}

ClipSelection next_clip_selection(const ClipList& clips, const ClipSelection& current,
                                  std::string_view clicked_id, SelectMods mods) {
    const TimelineClip* clicked = find_clip(clips, clicked_id);
    if (!clicked) {
        return current;
    }
    const std::string clicked_s(clicked_id);
    std::vector<std::string> selected;
    std::ranges::copy_if(current.ids, std::back_inserter(selected),
                         [&](const std::string& id) { return find_clip(clips, id) != nullptr; });
    const std::string anchor_id = current.anchor.value_or("");
    const TimelineClip* anchor = find_clip(clips, anchor_id);

    if (mods.additive) {
        const TimelineClip* first = selected.empty() ? nullptr : find_clip(clips, selected.front());
        if (first && !first->kind.empty() && clicked->kind != first->kind) {
            return {{clicked_s}, clicked_s};
        }
        if (contains(selected, clicked_id)) {
            std::erase(selected, clicked_s);
            std::optional<std::string> next;
            if (current.anchor && contains(selected, *current.anchor)) {
                next = current.anchor;
            } else if (!selected.empty()) {
                next = selected.back();
            }
            return {selected, next};
        }
        selected.push_back(clicked_s);
        return {selected, clicked_s};
    }
    if (mods.range && anchor && anchor->track_id == clicked->track_id) {
        return {range_select_on_track(clips, clicked->track_id, anchor_id, clicked_id), clicked_s};
    }
    if (mods.range) {
        if (contains(selected, clicked_id)) {
            std::erase(selected, clicked_s);
            if (selected.empty()) {
                return {{clicked_s}, clicked_s};
            }
            return {selected, selected.front()};
        }
        std::vector<std::string> ids = !selected.empty()    ? selected
                                       : !anchor_id.empty() ? std::vector<std::string>{anchor_id}
                                                            : std::vector<std::string>{};
        if (!contains(ids, clicked_id)) {
            ids.push_back(clicked_s);
        }
        return {ids, ids.front()};
    }
    if (mods.keep_group && contains(selected, clicked_id) && selected.size() > 1) {
        return {selected, clicked_s};
    }
    return {{clicked_s}, clicked_s};
}

std::vector<std::string> emptied_tracks(const std::vector<TimelineTrack>& tracks, const ClipList& clips,
                                        std::span<const std::string> candidates) {
    std::map<std::string, int> left;
    for (const auto& t : tracks) {
        if (!t.locked) {
            ++left[t.kind];
        }
    }
    std::vector<std::string> out;
    for (const auto& t : tracks) {
        const bool used = std::ranges::any_of(clips, [&](const auto& c) { return c->track_id == t.id; });
        if (std::ranges::find(candidates, t.id) == candidates.end() || used || t.locked ||
            left[t.kind] <= 1) {
            continue;
        }
        --left[t.kind];
        out.push_back(t.id);
    }
    return out;
}

TimelineTrack build_track(std::string_view kind, const std::vector<TimelineTrack>& tracks,
                          std::string_view id_suffix) {
    const std::string prefix = kind == "video" ? "V" : kind == "audio" ? "A" : "T";
    long n = 0;
    for (const auto& t : tracks) {
        if (t.kind != kind) {
            continue;
        }
        std::string digits;
        std::ranges::copy_if(t.name, std::back_inserter(digits),
                             [](char ch) { return ch >= '0' && ch <= '9'; });
        n = std::max(n, digits.empty() ? 0L
                                       : std::strtol(digits.substr(0, 15).c_str(), nullptr, 10)); // parseInt
    }
    TimelineTrack t;
    t.name = prefix + std::to_string(n + 1);
    t.id = t.name + "-" + std::string(id_suffix);
    t.kind = kind;
    return t;
}

std::vector<std::pair<std::string, double>>
group_move_starts(std::span<const std::pair<std::string, double>> originals, double delta) {
    if (originals.empty()) {
        return {};
    }
    const double min_start = std::ranges::min(originals | std::views::values);
    const double d = std::max(-min_start, or_zero(delta));
    std::vector<std::pair<std::string, double>> out;
    for (const auto& [id, start] : originals) {
        out.emplace_back(id, js_fixed3(start + d));
    }
    return out;
}

} // namespace gc::core
