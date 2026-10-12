// Paridad del audio del export EJECUTADO: gc::media::mix_audio frente al CLI de FFmpeg con las
// mismas entradas y el mismo grafo que arma compose.build_command (`-map [aout] -t total -ar 48000`).
//
// Solo con FFmpeg (gc_media, presets msvc-vcpkg-*).
// Fixtures (tools/fixtures/export_fixtures.py):
//   tests/fixtures/audio/mix.json        media_cases.AUDIO_MIX sobre tests/fixtures/media/files: la
//                                        timeline, las entradas y el grafo del build_command real, y
//                                        del audio del CLI: muestras, MD5 y RMS por canal cada 0,1 s
//   tests/fixtures/local/audio/mix.json  tus timelines con audio (RMS cada segundo)
//
// Se compara el número de muestras y el RMS de cada bloque (en dB, hasta 0,001 dB: el FFmpeg de
// vcpkg y el del PATH decodifican el MP3 con diferencias de ~1e-7 dB); el MD5 igual se apunta,
// pero no se exige. Con tus timelines sale el mismo MD5. En los sintéticos, además, el C++ arma las mismas entradas y el mismo
// grafo (core::export_inputs y core::export_audio_graph).

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "gc/core/audio_export.hpp"
#include "gc/core/export_input.hpp"
#include "gc/core/io.hpp"
#include "gc/core/serialize.hpp"
#include "gc/media/audio.hpp"
#include "gc/media/decode.hpp"

using gc::Json;
using namespace gc::core;

namespace {

const std::filesystem::path kFixtures = path_from_utf8(GC_FIXTURES_DIR);
const std::filesystem::path kFiles = kFixtures / "media" / "files";

// La mayor diferencia (en dB) entre el RMS de cada bloque y el de la referencia. Un bloque casi
// en silencio (por debajo de −80 dBFS en los dos) no cuenta.
double worst_block_db(const std::vector<float>& pcm, const Json& ref) {
    const auto block = ref.at("block").get<std::size_t>();
    const std::size_t n = pcm.size() / 2;
    double worst = 0.0;
    std::size_t k = 0;
    for (std::size_t b = 0; b < n; b += block, ++k) {
        const std::size_t e = std::min(n, b + block);
        for (std::size_t c = 0; c < 2; ++c) {
            double sum = 0.0;
            for (std::size_t i = b; i < e; ++i) {
                const double v = pcm[(2 * i) + c];
                sum += v * v;
            }
            const double got = std::sqrt(sum / static_cast<double>(e - b));
            const double want = ref.at("rms").at(k).at(c).get<double>();
            if (got < 1e-4 && want < 1e-4) {
                continue;
            }
            worst = std::max(worst, std::abs(20.0 * std::log10(std::max(got, 1e-12) / std::max(want, 1e-12))));
        }
    }
    return worst;
}

std::vector<std::string> strings(const Json& j) { return j.get<std::vector<std::string>>(); }

void check_mix(const Json& c, const std::vector<std::vector<std::string>>& inputs, double tol_db) {
    const auto graph = strings(c.at("graph"));
    auto pcm = gc::media::mix_audio(inputs, graph, c.at("total").get<double>());
    INFO((pcm ? std::string() : pcm.error().message));
    REQUIRE(pcm);
    CHECK(static_cast<std::int64_t>(pcm->size() / 2) == c.at("samples").get<std::int64_t>());
    const auto bytes = std::span(reinterpret_cast<const std::uint8_t*>(pcm->data()), pcm->size() * sizeof(float));
    const bool same = gc::media::md5_hex(bytes) == c.at("md5").get<std::string>();
    const double worst = worst_block_db(*pcm, c);
    UNSCOPED_INFO(c.at("name").get<std::string>() << ": " << (same ? "mismo MD5" : "otro MD5") << ", peor bloque a "
                                                  << worst << " dB");
    CHECK(worst <= tol_db);
}

} // namespace

TEST_CASE("paridad del audio ejecutado: casos sintéticos frente al CLI", "[parity][audio][media]") {
    const auto path = kFixtures / "audio" / "mix.json";
    REQUIRE(std::filesystem::exists(path)); // faltan: ejecuta tools/fixtures/export_fixtures.py
    auto file = read_json_file(path);
    REQUIRE(file);
    std::size_t cases = 0;
    for (const Json& c : *file) {
        INFO(c.at("name").get<std::string>());
        auto timeline = timeline_from_json(c.at("timeline"));
        REQUIRE(timeline);
        // Las entradas y el grafo del C++ son los del export.
        const auto media = [](const TimelineClip& clip) { return kFiles / path_from_utf8(clip.filename); };
        const auto exists = [&](const TimelineClip& clip) { return std::filesystem::exists(media(clip)); };
        const auto has_audio = [](const TimelineClip&) { return true; };
        auto plan = export_inputs(*timeline, exists, has_audio);
        REQUIRE(plan);
        std::vector<std::vector<std::string>> inputs;
        Json portable = Json::array();
        for (const TimelineClip* clip : plan->files) {
            inputs.push_back(ffmpeg_input_args(*clip, path_to_utf8(media(*clip)), static_cast<int>(timeline->fps)));
            auto p = ffmpeg_input_args(*clip, clip->filename, static_cast<int>(timeline->fps));
            portable.push_back(p);
        }
        CHECK(portable == c.at("inputs"));
        auto graph = export_audio_graph(*timeline, exists, has_audio);
        REQUIRE(graph);
        CHECK(Json(*graph) == c.at("graph"));
        check_mix(c, inputs, 0.001);
        ++cases;
    }
    CHECK(cases >= 5);
}

TEST_CASE("paridad del audio ejecutado: tus timelines frente al CLI", "[parity][audio][media][local]") {
    const auto path = kFixtures / "local" / "audio" / "mix.json";
    if (!std::filesystem::exists(path)) {
        SKIP("Sin fixtures locales: export_fixtures.py --real las genera desde data/projects.json");
    }
    auto file = read_json_file(path);
    REQUIRE(file);
    for (const Json& c : *file) {
        INFO(c.at("name").get<std::string>());
        check_mix(c, c.at("inputs").get<std::vector<std::vector<std::string>>>(), 0.001);
    }
}
