// Paridad de los TRAMOS DEL HEATMAP de YouTube con video-yt (heatmap._pick_segments).
//
// Fixture: tests/fixtures/heatmap/synthetic.json (export_fixtures.py --solo heatmap, casos de
// heatmap_cases.py): [heatmap, duration, min_score, max_clips, max_duration, padding, tramos].

#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "gc/core/heatmap.hpp"
#include "gc/core/io.hpp"

using gc::Json;
using namespace gc::core;

TEST_CASE("paridad del heatmap de YouTube: tramos de alto interés", "[parity][heatmap]") {
    const auto fixture = read_json_file(path_from_utf8(GC_FIXTURES_DIR) / "heatmap" / "synthetic.json");
    REQUIRE(fixture); // faltan: ejecuta tools/fixtures/export_fixtures.py --solo heatmap
    std::size_t n = 0;
    for (const Json& c : *fixture) {
        std::vector<HeatPoint> points;
        for (const Json& p : c[0]) {
            points.push_back({p.at("start_time").get<double>(), p.at("end_time").get<double>(),
                              p.at("value").get<double>()});
        }
        const auto got = pick_heatmap_segments(points, c[1].get<double>(), c[2].get<double>(),
                                               c[3].get<std::int64_t>(), c[4].get<double>(), c[5].get<double>());
        Json out = Json::array();
        for (const HeatSegment& s : got) {
            out.push_back({{"index", s.index}, {"start", s.start}, {"end", s.end}, {"score", s.score},
                           {"duration", s.duration}});
        }
        INFO("caso " << n << ": " << c[1] << ", " << c[2] << ", " << c[3] << ", " << c[4] << ", " << c[5]);
        CHECK(out == c[6]);
        ++n;
    }
    CHECK(n >= 30);
}
