#include "gc/core/layers.hpp"

#include <algorithm>
#include <cstddef>
#include <string_view>
#include <unordered_map>

#include "gc/core/timeline_rules.hpp"

namespace gc::core {

bool paints(const TimelineClip& clip) {
    const std::string& k = clip.kind;
    return k == "video" || k == "image" || k == "shape" || k == "motion" || k == "adjustment" || k == "text";
}

std::vector<Cow<TimelineClip>> layers_at(const Timeline& timeline, double head, const LayerRules& rules) {
    const auto layers = stack_layers(timeline.tracks);
    // Con ids repetidos gana el último (new Map(...) de JS).
    std::unordered_map<std::string_view, const TimelineTrack*> track_by_id;
    for (const TimelineTrack& t : timeline.tracks) {
        track_by_id[t.id] = &t;
    }
    std::unordered_map<std::string_view, std::size_t> index;
    for (std::size_t i = 0; i < timeline.clips.size(); ++i) {
        index[timeline.clips[i]->id] = i;
    }

    struct Item {
        const Cow<TimelineClip>* clip;
        long layer;
        double start;
        std::size_t index;
    };
    std::vector<Item> items;
    for (const Cow<TimelineClip>& c : timeline.clips) {
        if (!paints(*c) || c->disabled) {
            continue;
        }
        const auto tr = track_by_id.find(c->track_id);
        if (tr == track_by_id.end() || tr->second->hidden) {
            continue;
        }
        const double dur = clip_timeline_duration(*c);
        if (rules.export_rules) {
            const auto fits = clip_fits_track(c->kind, tr->second->kind);
            if (!fits || !*fits || dur <= kMinExportClipDur) {
                continue;
            }
        }
        const double start = rules.export_rules ? std::max(0.0, c->start) : c->start;
        if (!(head >= start - rules.seek_margin && head < start + dur)) {
            continue;
        }
        const auto layer = layers.find(c->track_id);
        items.push_back({&c, layer == layers.end() ? -1L : static_cast<long>(layer->second), start,
                         index.at(c->id)});
    }
    std::ranges::stable_sort(items, [&](const Item& a, const Item& b) {
        if (a.layer != b.layer) {
            return a.layer < b.layer;
        }
        if (rules.export_rules && a.start != b.start) {
            return a.start < b.start;
        }
        return a.index < b.index;
    });

    std::vector<Cow<TimelineClip>> out;
    out.reserve(items.size());
    for (const Item& it : items) {
        out.push_back(*it.clip);
    }
    return out;
}

std::vector<Cow<TimelineClip>> videos_at(const Timeline& timeline, double head, const LayerRules& rules) {
    std::vector<Cow<TimelineClip>> out = layers_at(timeline, head, rules);
    std::erase_if(out, [](const Cow<TimelineClip>& c) { return c->kind == "text"; });
    return out;
}

} // namespace gc::core
