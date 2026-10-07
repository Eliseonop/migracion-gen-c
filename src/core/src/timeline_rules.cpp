#include "gc/core/timeline_rules.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_map>

#include "py.hpp"

namespace gc::core {

using detail::py_repr;
using detail::py_round;

// --- Familias de clip ↔ pista ---------------------------------------------------------

std::optional<std::string_view> track_kind_for_clip(std::string_view clip_kind) {
    static constexpr std::array<std::pair<std::string_view, std::string_view>, 7> kMap{{
        {"video", "video"},
        {"image", "video"},
        {"shape", "video"},
        {"motion", "video"},     // motion graphic: overlay con alfa en pista de vídeo
        {"adjustment", "video"}, // capa de ajuste: filtra lo que tiene debajo
        {"audio", "audio"},
        {"text", "text"},
    }};
    for (const auto& [clip, track] : kMap) {
        if (clip == clip_kind) {
            return track;
        }
    }
    return std::nullopt;
}

Result<bool> clip_fits_track(std::string_view clip_kind, std::string_view track_kind) {
    const auto kind = track_kind_for_clip(clip_kind);
    if (!kind) {
        return fail(ErrorCode::InvalidArgument, "kind de clip inválido: " + std::string(clip_kind));
    }
    return *kind == track_kind;
}

// --- Velocidad y duración ------------------------------------------------------------------

double clip_speed(const TimelineClip& clip) {
    if (clip.kind == "text" || clip.kind == "image" || clip.kind == "adjustment") {
        return 1.0;
    }
    const double s = clip.speed;
    if (s <= 0.0 || std::isnan(s)) {
        return 1.0;
    }
    return std::min(kSpeedMax, std::max(kSpeedMin, s));
}

double clip_source_duration(const TimelineClip& clip) {
    return std::max(0.0, py_round(clip.out_point - clip.in_point, 3));
}

double clip_timeline_duration(const TimelineClip& clip) {
    return std::max(0.0, py_round(clip_source_duration(clip) / clip_speed(clip), 3));
}

double clip_span_duration(const TimelineClip& clip) {
    const double span = std::max(0.0, clip.out_point - clip.in_point);
    if (clip.kind == "text" || clip.kind == "image" || clip.kind == "shape") {
        return span;
    }
    const double sp = clip.speed == 0.0 ? 1.0 : clip.speed; // `c.speed or 1.0`
    return sp > 0.0 ? span / sp : span;
}

double clip_end(const TimelineClip& clip) { return clip.start + clip_timeline_duration(clip); }

// --- Pila de capas -----------------------------------------------------------------------

bool is_stack_track(const TimelineTrack& track) { return track.kind == "video" || track.kind == "text"; }

std::unordered_map<std::string, std::size_t> stack_layers(const std::vector<TimelineTrack>& tracks) {
    std::unordered_map<std::string, std::size_t> out;
    std::size_t layer = 0;
    for (const TimelineTrack& t : tracks) {
        if (is_stack_track(t)) {
            out[t.id] = layer++;
        }
    }
    return out;
}

std::set<std::string> buried_text_track_ids(const std::vector<TimelineTrack>& tracks) {
    std::set<std::string> buried;
    bool video_above = false;
    for (auto it = tracks.rbegin(); it != tracks.rend(); ++it) {
        if (!is_stack_track(*it)) {
            continue;
        }
        if (it->kind == "video") {
            video_above = true;
        } else if (video_above) {
            buried.insert(it->id);
        }
    }
    return buried;
}

std::vector<std::size_t> display_order(const std::vector<TimelineTrack>& tracks) {
    std::vector<std::size_t> out;
    out.reserve(tracks.size());
    for (std::size_t i = tracks.size(); i-- > 0;) {
        if (is_stack_track(tracks[i])) {
            out.push_back(i);
        }
    }
    for (std::size_t i = 0; i < tracks.size(); ++i) {
        if (!is_stack_track(tracks[i])) {
            out.push_back(i);
        }
    }
    return out;
}

std::size_t default_track_index(const std::vector<TimelineTrack>& tracks, std::string_view kind) {
    if (kind == "video") {
        std::size_t at = 0;
        for (std::size_t i = 0; i < tracks.size(); ++i) {
            if (tracks[i].kind == "video") {
                at = i + 1;
            }
        }
        return at;
    }
    return tracks.size();
}

void insert_track(std::vector<TimelineTrack>& tracks, TimelineTrack track, std::optional<std::size_t> index) {
    const std::size_t at = index ? std::min(tracks.size(), *index) : default_track_index(tracks, track.kind);
    tracks.insert(tracks.begin() + static_cast<std::ptrdiff_t>(at), std::move(track));
}

Result<std::vector<TimelineTrack>> reorder_tracks(const std::vector<TimelineTrack>& tracks,
                                                  std::string_view track_id, std::string_view target_id,
                                                  std::string_view place) {
    if (place != "above" && place != "below") {
        return fail(ErrorCode::InvalidArgument, "place debe ser 'above' o 'below'");
    }
    const std::vector<std::size_t> rows = display_order(tracks);
    auto find = [&](std::string_view id) {
        return std::ranges::find_if(rows, [&](std::size_t i) { return tracks[i].id == id; });
    };
    const auto moving = find(track_id);
    const auto target = find(target_id);
    if (moving == rows.end() || target == rows.end()) {
        return fail(ErrorCode::InvalidArgument,
                    "pista desconocida: " + std::string(moving == rows.end() ? track_id : target_id));
    }
    if (*moving == *target) {
        return tracks;
    }
    if (is_stack_track(tracks[*moving]) != is_stack_track(tracks[*target])) {
        return fail(ErrorCode::InvalidArgument,
                    "una pista de audio solo se reordena entre pistas de audio y una de vídeo/texto dentro de la "
                    "pila de vídeo/texto");
    }
    std::vector<std::size_t> rest;
    for (const std::size_t i : rows) {
        if (i != *moving) {
            rest.push_back(i);
        }
    }
    const auto pos = std::ranges::find(rest, *target) - rest.begin() + (place == "below" ? 1 : 0);
    rest.insert(rest.begin() + pos, *moving);

    std::vector<TimelineTrack> out;
    out.reserve(tracks.size());
    for (auto it = rest.rbegin(); it != rest.rend(); ++it) { // la pila vuelve a su orden (fondo primero)
        if (is_stack_track(tracks[*it])) {
            out.push_back(tracks[*it]);
        }
    }
    for (const std::size_t i : rest) {
        if (!is_stack_track(tracks[i])) {
            out.push_back(tracks[i]);
        }
    }
    return out;
}

// --- Validación ------------------------------------------------------------------------

std::vector<std::pair<std::string, std::string>> track_overlaps(const Timeline& timeline,
                                                               std::string_view track_id) {
    std::vector<const TimelineClip*> row;
    for (const auto& c : timeline.clips) {
        if (c->track_id == track_id) {
            row.push_back(&*c);
        }
    }
    std::ranges::stable_sort(row, {}, [](const TimelineClip* c) { return c->start; });
    std::vector<std::pair<std::string, std::string>> out;
    for (std::size_t i = 0; i + 1 < row.size(); ++i) {
        const TimelineClip& a = *row[i];
        if (a.start + clip_span_duration(a) > row[i + 1]->start + 1e-6) {
            out.emplace_back(a.id, row[i + 1]->id);
        }
    }
    return out;
}

std::vector<std::string> track_overlap_warnings(const Timeline& timeline, std::string_view track_id) {
    std::vector<std::string> out;
    for (const auto& [a, b] : track_overlaps(timeline, track_id)) {
        out.push_back("solape en " + std::string(track_id) + ": " + a + " ↔ " + b);
    }
    return out;
}

Result<std::vector<std::string>> validate_timeline(const Timeline& timeline) {
    std::unordered_map<std::string, std::string> kind_of;
    for (const auto& t : timeline.tracks) {
        kind_of[t.id] = t.kind;
    }
    std::vector<std::string> issues;
    for (const auto& cow : timeline.clips) {
        const TimelineClip& c = *cow;
        const auto track = kind_of.find(c.track_id);
        if (track == kind_of.end()) {
            issues.push_back("clip " + c.id + ": pista inexistente " + c.track_id);
        } else {
            const auto fits = clip_fits_track(c.kind, track->second);
            if (!fits) {
                return std::unexpected(fits.error());
            }
            if (!*fits) {
                issues.push_back("clip " + c.id + ": kind '" + c.kind + "' no cuadra con la pista '" + track->second +
                                 "'");
            }
        }
        if (c.start < 0) {
            issues.push_back("clip " + c.id + ": start negativo (" + py_repr(c.start) + ")");
        }
        if (c.in_point < 0) {
            issues.push_back("clip " + c.id + ": in_point negativo (" + py_repr(c.in_point) + ")");
        }
        if (c.out_point <= c.in_point) {
            issues.push_back("clip " + c.id + ": rango de fuente vacío o invertido [" + py_repr(c.in_point) + ", " +
                             py_repr(c.out_point) + "]");
        }
        if (c.source_duration != 0.0 && c.out_point > c.source_duration + 1e-6) {
            issues.push_back("clip " + c.id + ": out_point " + py_repr(c.out_point) +
                             " excede la duración de fuente " + py_repr(c.source_duration));
        }
    }
    return issues;
}

} // namespace gc::core
