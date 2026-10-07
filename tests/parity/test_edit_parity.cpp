// Paridad del motor de edición con timeline_store.apply_op de video-yt.
//
// Fixtures (tools/fixtures/export_fixtures.py):
//   tests/fixtures/edit/synthetic.json        casos de ops_cases.py (en el repo)
//   tests/fixtures/local/edit/real.json       operaciones sobre tus timelines (solo local)
//
// Cada caso: timeline de partida + operación + parámetros → timeline, changed y
// warnings EXACTOS (o error donde Python da error). Los ids generados coinciden
// porque Python usa la misma secuencia que SequentialIdSource.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "gc/core/edit.hpp"
#include "gc/core/fx_catalog.hpp"
#include "gc/core/io.hpp"
#include "gc/core/serialize.hpp"
#include "gc/core/sfx_library.hpp"
#include "json_diff.hpp"

using gc::Json;
using namespace gc::core;

namespace {

const std::filesystem::path kFixtures = path_from_utf8(GC_FIXTURES_DIR);

// Timeline esperada a partir de la de partida + lo que cambió (fixtures reales).
Json rebuild(const Json& base, const Json& delta) {
    std::unordered_map<std::string, const Json*> by_id;
    for (const Json& c : base.at("clips")) {
        by_id[c.at("id").get<std::string>()] = &c;
    }
    const Json& changed = delta.at("clips");
    Json clips = Json::array();
    for (const Json& id : delta.at("clip_ids")) {
        const auto key = id.get<std::string>();
        const auto it = changed.find(key);
        clips.push_back(it != changed.end() ? *it : *by_id.at(key));
    }
    Json out = Json::object();
    for (const Json& key : delta.at("keys")) {
        const auto k = key.get<std::string>();
        out[k] = k == "clips" ? clips : delta.at("meta").at(k);
    }
    return out;
}

// Biblioteca de SFX con la que se grabó el caso (las recetas la consultan).
void use_sfx_library(const Json& lib) {
    auto library = std::make_shared<SfxLibrary>();
    if (lib.is_object()) {
        for (const Json& it : lib.at("items")) {
            library->items.push_back(it);
        }
        for (const auto& [id, dur] : lib.at("durations").items()) {
            library->durations[id] = dur.get<double>();
        }
    }
    set_sfx_library(std::move(library));
}

std::vector<std::string> strings(const Json& j) {
    std::vector<std::string> out;
    for (const Json& s : j) {
        out.push_back(s.get<std::string>());
    }
    return out;
}

struct Stats {
    std::size_t ok = 0;
    std::size_t errors = 0;
};

// El catálogo de efectos es un recurso de la app (resources/fx/catalog.json).
void load_catalog() {
    auto catalog = FxCatalog::load(path_from_utf8(GC_RESOURCES_DIR) / "fx" / "catalog.json");
    REQUIRE(catalog);
    set_fx_catalog(std::make_shared<const FxCatalog>(std::move(*catalog)));
}

Stats check_file(const std::filesystem::path& path, bool check_immutable) {
    load_catalog();
    auto file = read_json_file(path);
    REQUIRE(file);
    const std::string fname = path_to_utf8(path.filename());

    std::map<std::string, Timeline> timelines;
    std::map<std::string, Json> timeline_json;
    for (const auto& [id, raw] : file->at("timelines").items()) {
        auto tl = timeline_from_json(raw);
        INFO(fname << " :: timeline " << id << ": " << (tl ? "" : tl.error().to_string()));
        REQUIRE(tl);
        timeline_json[id] = to_json(*tl);
        timelines.emplace(id, std::move(*tl));
    }

    Stats stats;
    for (const Json& c : file->at("cases")) {
        const auto name = c.at("name").get<std::string>();
        const auto base_id = c.at("timeline").get<std::string>();
        INFO(fname << " :: " << name);
        const Timeline& base = timelines.at(base_id);

        use_sfx_library(c.contains("sfx_library") ? c.at("sfx_library") : file->value("sfx_library", Json()));
        SequentialIdSource ids;
        auto command = parse_command(c.at("op").get<std::string>(), c.at("params"));
        const gc::Result<EditResult> result =
            command ? apply_command(base, *command, ids) : gc::Result<EditResult>(std::unexpected(command.error()));

        if (check_immutable) {
            const auto diff = gc::test::json_diff(timeline_json.at(base_id), to_json(base));
            INFO("la operación modificó su entrada: " << diff.value_or(""));
            CHECK_FALSE(diff);
        }

        if (c.contains("error")) {
            INFO("Python falló con: " << c.at("error").get<std::string>());
            CHECK_FALSE(result.has_value());
            ++stats.errors;
            continue;
        }
        if (!result) {
            FAIL_CHECK("C++ falló y Python no: " << result.error().to_string());
            continue;
        }
        const Json& expected = c.at("expected");
        const Json want = expected.contains("timeline") ? expected.at("timeline")
                                                        : rebuild(timeline_json.at(base_id), expected.at("timeline_delta"));
        const auto diff = gc::test::json_diff(want, to_json(result->timeline));
        INFO(diff.value_or(""));
        CHECK_FALSE(diff);

        CHECK(result->changed == strings(expected.at("changed")));

        // Python recorre las pistas magnéticas en el orden de un set (aleatorio entre
        // ejecuciones); con esos avisos se compara sin orden.
        auto got = result->warnings;
        auto exp = strings(expected.at("warnings"));
        const bool unordered = std::ranges::any_of(exp, [](const std::string& w) {
            return w.starts_with("pista magnética");
        });
        if (unordered) {
            std::ranges::sort(got);
            std::ranges::sort(exp);
        }
        CHECK(got == exp);
        ++stats.ok;
    }
    return stats;
}

} // namespace

TEST_CASE("paridad de edición: casos sintéticos", "[parity][edit]") {
    const auto path = kFixtures / "edit" / "synthetic.json";
    REQUIRE(std::filesystem::exists(path)); // faltan: ejecuta tools/fixtures/export_fixtures.py
    const Stats s = check_file(path, true);
    CHECK(s.ok > 40);
    CHECK(s.errors > 20);
}

TEST_CASE("paridad de edición: operaciones sobre tus timelines reales", "[parity][edit][local]") {
    const auto path = kFixtures / "local" / "edit" / "real.json";
    if (!std::filesystem::exists(path)) {
        SKIP("Sin fixtures locales: export_fixtures.py --real las genera desde data/projects.json");
    }
    const Stats s = check_file(path, false);
    CHECK(s.ok > 100);
}
