// Paridad de la timeline del editor (gc/core/timeline_ui.hpp) con editorModel.js de video-yt,
// ejecutado con Node: tests/fixtures/editor/timeline_ui.json (editor_cases.py).

#include <cmath>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "gc/core/io.hpp"
#include "gc/core/serialize.hpp"
#include "gc/core/timeline_drop.hpp"
#include "gc/core/timeline_ui.hpp"
#include "gc/core/timeline_view.hpp"

using gc::Json;
using namespace gc::core;

namespace {

const std::filesystem::path kFixtures = path_from_utf8(GC_FIXTURES_DIR);

// Las claves de los objetos se comparan sin orden (Json conserva el orden de inserción).
std::vector<std::string> strings(const Json& list) {
    return list.get<std::vector<std::string>>();
}

Json ids(const std::vector<const TimelineClip*>& clips) {
    Json out = Json::array();
    for (const TimelineClip* c : clips) {
        out.push_back(c->id);
    }
    return out;
}

// Number(raw) de JS para el alto de fila: null y "" son «sin valor»; un texto que no es número, NaN.
std::optional<double> row_raw(const Json& v) {
    if (v.is_null() || v == "") {
        return std::nullopt;
    }
    if (v.is_string()) {
        char* end = nullptr;
        const std::string s = v.get<std::string>();
        const double n = std::strtod(s.c_str(), &end);
        return *end == '\0' ? n : std::numeric_limits<double>::quiet_NaN();
    }
    return v.get<double>();
}

TrimEdge edge(const Json& mode) {
    return mode == "trim-left" ? TrimEdge::Left : TrimEdge::Right;
}

Json patch_json(const ClipPatch& p) {
    Json out = Json::object();
    const auto put = [&](const char* key, const std::optional<double>& v) {
        if (v) {
            out[key] = *v;
        }
    };
    put("start", p.start);
    put("in_point", p.in_point);
    put("out_point", p.out_point);
    put("source_duration", p.source_duration);
    return out;
}

Json slot_json(const std::optional<TrackSlot>& slot) {
    if (!slot) {
        return nullptr;
    }
    return {{"targetId", slot->target_id ? Json(*slot->target_id) : Json()},
            {"place", slot->below ? "below" : "above"}};
}

TrackSlot slot_from(const Json& j) {
    return {j.at("targetId").is_string() ? std::optional(j.at("targetId").get<std::string>()) : std::nullopt,
            j.at("place") == "below"};
}

Json times_json(const std::vector<double>& times) {
    return times;
}

Json run(const Timeline& tl, const std::vector<TrackRow>& geo, const Json& call) {
    const std::string fn = call.at(0).get<std::string>();
    const auto clip = [&](const Json& id) -> const TimelineClip& {
        for (const auto& c : tl.clips) {
            if (c->id == id.get<std::string>()) {
                return *c;
            }
        }
        throw std::runtime_error("clip inexistente en la fixture");
    };
    const auto num = [&](std::size_t i) { return call.at(i).get<double>(); };
    const auto str = [&](std::size_t i) { return call.at(i).get<std::string>(); };
    const auto others = [&](const std::vector<std::string>& exclude,
                            const std::vector<std::string>& beats_exclude) {
        auto out = align_others(tl.clips, exclude);
        for (auto& t : snap_targets(tl.markers, tl.clips, beats_exclude)) {
            out.push_back(std::move(t));
        }
        return out;
    };
    const auto clip_ptrs = [&](const Json& ids) {
        std::vector<const TimelineClip*> out;
        for (const Json& id : ids) {
            out.push_back(&clip(id));
        }
        return out;
    };
    const auto id_or_null = [](const TimelineClip* c) { return c ? Json(c->id) : Json(); };
    if (fn == "toFixed") {
        return js_fixed3(num(1));
    }
    if (fn == "clipDur") {
        return ui_clip_duration(clip(call.at(1)));
    }
    if (fn == "clipsOnTrackSorted") {
        return ids(clips_on_track_sorted(tl.clips, call.at(1).get<std::string>()));
    }
    if (fn == "freeStartOnTrack" || fn == "clampStartNoOverlap") {
        const auto exclude = strings(call.at(4));
        const auto f = fn == "freeStartOnTrack" ? free_start_on_track : clamp_start_no_overlap;
        return f(tl.clips, call.at(1).get<std::string>(), num(2), num(3), exclude);
    }
    if (fn == "trimBounds") {
        const TrimBounds b = trim_bounds(tl.clips, clip(call.at(1)));
        return {{"left", b.left}, {"right", std::isinf(b.right) ? Json("inf") : Json(b.right)}};
    }
    if (fn == "clampTrimDelta") {
        return clamp_trim_delta(tl.clips, clip(call.at(1)), edge(call.at(2)), num(3));
    }
    if (fn == "trimClipPatch") {
        return patch_json(trim_clip_patch(clip(call.at(1)), edge(call.at(2)), num(3)));
    }
    if (fn == "rangeSelectOnTrack") {
        return range_select_on_track(tl.clips, call.at(1).get<std::string>(), call.at(2).get<std::string>(),
                                     call.at(3).get<std::string>());
    }
    if (fn == "nextClipSelection") {
        ClipSelection cur{strings(call.at(1)), {}};
        if (call.at(2).is_string()) {
            cur.anchor = call.at(2).get<std::string>();
        }
        const Json& m = call.at(4);
        const SelectMods mods{m.value("additive", false), m.value("range", false),
                              m.value("keepGroup", false)};
        const ClipSelection next = next_clip_selection(tl.clips, cur, call.at(3).get<std::string>(), mods);
        return {{"ids", next.ids}, {"anchorId", next.anchor ? Json(*next.anchor) : Json()}};
    }
    if (fn == "maxPps") {
        return max_pps(num(1), num(2));
    }
    if (fn == "minPps") {
        return min_pps(num(1), num(2));
    }
    if (fn == "clampPps") {
        return clamp_pps(num(1), num(2), num(3), num(4));
    }
    if (fn == "zoomByDrag") {
        return zoom_by_drag(num(1), num(2), num(3), num(4), num(5));
    }
    if (fn == "tickSteps") {
        return tick_steps(num(1));
    }
    if (fn == "tickStep") {
        return tick_step(num(1), num(2), call.at(3).get<bool>());
    }
    if (fn == "fmtRuler") {
        const Json& o = call.at(2);
        const std::optional<double> step =
            o.contains("step") ? std::optional(o.at("step").get<double>()) : std::nullopt;
        return fmt_ruler(num(1), o.at("fps").get<double>(), step, o.at("long").get<bool>());
    }
    if (fn == "buildTicks") {
        const Json& o = call.at(3);
        Json out = Json::array();
        for (const Tick& k :
             build_ticks(num(1), num(2), o.at("fps").get<double>(), o.at("scrollX").get<double>(),
                         o.at("viewW").get<double>(), o.at("dense").get<bool>())) {
            out.push_back({k.t, k.major, k.step});
        }
        return out;
    }
    if (fn == "clampRowHeight") {
        return clamp_row_height(row_raw(call.at(1)));
    }
    if (fn == "trackRowHeight") {
        return track_row_height(call.at(1).get<std::string>(), row_raw(call.at(2)));
    }
    if (fn == "stepRowHeight") {
        return step_row_height(row_raw(call.at(1)), num(2));
    }
    if (fn == "displayTracks") {
        Json out = Json::array();
        for (const TimelineTrack* t : display_tracks(tl.tracks)) {
            out.push_back(t->id);
        }
        return out;
    }
    if (fn == "alignThresholdSec") {
        return align_threshold_sec(num(1));
    }
    if (fn == "clipBeatTimes") {
        return times_json(clip_beat_times(clip(call.at(1))));
    }
    if (fn == "snapTargets") {
        Json out = Json::array();
        for (const AlignClip& t : snap_targets(tl.markers, tl.clips, strings(call.at(1)))) {
            out.push_back({t.track_id, t.start, t.end});
        }
        return out;
    }
    if (fn == "timelineAlignHits") {
        const auto ids_ = strings(call.at(1));
        std::vector<AlignClip> moving;
        for (const auto& id : ids_) {
            moving.push_back(as_align_clip(clip(Json(id))));
        }
        const std::string which = str(3);
        const AlignEdge edge_ = which == "start" ? AlignEdge::Start
                                : which == "end" ? AlignEdge::End
                                                 : AlignEdge::Both;
        return times_json(timeline_align_hits(moving, others(ids_, ids_), num(2), edge_));
    }
    if (fn == "nearestAlignTime") {
        const auto t = nearest_align_time(num(1), str(2), others({}, {}), num(3));
        return t ? Json(*t) : Json();
    }
    if (fn == "snapClipMove") {
        const std::string id = str(1);
        const auto r = snap_clip_move(clip(call.at(1)), num(2), others({id}, {id}), num(3),
                                      call.at(4).is_string() ? call.at(4).get<std::string>() : std::string());
        return {{"start", r.start}, {"times", r.times}};
    }
    if (fn == "snapClipTrim") {
        const auto r =
            snap_clip_trim(clip(call.at(1)), edge(call.at(2)), num(3), others({str(1)}, {}), num(4));
        return {{"patch", patch_json(r.patch)}, {"times", r.times}, {"deltaT", r.delta}};
    }
    if (fn == "snapClipGroup") {
        const auto ids_ = strings(call.at(1));
        const auto r = snap_clip_group(clip_ptrs(call.at(1)), num(2), others(ids_, ids_), num(3));
        return {{"deltaT", r.delta}, {"times", r.times}};
    }
    if (fn == "clampGroupDelta") {
        return clamp_group_delta(tl.clips, clip_ptrs(call.at(1)), num(2), tl.tracks);
    }
    if (fn == "clipAtTime") {
        return id_or_null(clip_at_time(tl.clips, str(1), num(2), strings(call.at(3))));
    }
    if (fn == "clipCollidingWith") {
        return id_or_null(clip_colliding_with(tl.clips, str(1), num(2), num(3), strings(call.at(4))));
    }
    if (fn == "zoneAt") {
        const Zone z = zone_at(tl.tracks, geo, str(1), num(2), kBoundaryPx,
                               call.at(3).is_string() ? call.at(3).get<std::string>() : std::string(),
                               call.at(4).get<bool>());
        return {{"trackId", z.track_id ? Json(*z.track_id) : Json()},
                {"slot", slot_json(z.slot)},
                {"lineY", z.line_y}};
    }
    if (fn == "placeOnTrack") {
        const auto r = place_on_track(tl.clips, str(1), num(2), num(3), strings(call.at(4)), num(5), num(6));
        return r ? Json(*r) : Json();
    }
    if (fn == "liftTarget") {
        const auto r = lift_target(tl.tracks, tl.clips, str(1), num(2), num(3), strings(call.at(4)));
        if (!r) {
            return nullptr;
        }
        return {{"trackId", r->track_id ? Json(*r->track_id) : Json()}, {"slot", slot_json(r->slot)}};
    }
    if (fn == "magneticCut") {
        const auto r = magnetic_cut(tl.clips, str(1), num(2), strings(call.at(3)));
        return {{"start", r.start}, {"index", r.index}};
    }
    if (fn == "dropTarget") {
        const Json& q = call.at(1);
        const auto exclude = strings(q.at("excludeIds"));
        DropQuery dq{
            .kind = q.at("kind").get_ref<const std::string&>(),
            .y = q.at("y").get<double>(),
            .time = q.at("time").get<double>(),
            .duration = q.at("dur").get<double>(),
            .pointer_time =
                q.contains("pointerTime") ? std::optional(q.at("pointerTime").get<double>()) : std::nullopt,
            .exclude = exclude,
            .origin_id = q.at("originId").is_string() ? q.at("originId").get_ref<const std::string&>()
                                                      : std::string_view(),
            .origin_alone = q.at("originAlone").get<bool>(),
            .replace = q.at("replace").get<bool>(),
            .pps = q.at("pps").get<double>(),
            .snap_sec = q.at("snapSec").get<double>()};
        const auto d = drop_target(tl.tracks, tl.clips, geo, dq);
        if (!d) {
            return nullptr;
        }
        return {{"trackId", d->track_id ? Json(*d->track_id) : Json()},
                {"slot", slot_json(d->slot)},
                {"lineY", d->line_y},
                {"start", d->start},
                {"replaceId", d->replace_id ? Json(*d->replace_id) : Json()},
                {"insert", d->insert},
                {"lifted", d->lifted}};
    }
    if (fn == "newTrackIndex") {
        return new_track_index(tl.tracks, str(1), str(2) == "below");
    }
    if (fn == "slotBoundary") {
        return slot_boundary(tl.tracks, slot_from(call.at(1)));
    }
    if (fn == "slotIndex") {
        return slot_index(tl.tracks, slot_from(call.at(1)));
    }
    if (fn == "emptiedTracks") {
        return emptied_tracks(tl.tracks, tl.clips, strings(call.at(1)));
    }
    if (fn == "groupMoveFromOrig") {
        std::vector<std::pair<std::string, double>> origs;
        for (const Json& o : call.at(1)) {
            origs.emplace_back(o.at("id").get<std::string>(), o.at("start").get<double>());
        }
        Json out = Json::array();
        for (const auto& [id, start] : group_move_starts(origs, num(2))) {
            out.push_back(start);
        }
        return out;
    }
    throw std::runtime_error("función desconocida: " + fn);
}

} // namespace

TEST_CASE("paridad de la timeline del editor: editorModel.js", "[parity][editor]") {
    const auto path = kFixtures / "editor" / "timeline_ui.json";
    REQUIRE(std::filesystem::exists(path)); // faltan: export_fixtures.py --solo editor
    auto file = read_json_file(path);
    REQUIRE(file);
    std::size_t n = 0;
    for (const Json& c : file->at("cases")) {
        auto tl = timeline_from_json(c.at("timeline"));
        REQUIRE(tl);
        std::vector<TrackRow> geo;
        for (const Json& g : c.at("geo")) {
            geo.push_back(
                {g.at("id").get<std::string>(), g.at("top").get<double>(), g.at("bottom").get<double>()});
        }
        const Json& calls = c.at("calls");
        const Json& results = c.at("results");
        for (std::size_t i = 0; i < calls.size(); ++i) {
            INFO(c.at("name").get<std::string>() << " :: " << calls[i].dump());
            const Json got = run(*tl, geo, calls[i]);
            if (calls[i].at(0) == "zoomByDrag") { // Math.pow de V8 y el pow de la libc: hasta 1 ulp
                CHECK(std::abs(got.get<double>() / results[i].get<double>() - 1) < 1e-15);
            } else {
                CHECK(nlohmann::json::parse(got.dump()) == nlohmann::json::parse(results[i].dump()));
            }
            ++n;
        }
    }
    CHECK(n > 10000);
}
