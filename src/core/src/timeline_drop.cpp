#include "gc/core/timeline_drop.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <set>

#include "gc/core/timeline_rules.hpp"
#include "gc/core/timeline_view.hpp"

namespace gc::core {

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();

double or_zero(double x) {
    return std::isnan(x) ? 0.0 : x;
}

// +max(0, v || 0).toFixed(3) de dropIntent.js.
double r3(double v) {
    return js_fixed3(std::max(0.0, or_zero(v)));
}

bool excluded(std::span<const std::string> exclude, std::string_view id) {
    return std::ranges::find(exclude, id) != exclude.end();
}

// Number(x) de JS para lo que llega en un JSON.
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
        const std::string s = v.get<std::string>();
        const auto first = s.find_first_not_of(" \t\n\r");
        if (first == std::string::npos) {
            return 0;
        }
        char* end = nullptr;
        const double n = std::strtod(s.c_str() + first, &end);
        return end != s.c_str() + first &&
                       std::string_view(end).find_first_not_of(" \t\n\r") == std::string_view::npos
                   ? n
                   : std::numeric_limits<double>::quiet_NaN();
    }
    return std::numeric_limits<double>::quiet_NaN();
}

double source_to_timeline(const TimelineClip& c, double src) {
    const double sp = ui_clip_speed(c);
    return c.reverse ? c.start + (c.out_point - src) / sp : c.start + (src - c.in_point) / sp;
}

std::vector<double> edges(const AlignClip& c, AlignEdge which) {
    if (which == AlignEdge::Start) {
        return {c.start};
    }
    if (which == AlignEdge::End) {
        return {c.end};
    }
    return {c.start, c.end};
}

std::vector<double> unique_times(const std::vector<double>& times) {
    std::set<double> sorted;
    for (const double t : times) {
        const double r = js_fixed3(t);
        if (std::isfinite(r)) {
            sorted.insert(r);
        }
    }
    std::vector<double> out;
    for (const double t : sorted) {
        if (out.empty() || t - out.back() > 0.008) {
            out.push_back(t);
        }
    }
    return out;
}

bool is_stack_kind(std::string_view kind) {
    return kind == "video" || kind == "text";
}

const TimelineTrack* find_track(const std::vector<TimelineTrack>& tracks, std::string_view id) {
    const auto it = std::ranges::find(tracks, id, &TimelineTrack::id);
    return it == tracks.end() ? nullptr : &*it;
}

// stickToNeighbors: el inicio pegado al vecino si el hueco que queda es menor que `snap_sec`.
double stick_to_neighbors(const ClipList& clips, std::string_view track_id, double start, double span,
                          std::span<const std::string> exclude, double snap_sec) {
    if (!(snap_sec > 0)) {
        return start;
    }
    double prev_end = -kInf;
    double next_start = kInf;
    for (const auto& c : clips) {
        if (c->track_id != track_id || excluded(exclude, c->id)) {
            continue;
        }
        const double ce = ui_clip_end(*c);
        if (ce <= start + 1e-9) {
            prev_end = std::max(prev_end, ce);
        } else if (c->start >= start + span - 1e-9) {
            next_start = std::min(next_start, c->start);
        }
    }
    if (start - prev_end <= snap_sec) {
        return prev_end;
    }
    if (next_start - (start + span) <= snap_sec && next_start - span >= std::max(0.0, prev_end)) {
        return next_start - span;
    }
    return start;
}

double line_y_for(std::span<const TrackRow> geo, const TrackSlot& slot) {
    const auto it = std::ranges::find_if(geo, [&](const TrackRow& r) { return slot.target_id == r.id; });
    if (it == geo.end()) {
        return 0;
    }
    return slot.below ? it->bottom : it->top;
}

} // namespace

double align_threshold_sec(double pps, double px) {
    return std::isfinite(pps) && pps > 0 ? px / pps : 0.12;
}

AlignClip as_align_clip(const TimelineClip& clip) {
    return {clip.id, clip.track_id, or_zero(clip.start), ui_clip_end(clip)};
}

std::vector<AlignClip> align_others(const ClipList& clips, std::span<const std::string> exclude) {
    std::vector<AlignClip> out;
    for (const auto& c : clips) {
        if (!excluded(exclude, c->id)) {
            out.push_back(as_align_clip(*c));
        }
    }
    return out;
}

std::vector<double> clip_beat_times(const TimelineClip& clip) {
    if (!clip.beats || !clip.beats->is_object()) {
        return {};
    }
    const Json& b = *clip.beats;
    const Json times = b.value("times", Json());
    if (!times.is_array() || times.empty()) {
        return {};
    }
    const double e = js_number(b.value("every", Json::object()));
    const int every = e == 2 || e == 4 ? static_cast<int>(e) : 1;
    std::vector<double> out;
    std::size_t i = 0;
    for (const Json& raw : times) {
        const double src = js_number(raw);
        if (!std::isfinite(src)) {
            continue;
        }
        const bool keep = i++ % static_cast<std::size_t>(every) == 0;
        if (keep && src >= clip.in_point - 1e-6 && src <= clip.out_point + 1e-6) {
            out.push_back(js_fixed3(source_to_timeline(clip, src)));
        }
    }
    std::ranges::stable_sort(out);
    return out;
}

std::vector<AlignClip> snap_targets(const std::vector<Json>& markers, const ClipList& clips,
                                    std::span<const std::string> exclude) {
    std::vector<double> marks;
    for (const Json& m : markers) {
        const double t = m.is_object() && m.contains("t") ? js_number(m.at("t")) : 0;
        marks.push_back(js_fixed3(std::max(0.0, or_zero(t))));
    }
    std::ranges::stable_sort(marks);
    std::vector<AlignClip> out;
    for (const double t : marks) {
        out.push_back({"mk", "__markers__", t, t});
    }
    for (const auto& c : clips) {
        if (excluded(exclude, c->id) || c->disabled) {
            continue;
        }
        for (const double t : clip_beat_times(*c)) {
            out.push_back({"bt:" + c->id, "__beats__", t, t});
        }
    }
    return out;
}

std::vector<double> timeline_align_hits(std::span<const AlignClip> moving, std::span<const AlignClip> others,
                                        double threshold, AlignEdge which) {
    if (!std::isfinite(threshold) || threshold < 0) {
        return {};
    }
    std::vector<double> times;
    for (const AlignClip& m : moving) {
        for (const AlignClip& o : others) {
            if (o.track_id == m.track_id || o.id == m.id) {
                continue;
            }
            for (const double me : edges(m, which)) {
                for (const double oe : {o.start, o.end}) {
                    if (std::isfinite(me) && std::isfinite(oe) && std::abs(me - oe) <= threshold + 1e-9) {
                        times.push_back(oe);
                    }
                }
            }
        }
    }
    return unique_times(times);
}

std::optional<double> nearest_align_time(double time, std::string_view track_id,
                                         std::span<const AlignClip> others, double threshold) {
    if (!std::isfinite(time) || !std::isfinite(threshold) || threshold < 0) {
        return std::nullopt;
    }
    std::optional<double> best;
    double best_d = threshold;
    for (const AlignClip& o : others) {
        if (o.track_id == track_id) {
            continue;
        }
        for (const double oe : {o.start, o.end}) {
            const double d = std::abs(time - oe);
            if (std::isfinite(oe) && d <= best_d + 1e-9) {
                best_d = d;
                best = oe;
            }
        }
    }
    return best;
}

SnapResult snap_group_delta(std::span<const AlignClip> moving, std::span<const AlignClip> others,
                            double threshold) {
    if (!std::isfinite(threshold) || threshold < 0) {
        return {};
    }
    double best_abs = threshold;
    std::optional<double> best;
    for (const AlignClip& m : moving) {
        for (const AlignClip& o : others) {
            if (o.track_id == m.track_id || o.id == m.id) {
                continue;
            }
            for (const double me : {m.start, m.end}) {
                for (const double oe : {o.start, o.end}) {
                    if (!std::isfinite(me) || !std::isfinite(oe)) {
                        continue;
                    }
                    const double delta = oe - me;
                    const double a = std::abs(delta);
                    if (a <= best_abs + 1e-9 && (!best || a < best_abs - 1e-9)) {
                        best_abs = a;
                        best = delta;
                    }
                }
            }
        }
    }
    if (!best) {
        return {};
    }
    std::vector<AlignClip> moved(moving.begin(), moving.end());
    for (AlignClip& m : moved) {
        m.start += *best;
        m.end += *best;
    }
    return {*best, timeline_align_hits(moved, others, threshold)};
}

SnapMove snap_clip_move(const TimelineClip& orig, double delta, std::span<const AlignClip> others,
                        double threshold, std::string_view track_id) {
    const double s0 = or_zero(orig.start);
    const double dur = std::max(0.0, ui_clip_end(orig) - s0);
    const double ns = std::max(0.0, s0 + or_zero(delta));
    const std::string tid(track_id.empty() ? std::string_view(orig.track_id) : track_id);
    const AlignClip proposed{orig.id, tid, ns, ns + dur};
    const SnapResult snap = snap_group_delta(std::span(&proposed, 1), others, threshold);
    const double start = std::max(0.0, ns + snap.delta);
    const AlignClip final_clip{orig.id, tid, start, start + dur};
    return {js_fixed3(start), timeline_align_hits(std::span(&final_clip, 1), others, threshold)};
}

SnapTrim snap_clip_trim(const TimelineClip& orig, TrimEdge edge, double delta,
                        std::span<const AlignClip> others, double threshold) {
    const auto aligned = [&](const ClipPatch& p) {
        TimelineClip c = orig;
        p.apply(c);
        return as_align_clip(c);
    };
    const AlignEdge which = edge == TrimEdge::Left ? AlignEdge::Start : AlignEdge::End;
    ClipPatch patch = trim_clip_patch(orig, edge, delta);
    const AlignClip next = aligned(patch);
    const double at = which == AlignEdge::Start ? next.start : next.end;
    double used = or_zero(delta);
    if (const auto hit = nearest_align_time(at, next.track_id, others, threshold)) {
        used += *hit - at;
        patch = trim_clip_patch(orig, edge, used);
    }
    const AlignClip final_clip = aligned(patch);
    return {patch, timeline_align_hits(std::span(&final_clip, 1), others, threshold, which), used};
}

SnapResult snap_clip_group(std::span<const TimelineClip* const> origs, double delta,
                           std::span<const AlignClip> others, double threshold) {
    if (origs.empty()) {
        return {};
    }
    double min_start = kInf;
    for (const TimelineClip* o : origs) {
        min_start = std::min(min_start, o->start);
    }
    const auto shifted = [&](double d) {
        std::vector<AlignClip> out;
        for (const TimelineClip* o : origs) {
            TimelineClip c = *o;
            c.start = o->start + d;
            out.push_back(as_align_clip(c));
        }
        return out;
    };
    const double raw = std::max(-min_start, or_zero(delta));
    const SnapResult snap = snap_group_delta(shifted(raw), others, threshold);
    const double d = std::max(-min_start, raw + snap.delta);
    return {d, timeline_align_hits(shifted(d), others, threshold)};
}

double clamp_group_delta(const ClipList& clips, std::span<const TimelineClip* const> origs, double delta,
                         const std::vector<TimelineTrack>& tracks) {
    if (origs.empty()) {
        return 0;
    }
    const auto moving = [&](std::string_view id) {
        return std::ranges::any_of(origs, [&](const TimelineClip* o) { return o->id == id; });
    };
    const auto magnetic = [&](std::string_view id) {
        return std::ranges::any_of(tracks, [&](const TimelineTrack& t) { return t.magnetic && t.id == id; });
    };
    double lo = kInf;
    for (const TimelineClip* o : origs) {
        lo = std::min(lo, std::max(0.0, or_zero(o->start)));
    }
    lo = -lo;
    double hi = kInf;
    for (const TimelineClip* o : origs) {
        if (magnetic(o->track_id)) {
            continue;
        }
        const double s = or_zero(o->start);
        const double e = ui_clip_end(*o);
        double left = 0;
        double right = kInf;
        bool clash = false;
        for (const auto& c : clips) {
            if (moving(c->id) || c->track_id != o->track_id) {
                continue;
            }
            const double ce = ui_clip_end(*c);
            if (ce <= s + 1e-6) {
                left = std::max(left, ce);
            } else if (c->start >= e - 1e-6) {
                right = std::min(right, c->start);
            } else {
                clash = true;
            }
        }
        if (!clash) {
            lo = std::max(lo, left - s);
            hi = std::min(hi, right - e);
        }
    }
    return lo > hi ? 0 : std::min(hi, std::max(lo, or_zero(delta)));
}

const TimelineClip* clip_at_time(const ClipList& clips, std::string_view track_id, double t,
                                 std::span<const std::string> exclude) {
    const TimelineClip* hit = nullptr;
    for (const auto& c : clips) {
        if (c->track_id == track_id && !excluded(exclude, c->id) && t >= c->start && t < ui_clip_end(*c)) {
            hit = &*c;
        }
    }
    return hit;
}

const TimelineClip* clip_colliding_with(const ClipList& clips, std::string_view track_id, double start,
                                        double duration, std::span<const std::string> exclude) {
    const double end = start + std::max(0.0, or_zero(duration));
    const TimelineClip* hit = nullptr;
    for (const auto& c : clips) {
        if (c->track_id != track_id || excluded(exclude, c->id)) {
            continue;
        }
        const double ce = ui_clip_end(*c);
        if (ce > c->start && start < ce && c->start < end) {
            hit = &*c;
        }
    }
    return hit;
}

Zone zone_at(const std::vector<TimelineTrack>& tracks, std::span<const TrackRow> geo, std::string_view kind,
             double y, double boundary_px, std::string_view origin_id, bool origin_alone) {
    struct Row {
        const TrackRow* geo;
        const TimelineTrack* track;
    };
    std::vector<Row> rows;
    for (const TrackRow& g : geo) {
        if (const TimelineTrack* t = find_track(tracks, g.id)) {
            rows.push_back({&g, t});
        }
    }
    const bool stack = is_stack_kind(kind);
    std::vector<Row> group;
    std::ranges::copy_if(rows, std::back_inserter(group),
                         [&](const Row& r) { return is_stack_track(*r.track) == stack; });
    if (group.empty()) {
        const double line =
            stack ? (rows.empty() ? 0 : rows.front().geo->top) : (rows.empty() ? 0 : rows.back().geo->bottom);
        return {std::nullopt, TrackSlot{std::nullopt, !stack}, line};
    }
    const auto slot = [](const Row& r, bool below) {
        return Zone{std::nullopt, TrackSlot{r.geo->id, below}, below ? r.geo->bottom : r.geo->top};
    };
    const double Y = or_zero(y);
    const auto row =
        std::ranges::find_if(group, [&](const Row& r) { return Y >= r.geo->top && Y < r.geo->bottom; });
    Zone zone;
    if (row == group.end()) {
        zone = Y < group.front().geo->top ? slot(group.front(), false) : slot(group.back(), true);
    } else {
        const double h = row->geo->bottom - row->geo->top;
        const double band = std::min(boundary_px, h / 4);
        if (Y < row->geo->top + band) {
            zone = slot(*row, false);
        } else if (Y >= row->geo->bottom - band) {
            zone = slot(*row, true);
        } else if (row->track->kind == kind && !row->track->locked) {
            zone = {row->geo->id, std::nullopt, 0};
        } else {
            zone = slot(*row, Y >= row->geo->top + h / 2);
        }
    }
    if (zone.slot && origin_alone && !origin_id.empty()) {
        const auto index_of = [&](const std::optional<std::string>& id) {
            const auto it = std::ranges::find_if(rows, [&](const Row& r) { return id == r.geo->id; });
            return it == rows.end() ? -1L : static_cast<long>(it - rows.begin());
        };
        const long b = index_of(zone.slot->target_id) + (zone.slot->below ? 1 : 0);
        const long o = index_of(std::string(origin_id));
        if (o >= 0 && (b == o || b == o + 1)) {
            return {std::string(origin_id), std::nullopt, 0};
        }
    }
    return zone;
}

std::optional<double> place_on_track(const ClipList& clips, std::string_view track_id, double desired,
                                     double duration, std::span<const std::string> exclude, double stick_sec,
                                     double snap_sec) {
    const double span = std::max(0.001, or_zero(duration));
    const double want = r3(desired);
    if (!clip_colliding_with(clips, track_id, want, span, exclude)) {
        return r3(stick_to_neighbors(clips, track_id, want, span, exclude, snap_sec));
    }
    const double cand = clamp_start_no_overlap(clips, track_id, want, span, exclude);
    if (std::abs(cand - want) <= stick_sec + 1e-9) {
        return r3(cand);
    }
    return std::nullopt;
}

std::optional<LiftTarget> lift_target(const std::vector<TimelineTrack>& tracks, const ClipList& clips,
                                      std::string_view from_id, double start, double duration,
                                      std::span<const std::string> exclude) {
    const auto rows = display_tracks(tracks);
    const auto it = std::ranges::find_if(rows, [&](const TimelineTrack* t) { return t->id == from_id; });
    if (it == rows.end()) {
        return std::nullopt;
    }
    const TimelineTrack& from = **it;
    const bool up = is_stack_track(from);
    const auto i = it - rows.begin();
    const auto j = up ? i - 1 : i + 1;
    if (j >= 0 && j < static_cast<long>(rows.size())) {
        const TimelineTrack& next = *rows[static_cast<std::size_t>(j)];
        if (next.kind == from.kind && !next.locked && !next.hidden &&
            !clip_colliding_with(clips, next.id, start, duration, exclude)) {
            return LiftTarget{next.id, std::nullopt};
        }
    }
    return LiftTarget{std::nullopt, TrackSlot{from.id, !up}};
}

MagneticCut magnetic_cut(const ClipList& clips, std::string_view track_id, double time,
                         std::span<const std::string> exclude) {
    std::vector<const TimelineClip*> row;
    for (const auto& c : clips) {
        if (c->track_id == track_id && !excluded(exclude, c->id)) {
            row.push_back(&*c);
        }
    }
    std::ranges::stable_sort(row, {}, &TimelineClip::start);
    const double t = or_zero(time);
    for (std::size_t i = 0; i < row.size(); ++i) {
        if (t < row[i]->start + ui_clip_duration(*row[i]) / 2) {
            return {js_fixed3(row[i]->start), i};
        }
    }
    return {row.empty() ? 0.0 : js_fixed3(ui_clip_end(*row.back())), row.size()};
}

std::optional<Drop> drop_target(const std::vector<TimelineTrack>& tracks, const ClipList& clips,
                                std::span<const TrackRow> geo, const DropQuery& q) {
    const Zone zone = zone_at(tracks, geo, q.kind, q.y, kBoundaryPx, q.origin_id, q.origin_alone);
    const double start = r3(q.time);
    if (zone.slot) {
        return Drop{.slot = zone.slot, .line_y = zone.line_y, .start = start};
    }
    const TimelineTrack* track = find_track(tracks, *zone.track_id);
    if (!track) {
        return std::nullopt;
    }
    const double span = std::max(0.001, or_zero(q.duration));
    const double at = q.pointer_time && std::isfinite(*q.pointer_time) ? *q.pointer_time : start;
    if (q.replace) {
        if (const TimelineClip* hit = clip_at_time(clips, track->id, at, q.exclude)) {
            return Drop{.track_id = track->id, .start = r3(hit->start), .replace_id = hit->id};
        }
    }
    if (track->magnetic) {
        return Drop{.track_id = track->id,
                    .start = magnetic_cut(clips, track->id, at, q.exclude).start,
                    .insert = true};
    }
    const double stick_sec = kStickPx / std::max(1.0, or_zero(q.pps) == 0 ? 60 : q.pps);
    if (const auto placed = place_on_track(clips, track->id, start, span, q.exclude, stick_sec, q.snap_sec)) {
        return Drop{.track_id = track->id, .start = *placed};
    }
    const auto lift = lift_target(tracks, clips, track->id, start, span, q.exclude);
    if (lift->track_id) {
        return Drop{.track_id = lift->track_id, .start = start, .lifted = true};
    }
    return Drop{.slot = lift->slot, .line_y = line_y_for(geo, *lift->slot), .start = start, .lifted = true};
}

std::size_t new_track_index(const std::vector<TimelineTrack>& tracks, std::string_view kind, bool below) {
    const bool stack = is_stack_kind(kind);
    std::vector<std::size_t> idx;
    for (std::size_t i = 0; i < tracks.size(); ++i) {
        if (stack ? is_stack_track(tracks[i]) : tracks[i].kind == kind) {
            idx.push_back(i);
        }
    }
    if (idx.empty()) {
        return tracks.size();
    }
    const bool to_end = stack ? !below : below;
    return to_end ? idx.back() + 1 : idx.front();
}

long slot_boundary(const std::vector<TimelineTrack>& tracks, const TrackSlot& slot) {
    const auto rows = display_tracks(tracks);
    const auto it =
        std::ranges::find_if(rows, [&](const TimelineTrack* t) { return slot.target_id == t->id; });
    if (it == rows.end()) {
        return -1;
    }
    return static_cast<long>(it - rows.begin()) + (slot.below ? 1 : 0);
}

std::size_t slot_index(const std::vector<TimelineTrack>& tracks, const TrackSlot& slot) {
    const auto it =
        std::ranges::find_if(tracks, [&](const TimelineTrack& t) { return slot.target_id == t.id; });
    if (it == tracks.end()) {
        return tracks.size();
    }
    const auto i = static_cast<std::size_t>(it - tracks.begin());
    const bool on_top = is_stack_track(*it) ? !slot.below : slot.below;
    return on_top ? i + 1 : i;
}

} // namespace gc::core
