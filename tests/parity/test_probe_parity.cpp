// Paridad de las SONDAS DE MEDIOS con video-yt: lo que pregunta a ffprobe
// (sound_design.probe_duration, compose._color_untagged y compose._has_audio).
//
// Solo se compila con FFmpeg (gc_media, presets msvc-vcpkg-*).
// Fixtures (tools/fixtures/export_fixtures.py):
//   tests/fixtures/media/synthetic.json (`probe`)   los medios de tests/fixtures/media/files
//                                                   (media_cases.PROBE_MEDIA; git-lfs), archivos
//                                                   rotos, vacíos y rutas que no existen
//   tests/fixtures/local/media/real.json (`probe`)  una muestra de tus medios, en su sitio
//
// Exacta: la duración pasa por el mismo texto "%f" que imprime ffprobe y el mismo round().
// Ojo: ffprobe (el del PATH de video-yt) y la libavformat de vcpkg deben ser la misma versión.

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

#include "gc/core/io.hpp"
#include "gc/media/probe.hpp"

using gc::Json;
using namespace gc::core;

namespace {

const std::filesystem::path kFixtures = path_from_utf8(GC_FIXTURES_DIR);

std::size_t check_probes(const Json& file, const std::filesystem::path& base, bool skip_missing) {
    gc::media::silence_ffmpeg_log(); // los archivos rotos son a propósito
    std::size_t n = 0;
    for (const Json& c : file.at("probe")) {
        const auto name = c.at("file").get<std::string>();
        const auto path = base.empty() ? path_from_utf8(name) : base / path_from_utf8(name);
        if (skip_missing && !std::filesystem::exists(path)) {
            continue; // un medio tuyo que ya no está
        }
        INFO(name);
        const auto got = gc::media::probe_media(path);
        CHECK(got.duration == c.at("duration").get<double>());
        CHECK(got.color_untagged == c.at("color_untagged").get<bool>());
        CHECK(got.has_audio == c.at("has_audio").get<bool>());
        ++n;
    }
    return n;
}

} // namespace

TEST_CASE("paridad de las sondas de medios: medios sintéticos", "[parity][probe]") {
    const auto path = kFixtures / "media" / "synthetic.json";
    REQUIRE(std::filesystem::exists(path)); // faltan: ejecuta tools/fixtures/export_fixtures.py
    auto file = read_json_file(path);
    REQUIRE(file);
    CHECK(check_probes(*file, kFixtures / "media" / "files", false) > 15);
}

TEST_CASE("paridad de las sondas de medios: tus medios", "[parity][probe][local]") {
    const auto path = kFixtures / "local" / "media" / "real.json";
    if (!std::filesystem::exists(path)) {
        SKIP("Sin fixtures locales: export_fixtures.py --real las genera desde tus proyectos");
    }
    auto file = read_json_file(path);
    REQUIRE(file);
    CHECK(check_probes(*file, {}, true) > 50);
}
