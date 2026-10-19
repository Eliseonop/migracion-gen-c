// Paridad de los BEATS AUTOMÁTICOS con video-yt (beats.py) sobre señales sintéticas.
//
// Fixture: tests/fixtures/beats/synthetic.json (export_fixtures.py --solo beats, casos de
// beats_cases.py). Se sintetiza la misma señal y se comparan la curva de ataque, el tempo y los
// beats. numpy hace la FFT y el producto por bandas en float32 y aquí en double: la curva puede
// moverse en la sexta cifra (en Linux los beats salen idénticos; se deja ±1 ms por otras libm).

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

#include "gc/core/beat_detect.hpp"
#include "gc/core/io.hpp"

using gc::Json;
using namespace gc::core;

namespace {

// synth de beats_cases.py: ruido LCG y golpes de bombo o charles.
std::vector<float> synth(const Json& c) {
    const auto n = c.at("samples").get<std::size_t>();
    std::vector<double> noise(n);
    auto x = static_cast<std::uint32_t>(c.at("seed").get<std::int64_t>());
    for (double& v : noise) {
        x = 1664525U * x + 1013904223U;
        v = static_cast<double>(x) / 2147483648.0 - 1;
    }
    std::vector<double> y(n);
    const double amp_noise = c.at("noise").get<double>();
    for (std::size_t i = 0; i < n; ++i) {
        y[i] = amp_noise * noise[i];
    }
    for (const Json& hit : c.at("hits")) {
        const auto s = static_cast<std::size_t>(std::nearbyint(hit[0].get<double>() * kBeatSampleRate));
        if (s >= n) {
            continue;
        }
        const bool kick = hit[1] == "kick";
        const double amp = hit[2].get<double>();
        const std::size_t span = std::min(n - s, static_cast<std::size_t>(kBeatSampleRate * (kick ? 0.25 : 0.05)));
        for (std::size_t i = 0; i < span; ++i) {
            const double tau = static_cast<double>(i) / kBeatSampleRate;
            y[s + i] += kick ? amp * std::sin(2 * std::numbers::pi * 60 * tau) * std::exp(-30 * tau)
                             : amp * noise[s + i] * std::exp(-80 * tau);
        }
    }
    return {y.begin(), y.end()};
}

} // namespace

TEST_CASE("paridad de los beats automáticos: señales sintéticas", "[parity][beats]") {
    const auto fixture = read_json_file(path_from_utf8(GC_FIXTURES_DIR) / "beats" / "synthetic.json");
    REQUIRE(fixture); // faltan: ejecuta tools/fixtures/export_fixtures.py --solo beats
    for (const Json& want : *fixture) {
        const Json& c = want.at("case");
        INFO(c.at("name").get<std::string>());
        const std::vector<float> y = synth(c);
        const auto got = detect_beats(y);
        if (want.contains("error")) {
            CHECK_FALSE(got);
            continue;
        }
        REQUIRE(got);
        const std::vector<double> env = onset_envelope(y);
        REQUIRE(env.size() == want.at("env_len").get<std::size_t>());
        for (std::size_t k = 0; k < want.at("env").size(); ++k) {
            CHECK(std::abs(env[k * 7] - want.at("env")[k].get<double>()) < 1e-5);
        }
        CHECK(std::abs(estimate_bpm(env) / want.at("bpm_raw").get<double>() - 1) < 1e-4);
        CHECK(std::abs(got->bpm - want.at("bpm").get<double>()) < 0.11);
        const auto& times = want.at("times");
        REQUIRE(got->times.size() == times.size());
        for (std::size_t k = 0; k < times.size(); ++k) {
            CHECK(std::abs(got->times[k] - times[k].get<double>()) < 0.0015);
        }
    }
}
