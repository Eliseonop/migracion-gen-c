// Paridad del AUDIO MONO de los beats y la envolvente: gc::media::decode_mono frente al ffmpeg del
// PATH con el comando que arman beats.decode_mono (`-i x -t 1200.0 -ac 1 -ar 22050`) y
// clip_motion.sample_rms_envelope (`-ss s -t d -i x -ac 1 -ar 400`), y gc::media::detect_beats y
// gc::media::sample_rms_envelope frente a lo que devuelven esas funciones de video-yt.
//
// Solo con FFmpeg (gc_media, presets msvc-vcpkg-*).
// Fixtures (tools/fixtures/export_fixtures.py --solo mono [--real]):
//   tests/fixtures/audio/mono.json        media_cases.MONO_BEATS y MONO_RMS sobre tests/fixtures/media/files
//   tests/fixtures/local/audio/mono.json  tus audios: los beats de cada archivo y la envolvente de cada
//                                         clip que se solapa con otro (rutas absolutas)
//
// Del audio se comparan el número de muestras y el RMS de cada décima de segundo (hasta 0,001 dB,
// como la mezcla: el FFmpeg de vcpkg y el del PATH pueden decodificar con diferencias de ~1e-7);
// el MD5 se apunta. Los beats, con ±1,5 ms y ±0,11 BPM, como la paridad de beats.py, salvo que en
// hasta el 1 % de los beats se admite un fotograma de análisis (23,2 ms): numpy calcula la curva
// de ataque en float32 y el C++ en double, y en una pista larga eso cambia algún empate.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "gc/core/io.hpp"
#include "gc/media/audio.hpp"
#include "gc/media/decode.hpp"

using gc::Json;
using namespace gc::core;

namespace {

const std::filesystem::path kFixtures = path_from_utf8(GC_FIXTURES_DIR);
const std::filesystem::path kFiles = kFixtures / "media" / "files";

struct Counts {
    std::size_t same_md5 = 0;
    std::size_t decoded = 0;
};

// Los argumentos de entrada (-ss y -t antes de -i), la frecuencia y el -t de la salida del comando
// grabado, con la ruta de verdad en vez del nombre.
struct MonoCommand {
    std::vector<std::string> input;
    int rate = 0;
    std::optional<double> max_seconds;
};

MonoCommand parse_command(const Json& cmd, const std::string& path) {
    MonoCommand out;
    bool after_input = false;
    for (std::size_t i = 0; i + 1 < cmd.size(); ++i) {
        const auto a = cmd[i].get<std::string>();
        const auto v = cmd[i + 1].get<std::string>();
        if (a == "-i") {
            out.input.insert(out.input.end(), {"-i", path});
            after_input = true;
            ++i;
        } else if ((a == "-ss" || a == "-t") && !after_input) {
            out.input.insert(out.input.end(), {a, v});
            ++i;
        } else if (a == "-t") {
            out.max_seconds = std::stod(v);
            ++i;
        } else if (a == "-ar") {
            out.rate = std::stoi(v);
            ++i;
        }
    }
    return out;
}

// La mayor diferencia (en dB) entre el RMS de cada bloque y el de la referencia; los bloques casi
// en silencio (por debajo de −80 dBFS en los dos) no cuentan.
double worst_block_db(const std::vector<float>& pcm, const Json& ref) {
    const auto block = ref.at("block").get<std::size_t>();
    double worst = 0.0;
    std::size_t k = 0;
    for (std::size_t b = 0; b < pcm.size(); b += block, ++k) {
        const std::size_t e = std::min(pcm.size(), b + block);
        double sum = 0.0;
        for (std::size_t i = b; i < e; ++i) {
            sum += static_cast<double>(pcm[i]) * pcm[i];
        }
        const double got = std::sqrt(sum / static_cast<double>(e - b));
        const double want = ref.at("rms").at(k).get<double>();
        if (got < 1e-4 && want < 1e-4) {
            continue;
        }
        worst = std::max(worst, std::abs(20.0 * std::log10(std::max(got, 1e-12) / std::max(want, 1e-12))));
    }
    return worst;
}

// El audio mono del C++ frente al del CLI grabado en el caso `c`. Devuelve si sale el mismo MD5.
bool check_decode(const Json& c, const std::string& path, Counts& counts) {
    if (!c.contains("cmd")) {
        return false; // Python falló antes de llamar a ffmpeg (el archivo no existe)
    }
    const MonoCommand cmd = parse_command(c.at("cmd"), path);
    auto pcm = gc::media::decode_mono(cmd.input, cmd.rate, cmd.max_seconds);
    if (c.at("returncode").get<std::int64_t>() != 0) {
        CHECK_FALSE(pcm); // el CLI falla (sin audio)
        return false;
    }
    INFO((pcm ? std::string() : pcm.error().message));
    REQUIRE(pcm);
    REQUIRE(pcm->size() == c.at("samples").get<std::size_t>());
    const auto bytes = std::span(reinterpret_cast<const std::uint8_t*>(pcm->data()), pcm->size() * sizeof(float));
    const bool same = gc::media::md5_hex(bytes) == c.at("md5").get<std::string>();
    counts.same_md5 += same ? 1 : 0;
    ++counts.decoded;
    CHECK(worst_block_db(*pcm, c) <= 0.001);
    return same;
}

void check_beats(const Json& c, const std::filesystem::path& path) {
    auto got = gc::media::detect_beats(path);
    if (c.contains("error")) {
        CHECK_FALSE(got);
        return;
    }
    INFO((got ? std::string() : got.error().message));
    REQUIRE(got);
    const Json& want = c.at("out");
    CHECK(std::abs(got->bpm - want.at("bpm").get<double>()) < 0.11);
    const auto& times = want.at("times");
    REQUIRE(got->times.size() == times.size());
    // numpy hace la FFT y las bandas en float32 y el C++ en double: con el mismo audio, en una pista
    // larga la programación dinámica puede quedarse con el fotograma de al lado en algún beat.
    constexpr double kFrame = static_cast<double>(kBeatHop) / kBeatSampleRate;
    std::size_t moved = 0;
    for (std::size_t k = 0; k < times.size(); ++k) {
        const double diff = std::abs(got->times[k] - times[k].get<double>());
        CHECK(diff < kFrame + 0.0015);
        moved += diff >= 0.0015 ? 1 : 0;
    }
    CHECK(moved <= times.size() / 100);
}

void check_rms(const Json& c, const std::filesystem::path& path) {
    auto got = gc::media::sample_rms_envelope(path, c.at("src_start").get<double>(), c.at("duration").get<double>(),
                                              c.at("rate").get<int>());
    if (c.contains("error")) {
        CHECK_FALSE(got);
        return;
    }
    INFO((got ? std::string() : got.error().message));
    REQUIRE(got);
    const Json& want = c.at("out");
    REQUIRE(got->size() == want.size());
    for (std::size_t k = 0; k < want.size(); ++k) {
        CHECK((*got)[k].first == want[k][0].get<double>());
        CHECK(std::abs((*got)[k].second - want[k][1].get<double>()) < 1e-4);
    }
}

} // namespace

TEST_CASE("paridad del audio mono: beats y envolvente sobre los medios de prueba", "[parity][audio][media]") {
    const auto file = read_json_file(kFixtures / "audio" / "mono.json");
    REQUIRE(file); // faltan: ejecuta tools/fixtures/export_fixtures.py --solo mono
    Counts counts;
    for (const Json& c : file->at("beats")) {
        const auto path = kFiles / path_from_utf8(c.at("file").get<std::string>());
        INFO("beats de " << c.at("file").get<std::string>());
        INFO("mismo MD5 que el CLI: " << check_decode(c, path_to_utf8(path), counts));
        check_beats(c, path);
    }
    for (const Json& c : file->at("rms")) {
        const auto path = kFiles / path_from_utf8(c.at("file").get<std::string>());
        INFO("RMS de " << c.at("file").get<std::string>() << " desde " << c.at("src_start").get<double>());
        INFO("mismo MD5 que el CLI: " << check_decode(c, path_to_utf8(path), counts));
        check_rms(c, path);
    }
    WARN(counts.same_md5 << " de " << counts.decoded << " con el mismo MD5 que el CLI");
    CHECK(counts.decoded >= 15);
}

TEST_CASE("paridad del audio mono: tus audios", "[parity][audio][media][local]") {
    const auto path = kFixtures / "local" / "audio" / "mono.json";
    if (!std::filesystem::exists(path)) {
        SKIP("sin tests/fixtures/local/audio/mono.json (export_fixtures.py --solo mono --real)");
    }
    const auto file = read_json_file(path);
    REQUIRE(file);
    Counts counts;
    for (const Json& c : file->at("beats")) {
        INFO("beats de " << c.at("name").get<std::string>());
        const auto media = path_from_utf8(c.at("path").get<std::string>());
        INFO("mismo MD5 que el CLI: " << check_decode(c, c.at("path").get<std::string>(), counts));
        check_beats(c, media);
    }
    for (const Json& c : file->at("rms")) {
        INFO("RMS de " << c.at("name").get<std::string>());
        const auto media = path_from_utf8(c.at("path").get<std::string>());
        INFO("mismo MD5 que el CLI: " << check_decode(c, c.at("path").get<std::string>(), counts));
        check_rms(c, media);
    }
    WARN(counts.same_md5 << " de " << counts.decoded << " con el mismo MD5 que el CLI");
    CHECK(counts.decoded > 0);
}
