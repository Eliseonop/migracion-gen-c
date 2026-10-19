// Paridad de la timeline del editor (gc/core/timeline_ui.hpp) con editorModel.js de video-yt,
// ejecutado con Node: tests/fixtures/editor/timeline_ui.json (editor_cases.py).

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "gc/core/clip_layout_ui.hpp"
#include "gc/core/fx_catalog.hpp"
#include "gc/core/clip_pose_edit.hpp"
#include "gc/core/io.hpp"
#include "gc/core/mask_edit.hpp"
#include "gc/core/material_drop.hpp"
#include "gc/core/serialize.hpp"
#include "gc/core/sfx_name.hpp"
#include "gc/core/shapes.hpp"
#include "gc/core/text_clip_ui.hpp"
#include "gc/core/timeline_drop.hpp"
#include "gc/core/timeline_ui.hpp"
#include "gc/core/timeline_view.hpp"
#include "gc/core/vfx_stack.hpp"

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

Json without_ids(Json keyframes) {
    if (keyframes.contains("items")) {
        for (Json& k : keyframes["items"]) {
            k.erase("id");
        }
    }
    return keyframes;
}

// Lo que cambia commitPose, como lo devuelve editor_ui_js.mjs (poseView).
Json pose_view(const TimelineClip& c) {
    const Json dumped = to_json(c);
    const auto get = [&](const char* key) { return dumped.contains(key) ? dumped.at(key) : Json(); };
    return {{"keyframes", c.keyframes ? without_ids(*c.keyframes) : Json()},
            {"style", get("style")},
            {"shape", get("shape")},
            {"transform", get("transform")},
            {"opacity", get("opacity")},
            {"volume", get("volume")},
            {"audio_fx", get("audio_fx")},
            {"frame", get("frame")},
            {"zoom", c.reframe ? Json(c.reframe->zoom) : Json()}};
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

CropWindow crop_from(const Json& j) {
    return {j.at("cx").get<double>(), j.at("cy").get<double>(), j.at("wf").get<double>(), j.at("hf").get<double>()};
}

Json crop_json(const CropWindow& c) { return {{"cx", c.cx}, {"cy", c.cy}, {"wf", c.wf}, {"hf", c.hf}}; }

Json dest_json(const DestRect& d) {
    return {{"dx", d.dx}, {"dy", d.dy}, {"dw", d.dw}, {"dh", d.dh}, {"rotation", d.rotation}};
}

double opt_num(const Json& v) { return v.is_number() ? v.get<double>() : 0; }

// Geometría con senos y cosenos o divisiones encadenadas: la de V8 y la de la libc difieren en ulps.
bool near(const Json& a, const Json& b) {
    if (a.is_number() && b.is_number()) {
        const double x = a.get<double>();
        const double y = b.get<double>();
        return x == y || std::abs(x - y) <= 1e-12 * std::max({1.0, std::abs(x), std::abs(y)});
    }
    if (a.is_object() && b.is_object()) {
        return a.size() == b.size() && std::ranges::all_of(a.items(), [&](const auto& kv) {
                   return b.contains(kv.key()) && near(kv.value(), b.at(kv.key()));
               });
    }
    return a == b;
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
    if (fn == "normalizeMarkers" || fn == "toggleMarkerAt") {
        SequentialIdSource ids;
        const auto list = fn == "normalizeMarkers"
                              ? normalize_markers(call.at(1).get<std::vector<Json>>(), ids)
                              : toggle_marker_at(tl.markers, num(1), num(2), ids);
        Json out = Json::array();
        for (const Json& m : list) {
            if (fn == "toggleMarkerAt") {
                out.push_back(m.at("t"));
            } else {
                out.push_back({m.at("t"), m.value("label", Json()), m.value("color", Json())});
            }
        }
        return out;
    }
    if (fn == "nextSnapTime") {
        const auto t = next_snap_time(tl.markers, tl.clips, num(1), call.at(2).get<int>());
        return t ? Json(*t) : Json();
    }
    if (fn == "kfState") {
        return kf_state(clip(call.at(1)), num(2), num(3));
    }
    if (fn == "keyframeIdAt") {
        const auto id = keyframe_id_at(clip(call.at(1)), num(2), num(3));
        return id ? Json(*id) : Json();
    }
    if (fn == "textStyleKfKeys") {
        return text_style_kf_keys(clip(call.at(1)));
    }
    if (fn == "toggleKeyframe" || fn == "applyVolumeFade") {
        TimelineClip c = clip(call.at(1));
        SequentialIdSource ids;
        if (fn == "applyVolumeFade") {
            apply_volume_fade_ui(c, ui_clip_duration(c), str(2), ids);
            return without_ids(*c.keyframes);
        }
        toggle_keyframe_at(c, num(2), num(3), ids);
        Json rf = Json::array();
        if (c.reframe) {
            for (const Keyframe& k : c.reframe->keyframes) {
                rf.push_back(k.t);
            }
        }
        return {{"keyframes", c.keyframes ? without_ids(*c.keyframes) : Json()}, {"reframeKfs", rf}};
    }
    if (fn == "poseKeyframed") {
        return pose_keyframed(clip(call.at(1)));
    }
    if (fn == "shouldKeyframe") {
        return should_keyframe(clip(call.at(1)), call.at(2));
    }
    if (fn == "upsertKeyframeAt" || fn == "applyStaticPose" || fn == "commitPose") {
        TimelineClip c = clip(call.at(1));
        SequentialIdSource ids;
        if (fn == "upsertKeyframeAt") {
            upsert_keyframe_ui(c, num(2), call.at(3),
                               call.at(4).is_string() ? std::optional(str(4)) : std::nullopt, num(5), ids);
            return without_ids(*c.keyframes);
        }
        if (fn == "applyStaticPose") {
            apply_static_pose(c, call.at(2));
        } else {
            commit_pose(c, call.at(2), num(3), num(4), ids);
        }
        return pose_view(c);
    }
    if (fn == "segmentRange") {
        const auto r = segment_range(call.at(1));
        return r ? Json{{"in", r->first}, {"out", r->second}} : Json();
    }
    if (fn == "materialDuration") {
        return material_duration(call.at(1));
    }
    if (fn == "dragPayload") {
        return drag_payload(str(1), call.at(2));
    }
    if (fn == "makeClip" || fn == "placeAsset") {
        SequentialIdSource ids;
        Json c = fn == "makeClip"
                     ? make_clip_json(str(1), call.at(2), str(3), num(4), call.at(5), ids)
                     : place_asset_clip(call.at(1), str(2), num(3),
                                        call.at(4).is_number() ? std::optional(num(4)) : std::nullopt, ids);
        c.erase("id");
        if (c.contains("reframe") && c["reframe"].is_object()) {
            for (const char* key : {"keyframes", "keyframes2"}) {
                for (Json& k : c["reframe"][key]) {
                    k.erase("id");
                }
            }
        }
        return c;
    }
    if (fn == "frameOf") {
        return frame_of(clip(call.at(1)));
    }
    if (fn == "slotAspectOf") {
        return slot_aspect_of(clip(call.at(1)), num(2));
    }
    if (fn == "toCapcutPos") {
        const auto [X, Y] = to_capcut_pos(num(1), num(2), num(3), num(4));
        return {{"X", X}, {"Y", Y}};
    }
    if (fn == "fromCapcutPos") {
        const auto [x, y] = from_capcut_pos(num(1), num(2), num(3), num(4));
        return {{"x", x}, {"y", y}};
    }
    if (fn == "frameRectOf") {
        const Rect r = frame_rect_of(num(1), num(2), opt_num(call.at(3)), opt_num(call.at(4)));
        return {{"x", r.x}, {"y", r.y}, {"w", r.w}, {"h", r.h}};
    }
    if (fn == "destRectOnFrame") {
        const Json& t = call.at(1);
        const Json& p = call.at(2);
        const Json& f = call.at(5);
        const Pose pose{t.at("x").get<double>(), t.at("y").get<double>(), t.at("scale").get<double>(),
                        t.at("rotation").get<double>()};
        const SourceCrop px{p.at("sx").get<double>(), p.at("sy").get<double>(), p.at("sw").get<double>(),
                            p.at("sh").get<double>()};
        const Rect frame{f.at("x").get<double>(), f.at("y").get<double>(), f.at("w").get<double>(),
                         f.at("h").get<double>()};
        return dest_json(dest_rect_on_frame(pose, px, num(3), num(4), frame));
    }
    if (fn == "sourceCropPx") {
        const SourceCrop p = source_crop_px(crop_from(call.at(1)), num(2), num(3));
        return {{"sx", p.sx}, {"sy", p.sy}, {"sw", p.sw}, {"sh", p.sh}};
    }
    if (fn == "clampCrop") {
        return crop_json(clamp_crop(num(1), num(2), num(3), num(4)));
    }
    if (fn == "cropSizeFromCorner") {
        const auto [wf, hf] = crop_size_from_corner(num(1), num(2), num(3), num(4));
        return {{"wf", wf}, {"hf", hf}};
    }
    if (fn == "cropHandleAt") {
        const Json& rect = call.at(4);
        const auto h = crop_handle_at(num(1), num(2), crop_from(call.at(3)), rect.value("width", 0.0),
                                      rect.value("height", 0.0), num(5));
        return h ? Json{{"hx", h->first}, {"hy", h->second}} : Json();
    }
    if (fn == "insideCrop") {
        return inside_crop(num(1), num(2), crop_from(call.at(3)));
    }
    if (fn == "resizeCropFree") {
        return crop_json(resize_crop_free(crop_from(call.at(1)), call.at(2).get<int>(), call.at(3).get<int>(), num(4), num(5)));
    }
    if (fn == "resizeCropLocked") {
        return crop_json(resize_crop_locked(crop_from(call.at(1)), call.at(2).get<int>(), call.at(3).get<int>(), num(4),
                                            num(5), num(6)));
    }
    if (fn == "cropRatioNorm") {
        const auto r = crop_ratio_norm(str(1), num(2), num(3));
        return r ? Json(*r) : Json();
    }
    if (fn == "fitCropRatio") {
        return crop_json(fit_crop_ratio(crop_from(call.at(1)), num(2)));
    }
    if (fn == "cropCursor") {
        return crop_cursor(call.at(1).get<int>(), call.at(2).get<int>());
    }
    if (fn == "freeFrameAt") {
        const Frame f = free_frame_at(clip(call.at(1)), num(2), num(3), num(4), num(5), num(6));
        return {{"cx", f.cx}, {"cy", f.cy}, {"zoom", f.zoom}};
    }
    if (fn == "hitTransformHandle") {
        const Json& d = call.at(3);
        const auto hit = hit_transform_handle(num(1), num(2),
                                              {d.at("dx").get<double>(), d.at("dy").get<double>(),
                                               d.at("dw").get<double>(), d.at("dh").get<double>(),
                                               d.at("rotation").get<double>()});
        return hit ? Json(*hit) : Json();
    }
    if (fn == "defaultShape") {
        return default_shape(call.at(1));
    }
    if (fn == "makeShapeClip" || fn == "makeAdjustmentClip") {
        SequentialIdSource ids;
        const double dur = call.at(3).is_number() ? num(3) : 0;
        Json c = fn == "makeShapeClip" ? make_shape_clip_json(str(1), num(2), dur, call.at(4), ids)
                                       : make_adjustment_clip_json(str(1), num(2), dur, ids);
        c.erase("id");
        return c;
    }
    if (fn == "subtitleThemes") {
        return subtitle_themes();
    }
    if (fn == "textLooks") {
        return text_looks();
    }
    if (fn == "applyThemeToStyle") {
        return apply_theme_to_style(call.at(1), *theme_by_id(str(2)));
    }
    if (fn == "clearTextTheme") {
        return clear_text_theme(call.at(1));
    }
    if (fn == "selectedSubtitleThemeId") {
        const auto id = selected_subtitle_theme_id(call.at(1));
        return id ? Json(*id) : Json();
    }
    if (fn == "applyOrClearTheme") {
        return apply_or_clear_theme(call.at(1), call.at(2).is_string() ? theme_by_id(str(2)) : nullptr);
    }
    if (fn == "lookPatch") {
        const Json& looks = text_looks();
        const auto it = std::ranges::find_if(looks, [&](const Json& l) { return l.at("id") == call.at(1); });
        return look_patch(it == looks.end() ? nullptr : &*it);
    }
    if (fn == "activeLookId") {
        const auto id = active_look_id(call.at(1));
        return id ? Json(*id) : Json();
    }
    if (fn == "defaultTextStyle") {
        return default_text_style();
    }
    if (fn == "subtitleStyle") {
        return subtitle_style();
    }
    if (fn == "newTextStyle") {
        return new_text_style(call.at(1));
    }
    if (fn == "makeTextClip") {
        SequentialIdSource ids;
        Json c = make_text_clip_json(str(1), num(2), num(3), str(4), call.at(5).is_null() ? Json() : call.at(5), ids);
        c.erase("id");
        c.erase("asset_id");
        return c;
    }
    if (fn == "newTextDuration") {
        return new_text_duration(tl, num(1));
    }
    if (fn == "confirmCrop") {
        SequentialIdSource ids;
        const TimelineClip& c = clip(call.at(1));
        const Json& cr = call.at(2);
        const double t = num(4);
        const double local = std::max(0.0, t);
        const double src = std::clamp(c.reverse ? c.out_point - local * ui_clip_speed(c) : c.in_point + local * ui_clip_speed(c),
                                      c.in_point, c.out_point);
        auto out = confirm_crop(c, {cr.at("cx").get<double>(), cr.at("cy").get<double>(), cr.at("wf").get<double>(),
                                    cr.at("hf").get<double>()},
                                num(3), t, src, num(5), ids);
        REQUIRE(out);
        Json j = to_json(*out);
        Json rf = j.value("reframe", Json());
        if (rf.is_object()) {
            for (Json& k : rf["keyframes"]) {
                k.erase("id");
            }
        }
        return {{"reframe", rf}, {"transform", j.value("transform", Json())}, {"frame", j.value("frame", Json())},
                {"keyframes", out->keyframes ? without_ids(*out->keyframes) : Json()}};
    }
    if (fn == "styleKfState") {
        return style_kf_state(clip(call.at(1)), num(2), strings(call.at(3)), num(4));
    }
    if (fn == "toggleStyleKf") {
        SequentialIdSource ids;
        TimelineClip c = clip(call.at(1));
        toggle_style_kf(c, call.at(2), num(4), strings(call.at(3)), num(5), ids);
        return c.keyframes ? without_ids(*c.keyframes) : Json();
    }
    if (fn == "sfxNameFromFile") {
        return sfx_name_from_file(call.at(1));
    }
    if (fn == "realSfxCategories") {
        return real_sfx_categories(call.at(1));
    }
    if (fn == "upsertSfxCategory") {
        return upsert_sfx_category(call.at(1), call.at(2));
    }
    if (fn == "matchSfxCategory") {
        return match_sfx_category(call.at(1), call.at(2));
    }
    if (fn == "sfxDraftFromSaved") {
        return sfx_draft_from_saved(call.at(1));
    }
    if (fn == "sfxSavePayload") {
        return sfx_save_payload(call.at(1), call.at(2));
    }
    if (fn == "normalizeFavorites") {
        return normalize_favorites(call.at(1));
    }
    if (fn == "nameTextStyleFavorite") {
        return name_text_style_favorite(call.at(1), call.at(2));
    }
    if (fn == "splitClipByMaxWords") {
        SequentialIdSource ids;
        Json out = Json::array();
        for (Json c : split_clip_by_max_words(clip(call.at(1)), call.at(2), ids)) {
            c.erase("id");
            c.erase("asset_id");
            out.push_back(std::move(c));
        }
        return out;
    }
    if (fn == "splitTrackText") {
        SequentialIdSource ids;
        const auto clips = split_track_text(tl.clips, str(1), call.at(2), ids);
        REQUIRE(clips);
        Json out = Json::array();
        for (const auto& c : *clips) {
            out.push_back({c->text ? Json(*c->text) : Json(), c->start, c->out_point});
        }
        return out;
    }
    if (fn == "maskOp") {
        SequentialIdSource ids;
        TimelineClip c = clip(call.at(1));
        const std::string op = str(2);
        const double aspect = num(4);
        if (op == "addMask" || op == "addAdjust") {
            add_mask(c, str(3), op == "addMask" ? "clip" : "adjust", aspect, ids);
        } else if (op == "removeMask") {
            remove_mask(c);
        } else if (op == "duplicateMask") {
            duplicate_mask(c, ids);
        } else if (op == "changeMask") {
            change_mask(c, call.at(3), aspect, ids);
        } else {
            commit_mask(c, call.at(3), num(5), num(6), ids);
        }
        Json masks = Json::array();
        for (Json m : c.masks) {
            m.erase("id");
            masks.push_back(std::move(m));
        }
        return {{"masks", masks}, {"keyframes", c.keyframes ? without_ids(*c.keyframes) : Json()}};
    }
    if (fn == "normalizeVfxEntry" || fn == "makeVfxEntry" || fn == "keepsUid") {
        SequentialIdSource ids;
        auto e = fn == "makeVfxEntry" ? make_vfx_entry(str(1), ids) : normalize_vfx_entry(call.at(1), ids);
        if (!e) {
            return nullptr;
        }
        if (fn == "keepsUid") {
            return e->at("uid");
        }
        e->erase("uid");
        return *e;
    }
    if (fn == "patchKeyframe" || fn == "moveKeyframe") {
        SequentialIdSource ids;
        TimelineClip c = clip(call.at(1));
        if (fn == "moveKeyframe") {
            move_keyframe(c, str(2), num(3), num(4), ids);
            Json reframe = Json::array();
            for (const Keyframe& k : c.reframe ? c.reframe->keyframes : std::vector<Keyframe>{}) {
                reframe.push_back({k.extra.value("id", Json()), k.t});
            }
            return {{"keyframes", c.keyframes ? without_ids(*c.keyframes) : Json()}, {"reframe", reframe}};
        }
        const Json& p = call.at(3);
        KeyframePatch patch{p.contains("t") ? std::optional(p.at("t").get<double>()) : std::nullopt,
                            p.contains("interpolation") ? std::optional(p.at("interpolation").get<std::string>())
                                                        : std::nullopt,
                            p.value("bezier", Json()), p.value("props", Json())};
        patch_keyframe(c, str(2), patch, num(4), ids);
        return c.keyframes ? without_ids(*c.keyframes) : Json();
    }
    if (fn == "targetInterpItem") {
        const auto k = target_interp_item(clip(call.at(1)), num(2), num(3));
        return k ? Json{k->at("t"), interp_bezier(*k)} : Json();
    }
    if (fn == "copyKeyframeAt") {
        const auto board = copy_keyframe_at(clip(call.at(1)), num(2), num(3));
        if (!board) {
            return nullptr;
        }
        return {{"type", "keyframe"},
                {"props", board->props},
                {"interpolation", board->interpolation},
                {"bezier", board->bezier},
                {"srcKind", board->src_kind}};
    }
    if (fn == "pasteKeyframeAt" || fn == "duplicateKeyframeAt") {
        SequentialIdSource ids;
        TimelineClip c = clip(call.at(1));
        if (fn == "duplicateKeyframeAt") {
            duplicate_keyframe_at(c, str(2), num(3), num(4), ids);
        } else if (call.at(3).value("type", "") == "keyframe") { // pasteKeyframeAt ignora otro portapapeles
            const Json& b = call.at(3);
            KeyframeBoard board{b.at("props"), b.value("interpolation", "linear"), b.value("bezier", Json()),
                                b.value("srcKind", "")};
            const auto groups = call.at(4).is_array() ? std::optional(strings(call.at(4))) : std::nullopt;
            paste_keyframe_at(c, num(2), board, groups, num(5), ids);
        }
        return c.keyframes ? without_ids(*c.keyframes) : Json();
    }
    if (fn == "changeShape") {
        SequentialIdSource ids;
        TimelineClip c = clip(call.at(1));
        change_shape(c, call.at(2), num(3), num(4), ids);
        return {{"shape", c.shape ? *c.shape : Json()}, {"keyframes", c.keyframes ? without_ids(*c.keyframes) : Json()}};
    }
    if (fn == "upsertStyleKf" || fn == "changeStyle") {
        SequentialIdSource ids;
        TimelineClip c = clip(call.at(1));
        if (fn == "upsertStyleKf") {
            upsert_style_kf(c, num(2), call.at(3), num(4), ids);
            return c.keyframes ? without_ids(*c.keyframes) : Json();
        }
        change_text_style(c, call.at(2), num(3), num(4), call.at(5).get<bool>(), ids);
        return {{"style", c.style ? *c.style : Json()}, {"keyframes", c.keyframes ? without_ids(*c.keyframes) : Json()}};
    }
    if (fn == "applyAudioSpeedToLinkedText") {
        ClipList clips = tl.clips;
        apply_audio_speed_to_linked_text(clips, tl.tracks, clip(call.at(1)), js_number(call.at(2)));
        Json out = Json::array();
        for (std::size_t i = 0; i < clips.size(); ++i) {
            if (&clips[i].get() != &tl.clips[i].get()) {
                Json words = Json::array();
                for (const Word& w : clips[i]->words) {
                    words.push_back({{"text", w.text}, {"start", w.start}, {"end", w.end},
                                     {"prob", w.prob ? Json(*w.prob) : Json()}});
                }
                out.push_back({clips[i]->id, clips[i]->start, clips[i]->out_point, words});
            }
        }
        return out;
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
    auto catalog = FxCatalog::load(path_from_utf8(GC_RESOURCES_DIR) / "fx" / "catalog.json");
    REQUIRE(catalog);
    set_fx_catalog(std::make_shared<const FxCatalog>(std::move(*catalog)));
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
            if (calls[i].at(0) == "confirmCrop") { // el JS da el objeto tal cual; guardado pasaría por el modelo
                Json j = to_json([&]() -> const TimelineClip& {
                    for (const auto& x : tl->clips) {
                        if (x->id == calls[i].at(1).get<std::string>()) {
                            return *x;
                        }
                    }
                    throw std::runtime_error("clip");
                }());
                const Json& want = results[i];
                j["reframe"] = want.at("reframe");
                j["transform"] = want.at("transform");
                j["frame"] = want.at("frame");
                j["keyframes"] = want.at("keyframes");
                auto saved = clip_from_json(j);
                REQUIRE(saved);
                Json norm = to_json(*saved);
                if (norm["reframe"].is_object()) {
                    for (Json& k : norm["reframe"]["keyframes"]) {
                        k.erase("id");
                    }
                }
                const Json expected = {{"reframe", norm["reframe"]}, {"transform", norm["transform"]},
                                       {"frame", norm["frame"]}, {"keyframes", norm["keyframes"]}};
                CHECK(nlohmann::json::parse(got.dump()) == nlohmann::json::parse(expected.dump()));
            } else if (calls[i].at(0) == "zoomByDrag") { // Math.pow de V8 y el pow de la libc: hasta 1 ulp
                CHECK(std::abs(got.get<double>() / results[i].get<double>() - 1) < 1e-15);
            } else if (c.at("name") == "encuadre") {
                CHECK(near(nlohmann::json::parse(got.dump()), nlohmann::json::parse(results[i].dump())));
            } else {
                CHECK(nlohmann::json::parse(got.dump()) == nlohmann::json::parse(results[i].dump()));
            }
            ++n;
        }
    }
    CHECK(n > 10000);
}
