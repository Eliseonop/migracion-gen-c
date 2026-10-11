// Paridad del DECODE con el export de video-yt: los fotogramas que da la entrada de cada clip
// (ffmpeg_input_args) con la cadena del export (`{color_pre}{trim},setpts=PTS-STARTPTS`).
//
// Solo se compila con FFmpeg (gc_media, presets msvc-vcpkg-*).
// Fixture (tools/fixtures/export_fixtures.py → synthetic_decode_fixtures):
//   tests/fixtures/media/decode.json   media_cases.DECODE sobre tests/fixtures/media/files: la
//                                      entrada y la cadena que arman las funciones REALES de
//                                      video-yt y un MD5 por fotograma de lo que da el ffmpeg
//                                      del PATH con `-vf "<cadena>,format=rgba" -f rawvideo`.
//
// Exacta: la misma entrada, la misma cadena y los mismos píxeles (MD5) fotograma a fotograma.

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <vector>

#include "gc/core/io.hpp"
#include "gc/core/serialize.hpp"
#include "gc/media/decode.hpp"
#include "gc/media/probe.hpp"

using gc::Json;
using namespace gc::core;

namespace {

const std::filesystem::path kFixtures = path_from_utf8(GC_FIXTURES_DIR);

} // namespace

TEST_CASE("paridad del decode: la entrada y la cadena del export", "[parity][decode]") {
    const auto path = kFixtures / "media" / "decode.json";
    REQUIRE(std::filesystem::exists(path)); // faltan: ejecuta tools/fixtures/export_fixtures.py
    auto file = read_json_file(path);
    REQUIRE(file);
    gc::media::silence_ffmpeg_log();
    std::size_t frames = 0;
    for (const Json& c : *file) {
        INFO(c.at("name").get<std::string>());
        auto clip = clip_from_json(c.at("clip"));
        REQUIRE(clip);
        const auto media = kFixtures / "media" / "files" / path_from_utf8(c.at("file").get<std::string>());
        const auto spec = gc::media::export_decode_spec(*clip, media, c.at("fps").get<int>());
        CHECK(spec.chain == c.at("chain").get<std::string>());
        std::vector<std::string> args = spec.input_args;
        for (auto& a : args) {
            if (a == path_to_utf8(media)) {
                a = "{PATH}";
            }
        }
        CHECK(Json(args) == c.at("input_args"));

        std::vector<std::string> got;
        const auto n = gc::media::decode_frames(spec, [&](const gc::media::RgbaFrame& f) {
            CHECK(f.width == c.at("width").get<int>());
            CHECK(f.height == c.at("height").get<int>());
            got.push_back(gc::media::md5_hex(f.pixels));
            return true;
        });
        REQUIRE(n);
        CHECK(Json(got) == c.at("frames"));
        frames += got.size();
    }
    CHECK(frames > 150);
}
