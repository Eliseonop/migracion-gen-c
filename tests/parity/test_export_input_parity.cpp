// Paridad de CÓMO LEE EL EXPORT EL MATERIAL con video-yt (clip_kind.py: is_still_clip,
// ffmpeg_trim_window, ffmpeg_trim_filter y ffmpeg_input_args).
//
// Fixtures (tools/fixtures/export_fixtures.py):
//   tests/fixtures/export_input/synthetic.json   export_input_cases.py (20 clips × 7 fps × 10 rutas)
//   tests/fixtures/local/export_input/real.json  tus clips, con el fps de su timeline y su ruta
//
// Exacta: ventanas (reales), textos de los filtros y argumentos. NaN viaja como "nan".

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <limits>
#include <string>

#include "gc/core/export_input.hpp"
#include "gc/core/io.hpp"
#include "gc/core/serialize.hpp"
#include "json_diff.hpp"

using gc::Json;
using namespace gc::core;

namespace {

const std::filesystem::path kFixtures = path_from_utf8(GC_FIXTURES_DIR);

Json num_json(double v) { return std::isnan(v) ? Json("nan") : Json(v); }

std::size_t check_clips(const Json& file) {
    std::size_t n = 0;
    for (const Json& c : file.at("clips")) {
        INFO(c.at("name").get<std::string>());
        auto clip = clip_from_json(c.at("clip"));
        REQUIRE(clip);
        CHECK(is_still_clip(*clip) == c.at("still").get<bool>());
        for (const Json& t : c.at("trim")) {
            const int fps = t.at("fps").get<int>();
            INFO("fps " << fps);
            const auto [a, b] = ffmpeg_trim_window(*clip, fps);
            const auto diff = gc::test::json_diff(t.at("window"), Json::array({num_json(a), num_json(b)}));
            INFO("ffmpeg_trim_window: " << diff.value_or(""));
            CHECK_FALSE(diff);
            CHECK(ffmpeg_trim_filter(*clip, fps) == t.at("filter").get<std::string>());
            ++n;
        }
        for (const Json& in : c.at("input")) {
            const auto path = in.at("path").get<std::string>();
            const int fps = in.at("fps").get<int>();
            INFO("ffmpeg_input_args(" << path << ", " << fps << ")");
            CHECK(Json(ffmpeg_input_args(*clip, path, fps)) == in.at("args"));
            ++n;
        }
    }
    return n;
}

} // namespace

TEST_CASE("paridad de la entrada del export: casos sintéticos", "[parity][export_input]") {
    const auto path = kFixtures / "export_input" / "synthetic.json";
    REQUIRE(std::filesystem::exists(path)); // faltan: ejecuta tools/fixtures/export_fixtures.py
    auto file = read_json_file(path);
    REQUIRE(file);
    CHECK(check_clips(*file) > 500);
}

TEST_CASE("paridad de la entrada del export: tus clips", "[parity][export_input][local]") {
    const auto path = kFixtures / "local" / "export_input" / "real.json";
    if (!std::filesystem::exists(path)) {
        SKIP("Sin fixtures locales: export_fixtures.py --real las genera desde data/projects.json");
    }
    auto file = read_json_file(path);
    REQUIRE(file);
    CHECK(check_clips(*file) > 1000);
}
