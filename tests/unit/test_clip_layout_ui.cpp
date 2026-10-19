// El panel de Transformación (clip_layout_ui.hpp): EdTransform.jsx sobre la pose del clip.

#include <catch2/catch_test_macros.hpp>

#include "gc/core/clip_layout_ui.hpp"
#include "gc/core/serialize.hpp"

using gc::Json;
using namespace gc::core;

namespace {

TimelineClip make(const Json& extra) {
    Json j = {{"id", "c"},         {"track_id", "V1"},    {"kind", "video"}, {"asset_kind", "clips"},
              {"asset_id", "c"},   {"filename", "a.mp4"}, {"start", 0.0},    {"in_point", 0.0},
              {"out_point", 4.0},  {"source_duration", 4.0}};
    j.update(extra);
    auto c = clip_from_json(j);
    REQUIRE(c);
    return *c;
}

} // namespace

TEST_CASE("transformación: un texto se mueve con la posición de CapCut", "[layout]") {
    const TimelineClip t = make({{"kind", "text"}, {"asset_kind", "text"}, {"text", "hola"},
                                 {"style", {{"x", 0.75}, {"y", 0.25}, {"scale", 2}}}});
    const TransformView v = transform_view(t, 0, 1080, 1920);
    CHECK(v.capcut);
    CHECK(v.scale);
    CHECK_FALSE(v.zoom_scale);
    CHECK(v.x == 540);  // (0,75 − 0,5) · 2 · 1080
    CHECK(v.y == 960);  // Y hacia arriba
    CHECK(v.scale_pct == 200);
    CHECK(transform_pos_patch(t, 0, 0, 1080, 1920) == Json{{"x", 0.5}, {"y", 0.5}});
    CHECK(transform_pos_patch(t, 1e9, 0, 1080, 1920)["x"] == 11); // CLIP_POS_MAX
    CHECK(transform_scale_patch(t, 50000) == Json{{"scale", 100}}); // TEXT_SCALE_MAX
    CHECK(transform_reset_patch(t).contains("rot_x"));
}

TEST_CASE("transformación: un vídeo libre escala respecto al alto del cuadro", "[layout]") {
    const TimelineClip c = make({{"layout", "overlay"}, {"transform", {{"x", 0.5}, {"y", 0.5}, {"scale", 1.5}}}});
    CHECK(transform_view(c, 0, 1080, 1920, 0.5).scale_pct == 300); // 1,5 / (1920 / 3840)
    CHECK(transform_scale_patch(c, 100, 0.5) == Json{{"scale", 0.5}});
    CHECK(transform_scale_patch(c, 120) == Json{{"scale", 1.2}});
    CHECK(transform_reset_patch(c, 0.5)["scale"] == 0.5);
}

TEST_CASE("transformación: un vídeo que llena el cuadro encuadra la fuente", "[layout]") {
    const TimelineClip c = make({{"reframe", {{"zoom", 0.5}}}});
    const TransformView v = transform_view(c, 0, 1080, 1920);
    CHECK_FALSE(v.capcut);
    CHECK(v.zoom_scale);
    CHECK(v.scale_pct == 200);
    CHECK(transform_scale_patch(c, 400) == Json{{"zoom", 0.25}});
    CHECK(transform_scale_patch(c, 50) == Json{{"zoom", 1}}); // menos de 100 % no aleja
    CHECK(transform_pos_patch(c, 130, -5, 1080, 1920) == Json{{"cx", 1}, {"cy", 0}});
    CHECK_FALSE(transform_view(make({{"kind", "audio"}, {"asset_kind", "audios"}}), 0, 1080, 1920).show);
}
