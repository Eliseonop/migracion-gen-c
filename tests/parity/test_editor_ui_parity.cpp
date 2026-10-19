// Paridad de la timeline del editor (gc/core/timeline_ui.hpp) con editorModel.js de video-yt,
// ejecutado con Node: tests/fixtures/editor/timeline_ui.json (editor_cases.py).

#include <filesystem>
#include <limits>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "gc/core/io.hpp"
#include "gc/core/serialize.hpp"
#include "gc/core/timeline_ui.hpp"

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

Json run(const Timeline& tl, const Json& call) {
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
        const Json& calls = c.at("calls");
        const Json& results = c.at("results");
        for (std::size_t i = 0; i < calls.size(); ++i) {
            INFO(c.at("name").get<std::string>() << " :: " << calls[i].dump());
            CHECK(nlohmann::json::parse(run(*tl, calls[i]).dump()) ==
                  nlohmann::json::parse(results[i].dump()));
            ++n;
        }
    }
    CHECK(n > 10000);
}
