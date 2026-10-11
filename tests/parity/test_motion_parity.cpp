// Paridad de la ENVOLVENTE de un sonido con video-yt (clip_motion.py: downsample_envelope,
// normalize_amps, overlap_window, audio_src_at y el núcleo de sample_rms_envelope).
//
// Fixtures (tools/fixtures/export_fixtures.py):
//   tests/fixtures/motion/synthetic.json   motion_cases.py (en el repo); el RMS, con un ffmpeg
//                                          falso que devuelve las muestras del caso
//   tests/fixtures/local/motion/real.json  tus timelines (todos los pares visual/audio) y el RMS
//                                          de tus audios con las muestras del ffmpeg REAL
//
// Exactas, bit a bit: solo hay sumas, productos, divisiones, sqrt() y round(), todas
// correctamente redondeadas. NaN e infinito van como texto ("nan", "inf", "-inf").

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

#include "gc/core/clip_motion.hpp"
#include "gc/core/io.hpp"
#include "gc/core/serialize.hpp"
#include "json_diff.hpp"

using gc::Json;
using namespace gc::core;

namespace {

const std::filesystem::path kFixtures = path_from_utf8(GC_FIXTURES_DIR);

// NaN e infinito como texto, igual que la fixture (_nan_safe del exportador).
Json nan_safe(const Json& j) {
    if (j.is_number_float() && !std::isfinite(j.get<double>())) {
        const double v = j.get<double>();
        return std::isnan(v) ? "nan" : (v > 0 ? "inf" : "-inf");
    }
    if (j.is_structured()) {
        Json out = j;
        for (auto& v : out) {
            v = nan_safe(v);
        }
        return out;
    }
    return j;
}

double num(const Json& j) {
    if (j.is_string()) {
        const std::string& s = j.get_ref<const std::string&>();
        return s == "nan" ? std::numeric_limits<double>::quiet_NaN()
                          : (s == "-inf" ? -1 : 1) * std::numeric_limits<double>::infinity();
    }
    return j.get<double>();
}

Envelope env_of(const Json& j) {
    Envelope out;
    for (const Json& p : j) {
        out.emplace_back(num(p.at(0)), num(p.at(1)));
    }
    return out;
}

Json env_json(const Envelope& env) {
    Json out = Json::array();
    for (const auto& [t, a] : env) {
        out.push_back(Json::array({t, a}));
    }
    return nan_safe(out);
}

void check_same(const std::string& what, const Json& want, const Json& got) {
    const auto diff = gc::test::json_diff(want, got);
    INFO(what << ": " << diff.value_or(""));
    CHECK_FALSE(diff);
}

// Pares (visual, audio) de cada timeline: ventana de solape y audio_src_at.
std::size_t check_timelines(const Json& file) {
    std::size_t pairs = 0;
    for (const Json& tl : file.at("timelines")) {
        INFO(tl.at("name").get<std::string>());
        std::vector<TimelineClip> clips;
        for (const Json& c : tl.at("clips")) {
            auto clip = clip_from_json(c);
            REQUIRE(clip);
            clips.push_back(std::move(*clip));
        }
        for (const Json& p : tl.at("pairs")) {
            const TimelineClip& v = clips.at(p.at("v").get<std::size_t>());
            const TimelineClip& a = clips.at(p.at("a").get<std::size_t>());
            INFO("visual " << v.id << ", audio " << a.id);
            const auto window = overlap_window(v, a);
            check_same("overlap_window", p.at("window"),
                       window ? nan_safe(Json::array({window->first, window->second})) : Json(nullptr));
            for (const Json& s : p.at("src")) {
                const double t = num(s.at(0));
                check_same("audio_src_at(" + s.at(0).dump() + ")", s.at(1), nan_safe(Json(audio_src_at(a, t))));
            }
            ++pairs;
        }
    }
    return pairs;
}

std::size_t check_rms(const Json& file) {
    std::size_t n = 0;
    for (const Json& c : file.at("rms")) {
        std::vector<float> samples;
        for (const Json& s : c.at("samples")) {
            samples.push_back(static_cast<float>(num(s)));
        }
        INFO((c.contains("name") ? c.at("name").get<std::string>() : std::string("sintético")) << ": "
             << samples.size() << " muestras, duración " << c.at("duration").dump() << ", rate " << c.at("rate"));
        check_same("rms_envelope", c.at("out"),
                   env_json(rms_envelope(samples, num(c.at("duration")), c.at("rate").get<int>())));
        ++n;
    }
    return n;
}

} // namespace

TEST_CASE("paridad de la envolvente: casos sintéticos", "[parity][motion]") {
    const auto path = kFixtures / "motion" / "synthetic.json";
    REQUIRE(std::filesystem::exists(path)); // faltan: ejecuta tools/fixtures/export_fixtures.py
    auto file = read_json_file(path);
    REQUIRE(file);

    std::size_t errors = 0;
    for (const Json& d : file->at("downsample")) {
        const int max_n = d.at("max_n").get<int>();
        INFO("downsample_envelope(" << d.at("points").size() << " puntos, " << max_n << ")");
        const auto got = downsample_envelope(env_of(d.at("points")), max_n);
        if (d.contains("error")) {
            CHECK_FALSE(got);
            ++errors;
        } else {
            REQUIRE(got);
            check_same("downsample_envelope", d.at("out"), env_json(*got));
        }
    }
    CHECK(file->at("downsample").size() > 100);
    CHECK(errors > 5);

    for (const Json& d : file->at("normalize")) {
        INFO("normalize_amps(" << d.at("points").dump() << ")");
        check_same("normalize_amps", d.at("out"), env_json(normalize_amps(env_of(d.at("points")))));
    }
    CHECK(file->at("normalize").size() > 15);

    CHECK(check_timelines(*file) > 50);
    CHECK(check_rms(*file) >= 30);
}

TEST_CASE("paridad de la envolvente: tus timelines y tus audios", "[parity][motion][local]") {
    const auto path = kFixtures / "local" / "motion" / "real.json";
    if (!std::filesystem::exists(path)) {
        SKIP("Sin fixtures locales: export_fixtures.py --real las genera desde data/projects.json");
    }
    auto file = read_json_file(path);
    REQUIRE(file);
    CHECK(check_timelines(*file) > 1000);
    CHECK(check_rms(*file) > 0);
}
