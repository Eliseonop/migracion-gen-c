// La duración del vídeo exportado (compose.build_command): export_input.hpp.

#include <catch2/catch_test_macros.hpp>

#include "gc/core/export_input.hpp"

using namespace gc::core;

namespace {

TimelineClip clip(std::string track, double start, double in, double out) {
    TimelineClip c;
    c.id = "c";
    c.track_id = std::move(track);
    c.kind = "video";
    c.asset_kind = "clips";
    c.start = start;
    c.in_point = in;
    c.out_point = out;
    return c;
}

} // namespace

TEST_CASE("export_total_duration: el mayor fin de los clips con pista, en ms", "[export]") {
    Timeline tl;
    CHECK(export_total_duration(tl) == 0.0); // vacía
    TimelineTrack v1;
    v1.id = "V1";
    v1.kind = "video";
    tl.tracks.push_back(v1);
    tl.clips.emplace_back(clip("V1", 0.5, 0.0, 1.0));
    tl.clips.emplace_back(clip("V9", 0.0, 0.0, 50.0)); // pista que no existe: no cuenta
    CHECK(export_total_duration(tl) == 1.5);
    TimelineClip off = clip("V1", 1.0, 0.0, 1.23456);
    off.disabled = true; // desactivado: sí cuenta (Python no lo mira)
    tl.clips.emplace_back(off);
    CHECK(export_total_duration(tl) == 2.235); // round(2.23456, 3)
}
