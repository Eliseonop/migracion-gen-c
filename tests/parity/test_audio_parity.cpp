// Paridad del AUDIO del export con video-yt: las cadenas de FFmpeg de cada clip que suena y la
// mezcla (audio_fx.py, clip_keyframes.ffmpeg_envelope y volume_filter, clip_speed y el bloque de
// audio de compose.build_command).
//
// Fixtures (tools/fixtures/export_fixtures.py):
//   tests/fixtures/audio/synthetic.json   audio_cases.py: clips sueltos, nodos, atempo, intensidades,
//                                         pesos de las etapas y timelines enteras
//   tests/fixtures/local/audio/real.json  tus clips (los que no son texto) y tus timelines
//
// Exactas: los textos tienen que coincidir carácter a carácter. Las timelines se comparan con la
// parte de audio del filter_complex REAL de build_command (lo que va tras «[vout]»), con el
// material simulado: por id de clip, si existe y si tiene audio, lo que vio Python.

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

#include "gc/core/audio_export.hpp"
#include "gc/core/io.hpp"
#include "gc/core/serialize.hpp"

using gc::Json;
using namespace gc::core;

namespace {

const std::filesystem::path kFixtures = path_from_utf8(GC_FIXTURES_DIR);

double num(const Json& j) {
    if (j.is_string()) {
        const std::string& s = j.get_ref<const std::string&>();
        return s == "nan" ? std::numeric_limits<double>::quiet_NaN()
                          : (s == "-inf" ? -1 : 1) * std::numeric_limits<double>::infinity();
    }
    return j.get<double>();
}

Json strings(const std::vector<std::string>& v) {
    Json out = Json::array();
    for (const auto& s : v) {
        out.push_back(s);
    }
    return out;
}

std::size_t check_clips(const Json& file, const std::string& fname) {
    std::size_t n = 0;
    for (const Json& c : file.at("clips")) {
        INFO(fname << " :: " << c.at("name").get<std::string>());
        auto clip = clip_from_json(c.at("clip"));
        REQUIRE(clip);
        for (const auto& [key, want] : c.at("envelopes").items()) {
            INFO("ffmpeg_envelope(" << key << ")");
            const auto got = ffmpeg_envelope(*clip, key, key == "volume" ? 1.0 : 0.25);
            CHECK((got ? Json(*got) : Json()) == want);
        }
        CHECK(volume_filter(*clip) == c.at("volume_filter").get<std::string>());
        const AudioFxGraph g = audio_fx_graph(*clip, "ain0", "ax0");
        CHECK(strings(g.steps) == c.at("fx").at("steps"));
        CHECK(g.out == c.at("fx").at("out").get<std::string>());
        CHECK(audio_speed_filters(*clip) == c.at("speed").get<std::string>());
        ++n;
    }
    return n;
}

std::size_t check_timelines(const Json& file, const std::string& fname) {
    std::size_t n = 0;
    for (const Json& t : file.at("timelines")) {
        INFO(fname << " :: " << t.at("name").get<std::string>());
        auto timeline = timeline_from_json(t.at("timeline"));
        REQUIRE(timeline);
        const Json& exists = t.at("exists");
        const Json& audio = t.at("has_audio");
        const auto lookup = [](const Json& dict) {
            return [&dict](const TimelineClip& c) { return dict.value(c.id, true); };
        };
        const auto graph = export_audio_graph(*timeline, lookup(exists), lookup(audio));
        REQUIRE(graph);
        CHECK(strings(*graph) == t.at("graph"));
        ++n;
    }
    return n;
}

} // namespace

TEST_CASE("paridad del audio del export: casos sintéticos", "[parity][audio]") {
    const auto path = kFixtures / "audio" / "synthetic.json";
    REQUIRE(std::filesystem::exists(path)); // faltan: ejecuta tools/fixtures/export_fixtures.py
    auto file = read_json_file(path);
    REQUIRE(file);

    CHECK(check_clips(*file, "synthetic.json") >= 20);
    CHECK(check_timelines(*file, "synthetic.json") >= 10);

    const auto catalog = audio_fx_catalog();
    REQUIRE(file->at("catalog").size() == catalog.size());
    std::vector<std::string> nodes;
    for (std::size_t i = 0; i < catalog.size(); ++i) {
        const Json& want = file->at("catalog")[i];
        CHECK(std::string(catalog[i].id) == want.at("id").get<std::string>());
        CHECK(std::string(catalog[i].label) == want.at("label").get<std::string>());
        CHECK(std::string(catalog[i].group) == want.at("group").get<std::string>());
        for (const auto& stage : catalog[i].stages) {
            for (const AudioNode& node : stage) {
                nodes.push_back(ffmpeg_node(node));
            }
        }
    }
    CHECK(strings(nodes) == file->at("nodes"));

    for (const Json& a : file->at("atempo")) {
        INFO("atempo_chain(" << a.at("speed").dump() << ")");
        CHECK(atempo_chain(a.at("speed").get<double>()) == a.at("chain").get<std::string>());
    }
    for (const Json& i : file->at("intensity")) {
        INFO("fx_intensity(" << i.at("value").dump() << ")");
        CHECK(Json(fx_intensity(i.at("value"))) == i.at("k"));
    }
    for (const Json& w : file->at("weights")) {
        INFO("stage_weights(" << w.at("k").dump() << ", " << w.at("n").dump() << ")");
        CHECK(Json(stage_weights(num(w.at("k")), w.at("n").get<int>())) == w.at("w"));
    }
}

TEST_CASE("paridad del audio del export: tus clips y timelines", "[parity][audio][local]") {
    const auto path = kFixtures / "local" / "audio" / "real.json";
    if (!std::filesystem::exists(path)) {
        SKIP("Sin fixtures locales: export_fixtures.py --real las genera desde data/projects.json");
    }
    auto file = read_json_file(path);
    REQUIRE(file);
    CHECK(check_clips(*file, "real.json") > 250);
    CHECK(check_timelines(*file, "real.json") > 10);
}
