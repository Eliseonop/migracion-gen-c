// Paridad del SEEK EXACTO con video-yt: el fotograma de un instante como
// frame_grab.extract_frame (`-ss {max(0, t):.3f}` antes de `-i` y `-frames:v 1`), sin su
// escalado ni el JPEG, que son presentación.
//
// Solo se compila con FFmpeg (gc_media, presets msvc-vcpkg-*).
// Fixture (tools/fixtures/export_fixtures.py → synthetic_seek_fixtures):
//   tests/fixtures/media/seek.json   media_cases.SEEK sobre tests/fixtures/media/files: el MD5 en
//                                    RGBA de lo que da el ffmpeg del PATH, o null si no sale nada
//                                    (un instante después del último fotograma).

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <limits>
#include <string>

#include "gc/core/io.hpp"
#include "gc/media/decode.hpp"
#include "gc/media/probe.hpp"

using gc::Json;
using namespace gc::core;

namespace {

const std::filesystem::path kFixtures = path_from_utf8(GC_FIXTURES_DIR);

double num(const Json& j) {
    return j.is_string() ? std::numeric_limits<double>::quiet_NaN() : j.get<double>(); // "nan"
}

} // namespace

TEST_CASE("paridad del seek exacto: el fotograma de un instante", "[parity][decode][seek]") {
    const auto path = kFixtures / "media" / "seek.json";
    REQUIRE(std::filesystem::exists(path)); // faltan: ejecuta tools/fixtures/export_fixtures.py
    auto file = read_json_file(path);
    REQUIRE(file);
    gc::media::silence_ffmpeg_log();
    std::size_t cases = 0;
    for (const Json& c : *file) {
        const auto name = c.at("file").get<std::string>();
        const double t = num(c.at("t"));
        INFO(name << " en " << c.at("t").dump() << " (-ss " << c.at("ss").get<std::string>() << ")");
        const auto got = gc::media::frame_at(kFixtures / "media" / "files" / path_from_utf8(name), t);
        REQUIRE(got);
        if (c.at("frame").is_null()) {
            CHECK_FALSE(got->has_value());
        } else {
            REQUIRE(got->has_value());
            CHECK((*got)->width == c.at("width").get<int>());
            CHECK((*got)->height == c.at("height").get<int>());
            CHECK(gc::media::md5_hex((*got)->pixels) == c.at("frame").get<std::string>());
        }
        ++cases;
    }
    CHECK(cases > 40);
}

TEST_CASE("seek exacto: un instante que no cabe en µs es un error, no un seek con basura", "[decode][seek]") {
    gc::media::silence_ffmpeg_log();
    // ffmpeg -ss 1e300 da «Invalid duration» y extract_frame lanza ValueError.
    CHECK_FALSE(gc::media::frame_at(kFixtures / "media" / "files" / "v_untagged.mp4", 1e300));
}
