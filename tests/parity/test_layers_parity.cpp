// Paridad de las CAPAS de un instante con video-yt: layersAt/videosAt/stackLayers del
// preview (clipLayout.js y trackStack.js, ejecutados con Node) y stack_layers/
// buried_text_track_ids de track_stack.py.
//
// Fixtures (tools/fixtures/export_fixtures.py):
//   tests/fixtures/layers/synthetic.json   layers_cases.py (en el repo)
//   tests/fixtures/local/layers/real.json  tus timelines en los bordes de cada clip
//
// Se comparan las POSICIONES de los clips en la lista (los ids pueden repetirse), con las
// reglas del preview (kPreviewLayers). Las del render tienen sus propios tests
// (tests/unit/test_layers.cpp). Los instantes en que el clipEnd de JS (sin redondeo,
// figuras a velocidad 1) y el de Python no coinciden los descarta el exportador.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "gc/core/io.hpp"
#include "gc/core/layers.hpp"
#include "gc/core/serialize.hpp"
#include "gc/core/timeline_rules.hpp"

using gc::Cow;
using gc::Json;
using namespace gc::core;

namespace {

const std::filesystem::path kFixtures = path_from_utf8(GC_FIXTURES_DIR);

Json positions(const Timeline& tl, const std::vector<Cow<TimelineClip>>& list) {
    Json out = Json::array();
    for (const Cow<TimelineClip>& c : list) {
        const auto it = std::ranges::find_if(tl.clips, [&](const Cow<TimelineClip>& x) { return x.same_object(c); });
        out.push_back(static_cast<std::int64_t>(it - tl.clips.begin()));
    }
    return out;
}

std::vector<std::pair<std::string, std::int64_t>> sorted_pairs(const Json& pairs) {
    std::vector<std::pair<std::string, std::int64_t>> out;
    for (const Json& p : pairs) {
        out.emplace_back(p.at(0).get<std::string>(), p.at(1).get<std::int64_t>());
    }
    std::ranges::sort(out);
    return out;
}

std::size_t check_cases(const Json& file, const std::string& fname) {
    std::size_t heads = 0;
    for (const Json& c : file.at("cases")) {
        INFO(fname << " :: " << c.at("name").get<std::string>());
        auto tl = timeline_from_json(c.at("timeline"));
        REQUIRE(tl);

        // La pila: el orden de Python y el de JS coinciden; el C++ es un mapa.
        CHECK(c.at("stack_py") == c.at("stack_js"));
        std::vector<std::pair<std::string, std::int64_t>> got;
        for (const auto& [id, layer] : stack_layers(tl->tracks)) {
            got.emplace_back(id, static_cast<std::int64_t>(layer));
        }
        std::ranges::sort(got);
        CHECK(got == sorted_pairs(c.at("stack_py")));

        const auto buried = buried_text_track_ids(tl->tracks);
        CHECK(Json(std::vector<std::string>(buried.begin(), buried.end())) == c.at("buried"));

        const Json& hs = c.at("heads");
        for (std::size_t i = 0; i < hs.size(); ++i) {
            const double head = hs[i].get<double>();
            INFO("head = " << head);
            CHECK(positions(*tl, layers_at(*tl, head, kPreviewLayers)) == c.at("layers")[i]);
            CHECK(positions(*tl, videos_at(*tl, head, kPreviewLayers)) == c.at("videos")[i]);
            ++heads;
        }
    }
    return heads;
}

} // namespace

TEST_CASE("paridad de las capas: casos sintéticos", "[parity][layers]") {
    const auto path = kFixtures / "layers" / "synthetic.json";
    REQUIRE(std::filesystem::exists(path)); // faltan: ejecuta tools/fixtures/export_fixtures.py
    auto file = read_json_file(path);
    REQUIRE(file);
    CHECK(check_cases(*file, "synthetic.json") > 60);
}

TEST_CASE("paridad de las capas: tus timelines", "[parity][layers][local]") {
    const auto path = kFixtures / "local" / "layers" / "real.json";
    if (!std::filesystem::exists(path)) {
        SKIP("Sin fixtures locales: export_fixtures.py --real las genera desde data/projects.json");
    }
    auto file = read_json_file(path);
    REQUIRE(file);
    CHECK(check_cases(*file, "real.json") > 5000);
}
