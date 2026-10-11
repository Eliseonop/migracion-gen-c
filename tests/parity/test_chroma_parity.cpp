// Paridad del CROMA con video-yt: las cadenas de FFmpeg de clip_bg.py (chroma_filters y
// chroma_alpha_ffmpeg) sobre el croma normalizado.
//
// Fixture (tools/fixtures/export_fixtures.py → synthetic_chroma_fixtures):
//   tests/fixtures/media/chroma.json   media_cases.CHROMA con los altos de CHROMA_HEIGHTS (0 = el
//                                      512 que usa compose.py cuando la sonda no da alto).

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <string>

#include "gc/core/bg.hpp"
#include "gc/core/io.hpp"

using gc::Json;
using namespace gc::core;

TEST_CASE("paridad del croma: las cadenas de FFmpeg de clip_bg.py", "[parity][chroma]") {
    const auto path = path_from_utf8(GC_FIXTURES_DIR) / "media" / "chroma.json";
    REQUIRE(std::filesystem::exists(path)); // faltan: ejecuta tools/fixtures/export_fixtures.py
    auto file = read_json_file(path);
    REQUIRE(file);
    std::size_t cases = 0;
    for (const Json& c : *file) {
        INFO(c.at("raw").dump());
        const Json chroma = normalize_bg(Json{{"chroma", c.at("raw")}})["chroma"];
        CHECK(chroma == c.at("chroma"));
        CHECK(Json(chroma_filters(chroma)) == c.at("filters"));
        for (const Json& h : c.at("heights")) {
            const auto height = h.at("height").get<std::int64_t>();
            INFO("alto " << height);
            CHECK(chroma_alpha_ffmpeg(chroma, height > 0 ? height : 512) == h.at("alpha").get<std::string>());
        }
        ++cases;
    }
    CHECK(cases >= 16);
}

TEST_CASE("croma: la cadena del clip, sola o con el alfa aparte", "[chroma]") {
    const Json off = normalize_bg(Json{{"chroma", {{"enabled", false}}}})["chroma"];
    CHECK(chroma_source_chain(off, 1080).empty());
    const Json plain = normalize_bg(Json{{"chroma", {{"enabled", true}}}})["chroma"];
    CHECK(chroma_source_chain(plain, 1080) ==
          "format=yuva444p,chromakey=color=0x00FF00:similarity=0.200000:blend=0.100000");
    const Json edged = normalize_bg(Json{{"chroma", {{"enabled", true}, {"edge", 0.3}}}})["chroma"];
    CHECK(chroma_source_chain(edged, 1080) ==
          "format=yuva444p,chromakey=color=0x00FF00:similarity=0.200000:blend=0.100000,format=gbrap,"
          "split=2[bgcka][bgckb];[bgcka]alphaextract,gblur=sigma=9.7200,lutyuv=y=clip(val+-38\\,0\\,255)"
          "[bgckalpha];[bgckb][bgckalpha]alphamerge");
}
