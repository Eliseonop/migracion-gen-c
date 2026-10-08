// Paridad del modelo con video-yt.
//
// Recorre las fixtures que genera tools/fixtures/export_fixtures.py:
//   tests/fixtures/model/*.json        casos sintéticos (en el repo)
//   tests/fixtures/local/model/*.json  tus proyectos reales (solo en tu máquina)
// y exige que gc_core devuelva EXACTAMENTE lo mismo que Python (tipos, valores,
// orden de claves) o falle donde Python falla.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include "gc/core/io.hpp"
#include "gc/core/migrations.hpp"
#include "gc/core/serialize.hpp"
#include "json_diff.hpp"

using gc::Json;
using namespace gc::core;

namespace {

struct FixtureFile {
    std::string name;
    Json cases;
};

std::vector<FixtureFile> load_fixtures(const std::filesystem::path& dir) {
    std::vector<FixtureFile> out;
    if (!std::filesystem::is_directory(dir)) {
        return out;
    }
    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        if (entry.path().extension() != ".json") {
            continue;
        }
        auto json = read_json_file(entry.path());
        REQUIRE(json);
        REQUIRE(json->is_array());
        out.push_back({path_to_utf8(entry.path().filename()), std::move(*json)});
    }
    return out;
}

// Devuelve el dump en modo Model del resultado, o el error.
gc::Result<Json> run(const std::string& kind, const Json& input) {
    if (kind == "project") {
        return project_from_json(input).transform([](const Project& p) { return to_json(p); });
    }
    if (kind == "timeline") {
        return timeline_from_json(input).transform([](const Timeline& t) { return to_json(t); });
    }
    if (kind == "group") {
        return project_group_from_json(input).transform([](const ProjectGroup& g) { return to_json(g); });
    }
    if (kind == "migrate") {
        return migrate_timeline(input);
    }
    FAIL("kind desconocido en la fixture: " << kind);
    return {};
}

void check_file(const FixtureFile& file) {
    for (const Json& c : file.cases) {
        const std::string name = c.at("name").get<std::string>();
        const std::string kind = c.at("kind").get<std::string>();
        INFO(file.name << " :: " << name);
        const auto result = run(kind, c.at("input"));

        if (c.contains("error")) {
            INFO("Python falló con: " << c["error"].get<std::string>());
            REQUIRE_FALSE(result.has_value());
            const auto& paths = c.at("error_paths");
            if (!paths.empty()) {
                INFO("C++ falló en: " << result.error().to_string());
                INFO("Python falló en: " << paths.dump());
                CHECK(std::ranges::find(paths, Json(result.error().path)) != paths.end());
            }
            continue;
        }

        if (!result) {
            FAIL_CHECK("C++ falló y Python no: " << result.error().to_string());
            continue;
        }
        const auto diff = gc::test::json_diff(c.at("expected"), *result);
        INFO(diff.value_or(""));
        CHECK_FALSE(diff);

        // Reescribir con Preserve y volver a leer no cambia el resultado (lo que
        // hará la app al guardar projects.json).
        if (kind == "project") {
            const auto p = project_from_json(c.at("input"));
            const auto reread = project_from_json(to_json(*p, DumpMode::Preserve));
            REQUIRE(reread);
            const auto again = gc::test::json_diff(c.at("expected"), to_json(*reread));
            INFO("tras Preserve: " << again.value_or(""));
            CHECK_FALSE(again);
        }
    }
}

const std::filesystem::path kFixtures = path_from_utf8(GC_FIXTURES_DIR);

} // namespace

TEST_CASE("paridad del modelo: casos sintéticos", "[parity][model]") {
    const auto files = load_fixtures(kFixtures / "model");
    REQUIRE_FALSE(files.empty()); // faltan: ejecuta tools/fixtures/export_fixtures.py
    for (const auto& f : files) {
        check_file(f);
    }
}

TEST_CASE("paridad del modelo: tus proyectos reales", "[parity][model][local]") {
    const auto files = load_fixtures(kFixtures / "local" / "model");
    if (files.empty()) {
        SKIP("Sin fixtures locales: export_fixtures.py --real las genera desde data/projects.json");
    }
    for (const auto& f : files) {
        check_file(f);
    }
}
