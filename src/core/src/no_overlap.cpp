#include "gc/core/no_overlap.hpp"

#include <algorithm>
#include <cmath>
#include <tuple>
#include <unordered_map>

#include "gc/core/timeline_rules.hpp"
#include "py.hpp"

namespace gc::core {

using detail::py_repr;
using detail::py_round;

namespace {

double overlap(const TimelineClip& a, const TimelineClip& b) {
    return std::min(clip_end(a), clip_end(b)) - std::max(a.start, b.start);
}

bool same(double a, double b) { return std::abs(a - b) < 1e-6; }

bool is_free(const Timeline& tl, std::string_view track_id, double start, double dur, std::string_view exclude) {
    const double end = start + std::max(0.0, dur);
    for (const auto& c : tl.clips) {
        if (c->track_id != track_id || c->id == exclude) {
            continue;
        }
        if (start < clip_end(*c) && c->start < end && clip_end(*c) > c->start) {
            return false;
        }
    }
    return true;
}

std::string_view prefix_for(std::string_view kind) {
    if (kind == "video") {
        return "V";
    }
    if (kind == "audio") {
        return "A";
    }
    if (kind == "text") {
        return "T";
    }
    return "K";
}

// Número de la pista según su nombre: todos sus dígitos juntos ("V12" → 12, "Voz" → 0).
long long name_number(const std::string& name) {
    std::string digits;
    for (const char ch : name) {
        if (ch >= '0' && ch <= '9') {
            digits.push_back(ch);
        }
    }
    if (digits.empty()) {
        return 0;
    }
    long long n = 0;
    for (const char ch : digits) {
        if (n > 100'000'000'000'000LL) {
            break; // nombres absurdos: basta con que sea grande
        }
        n = n * 10 + (ch - '0');
    }
    return n;
}

TimelineTrack new_track(const std::vector<TimelineTrack>& tracks, const std::string& kind,
                        const std::optional<Json>& style, IdSource& ids) {
    const std::string_view prefix = prefix_for(kind);
    long long max_n = 0;
    bool any = false;
    for (const auto& t : tracks) {
        if (t.kind == kind) {
            max_n = any ? std::max(max_n, name_number(t.name)) : name_number(t.name);
            any = true;
        }
    }
    const long long n = (any ? max_n : 0) + 1;
    TimelineTrack t;
    t.id = std::string(prefix) + std::to_string(n) + "-" + ids.hex32().substr(0, 6);
    t.kind = kind;
    t.name = std::string(prefix) + std::to_string(n);
    if (style && !style->empty()) { // `dict(style) if style else None`
        t.style = *style;
    }
    return t;
}

struct Lift {
    std::string track_id;
    bool created = false;
};

// Pista a la que sube el clip `clip_index` (las pistas se crean en `tl`).
std::optional<Lift> lift(Timeline& tl, std::size_t clip_index, IdSource& ids) {
    const TimelineClip& clip = *tl.clips[clip_index];
    const std::vector<std::size_t> rows = display_order(tl.tracks);
    const auto it = std::ranges::find_if(rows, [&](std::size_t i) { return tl.tracks[i].id == clip.track_id; });
    if (it == rows.end()) {
        return std::nullopt;
    }
    const auto i = static_cast<std::ptrdiff_t>(it - rows.begin());
    const TimelineTrack& src = tl.tracks[*it];
    // La pila crece hacia arriba (delante); el audio, hacia abajo.
    const std::ptrdiff_t j = is_stack_track(src) ? i - 1 : i + 1;
    const double dur = clip_timeline_duration(clip);
    if (j >= 0 && j < static_cast<std::ptrdiff_t>(rows.size())) {
        const TimelineTrack& next = tl.tracks[rows[static_cast<std::size_t>(j)]];
        if (next.kind == src.kind && !next.locked && !next.hidden &&
            is_free(tl, next.id, clip.start, dur, clip.id)) {
            return Lift{next.id, false};
        }
    }
    const std::optional<Json> style = src.kind == "text" ? src.style : std::nullopt;
    TimelineTrack nt = new_track(tl.tracks, src.kind, style, ids);
    const std::string new_id = nt.id;
    const auto at = static_cast<std::size_t>(
        std::ranges::find_if(tl.tracks, [&](const TimelineTrack& t) { return t.id == src.id; }) - tl.tracks.begin());
    // En la pila "encima" es DESPUÉS en el array; en el audio, "debajo" también.
    insert_track(tl.tracks, std::move(nt), at + 1);
    return Lift{new_id, true};
}

} // namespace

std::unordered_set<std::string> touched_clip_ids(const Timeline& before, const Timeline& after) {
    std::unordered_map<std::string, const Cow<TimelineClip>*> old;
    for (const auto& c : before.clips) {
        old[c->id] = &c; // con ids repetidos gana el último, como el dict de Python
    }
    std::unordered_set<std::string> out;
    for (const auto& c : after.clips) {
        const auto it = old.find(c->id);
        if (it == old.end()) {
            out.insert(c->id);
            continue;
        }
        if (c.same_object(*it->second)) {
            continue; // el mismo objeto compartido: no ha cambiado nada
        }
        const TimelineClip& b = **it->second;
        if (b.track_id != c->track_id || !same(b.start, c->start) || !same(clip_end(b), clip_end(*c))) {
            out.insert(c->id);
        }
    }
    return out;
}

bool pack_magnetic(Timeline& tl, std::string_view track_id, const std::unordered_set<std::string>& priority) {
    std::vector<std::size_t> row;
    for (std::size_t i = 0; i < tl.clips.size(); ++i) {
        if (tl.clips[i]->track_id == track_id) {
            row.push_back(i);
        }
    }
    auto key = [&](std::size_t i) {
        const TimelineClip& c = *tl.clips[i];
        return std::tuple{py_round(c.start, 4), priority.contains(c.id) ? 0 : 1, i};
    };
    std::ranges::sort(row, {}, key);
    double cursor = 0.0;
    bool changed = false;
    for (const std::size_t i : row) {
        const double s = py_round(cursor, 3);
        if (!same(s, tl.clips[i]->start)) {
            tl.clips[i].mut().start = s;
            changed = true;
        }
        cursor += clip_timeline_duration(*tl.clips[i]);
    }
    return changed;
}

std::optional<Settled> settle_timeline(const Timeline& before, const Timeline& after, IdSource& ids, double eps) {
    const auto touched = touched_clip_ids(before, after);
    std::vector<std::string> magnetic; // en el orden de las pistas
    std::unordered_set<std::string> magnetic_set;
    for (const auto& t : after.tracks) {
        if (t.magnetic && magnetic_set.insert(t.id).second) {
            magnetic.push_back(t.id);
        }
    }
    if (touched.empty() && magnetic.empty()) {
        return std::nullopt;
    }
    Settled out{after, {}, {}};
    Timeline& tl = out.timeline;
    std::unordered_map<std::string, const TimelineClip*> old;
    for (const auto& c : before.clips) {
        old[c->id] = &*c;
    }

    for (const auto& tid : magnetic) {
        if (pack_magnetic(tl, tid, touched)) {
            out.notes.push_back("pista magnética " + tid + ": empaquetada sin huecos");
        }
    }

    std::vector<std::size_t> order;
    for (std::size_t i = 0; i < tl.clips.size(); ++i) {
        if (touched.contains(tl.clips[i]->id) && !magnetic_set.contains(tl.clips[i]->track_id)) {
            order.push_back(i);
        }
    }
    std::ranges::stable_sort(order, {}, [&](std::size_t i) { return tl.clips[i]->start; });

    for (const std::size_t ci : order) {
        std::vector<std::size_t> hits;
        for (std::size_t k = 0; k < tl.clips.size(); ++k) {
            const TimelineClip& x = *tl.clips[k];
            const TimelineClip& c = *tl.clips[ci];
            if (x.id != c.id && x.track_id == c.track_id && clip_end(x) > x.start && overlap(c, x) > eps) {
                hits.push_back(k);
            }
        }
        if (hits.empty()) {
            continue;
        }
        const TimelineClip& c = *tl.clips[ci];
        const auto prev = old.find(c.id);
        const TimelineClip* b = prev == old.end() ? nullptr : prev->second;
        const bool grew =
            b != nullptr && b->track_id == c.track_id && same(b->start, c.start) && clip_end(c) > clip_end(*b);
        const bool behind = b != nullptr && std::ranges::all_of(hits, [&](std::size_t k) {
                                return tl.clips[k]->start >= clip_end(*b) - eps;
                            });
        if (grew && behind) {
            // Creció por la derecha y solo pisa a los de detrás: se empujan.
            double first = tl.clips[hits.front()]->start;
            for (const std::size_t k : hits) {
                first = std::min(first, tl.clips[k]->start);
            }
            const double shift = clip_end(c) - first;
            const double origin = clip_end(*b) - eps;
            const std::string id = c.id;
            const std::string track = c.track_id;
            int moved = 0;
            for (auto& x : tl.clips) {
                if (x->track_id == track && x->id != id && x->start >= origin) {
                    x.mut().start = py_round(x->start + shift, 3);
                    ++moved;
                }
            }
            out.notes.push_back(id + " creció: " + std::to_string(moved) + " clip(s) de " + track +
                                " se desplazaron " + py_repr(py_round(shift, 3)) + " s");
            continue;
        }
        const std::string src = c.track_id;
        const std::string id = c.id;
        const auto to = lift(tl, ci, ids);
        if (!to) {
            continue;
        }
        tl.clips[ci].mut().track_id = to->track_id;
        if (to->created) {
            out.created_tracks.push_back(to->track_id);
        }
        out.notes.push_back(id + " no cabía en " + src + " (ocupada): va en " +
                            (to->created ? "la pista nueva " : "") + to->track_id);
    }
    if (out.created_tracks.empty() && out.notes.empty()) {
        return std::nullopt;
    }
    return out;
}

} // namespace gc::core
