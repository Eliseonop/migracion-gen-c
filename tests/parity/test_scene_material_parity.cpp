// Paridad de la DIRECCIÓN DE ESCENA con video-yt, la parte del material (app/scene_direction.py y
// key_terms y _strip_accents de app/motion/segment_context.py).
//
// Fixture: tests/fixtures/scene/material.json (export_fixtures.py --solo escena, casos de scene_cases.py):
// key_terms y _strip_accents de cada texto, normalize_semantic y semantic_blob de cada valor, el catálogo
// de cada proyecto sin biblioteca, con ella y con un library.json roto, rank_materials y los pasos de
// set_material_meta sobre un projects.json y un library.json temporales.

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <thread>

#include "gc/core/io.hpp"
#include "gc/core/library.hpp"
#include "gc/core/projects_store.hpp"
#include "gc/core/scene_direction.hpp"
#include "gc/core/segment_context.hpp"
#include "json_diff.hpp"
#include "py.hpp"

using gc::Json;
namespace scene = gc::core::scene;

namespace {

Json load_fixture() {
    auto file = gc::core::read_json_file(gc::core::path_from_utf8(GC_FIXTURES_DIR) / "scene" / "material.json");
    REQUIRE(file); // faltan: export_fixtures.py --solo escena
    return std::move(*file);
}

void same(const Json& expected, const Json& actual, const std::string& what) {
    const auto diff = gc::test::json_diff(expected, actual);
    INFO(what);
    INFO(diff.value_or(""));
    CHECK_FALSE(diff);
}

// El resultado o el error de Python: un LookupError es NotFound y un ValueError InvalidArgument, con su
// texto; el resto (TypeError) un crash.
void check(const Json& c, const gc::Result<Json>& got, const std::string& what) {
    INFO(what);
    if (c.contains("error")) {
        REQUIRE_FALSE(got);
        INFO(got.error().message);
        if (c["lookup"].get<bool>() || c["value_error"].get<bool>()) {
            CHECK(got.error().code ==
                  (c["lookup"].get<bool>() ? gc::ErrorCode::NotFound : gc::ErrorCode::InvalidArgument));
            CHECK(got.error().message == c["error"].get<std::string>());
        } else {
            CHECK(got.error().code == gc::ErrorCode::Internal);
        }
        return;
    }
    if (!got) {
        INFO(got.error().message);
        REQUIRE(got);
    }
    same(c["result"], *got, what);
}

gc::Result<Json> string_json(const gc::Result<std::string>& r) {
    if (!r) {
        return std::unexpected(r.error());
    }
    return Json(*r);
}

std::filesystem::path temp_dir(const char* name) {
    const auto tmp = std::filesystem::temp_directory_path() /
                     (std::string(name) + std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())));
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(tmp / "data" / "library");
    return tmp;
}

void write_json(const std::filesystem::path& file, const Json& value) {
    auto text = gc::core::py_json_dumps(value);
    REQUIRE(text);
    std::ofstream(file, std::ios::binary) << *text;
}

std::string text_of(const Json& v) { return v.is_string() ? v.get<std::string>() : std::string(); }

} // namespace

TEST_CASE("paridad de la dirección de escena: términos clave y quitar tildes", "[parity][scene]") {
    const Json f = load_fixture();
    REQUIRE(f["key_terms"].size() == 31);
    for (const Json& c : f["key_terms"]) {
        const auto got = gc::core::key_terms(text_of(c["text"]), c["limit"].get<std::size_t>());
        same(c["result"], Json(got), "key_terms " + c["text"].dump());
    }
    REQUIRE(f["strip_accents"].size() == 20);
    for (const Json& c : f["strip_accents"]) {
        INFO(c["text"].dump());
        CHECK(gc::core::detail::py_strip_accents(c["text"].get<std::string>()) == c["result"].get<std::string>());
    }
}

TEST_CASE("paridad de la dirección de escena: metadata semántica", "[parity][scene]") {
    const Json f = load_fixture();
    REQUIRE(f["semantics"].size() == 16);
    for (const Json& c : f["semantics"]) {
        check(c, scene::normalize_semantic(c["raw"]), "normalize_semantic " + c["raw"].dump());
    }
    std::size_t errors = 0;
    for (const Json& c : f["blobs"]) {
        check(c, string_json(scene::semantic_blob(c["raw"])), "semantic_blob " + c["raw"].dump());
        errors += c.contains("error") ? 1 : 0;
    }
    CHECK(errors == 4);
}

TEST_CASE("paridad de la dirección de escena: catálogo, ranking y guardar la metadata", "[parity][scene]") {
    const Json f = load_fixture();
    const auto tmp = temp_dir("gc_scene_material_parity_");
    const auto data = tmp / "data";
    write_json(data / "projects.json", Json{{"projects", f["projects"]}});
    gc::core::ProjectsStore store(data / "projects.json", tmp / "clips");
    gc::core::LibraryFile library(data);
    const auto project = [&](const Json& pid) -> gc::core::Project {
        auto p = store.get_project(pid.get<std::string>());
        REQUIRE(p);
        REQUIRE(*p);
        return std::move(**p);
    };
    const auto catalogs = [&](const Json& expected, const std::string& what) {
        for (const Json& raw : f["projects"]) {
            check(expected[raw["id"].get<std::string>()], scene::material_catalog(project(raw["id"]), library),
                  what + " " + raw["id"].get<std::string>());
        }
    };
    catalogs(f["catalog_without_library"], "catálogo sin biblioteca");
    write_json(data / "library" / "library.json", f["library"]);
    catalogs(f["catalog"], "catálogo");

    const auto catalog = scene::material_catalog(project(Json("m-main")), library);
    REQUIRE(catalog);
    std::size_t errors = 0;
    for (const Json& c : f["ranks"]) {
        const Json& cat = c.contains("catalog") ? c["catalog"] : *catalog;
        check(c, scene::rank_materials(cat, c["text"].get<std::string>(), c["limit"].get<std::size_t>()),
              "rank_materials " + c["text"].dump());
        errors += c.contains("error") ? 1 : 0;
    }
    CHECK(errors == 2);

    std::ofstream(data / "library" / "library.json", std::ios::binary) << "{roto";
    catalogs(f["catalog_broken_library"], "catálogo con library.json roto");
    write_json(data / "library" / "library.json", f["library"]);

    errors = 0;
    for (const Json& c : f["steps"]) {
        const Json& a = c["args"];
        const std::string what = "set_material_meta " + a.dump();
        check(c,
              scene::set_material_meta(store, library, a[0].get<std::string>(), a[1], a[2].get<std::string>(), a[3],
                                       a[4].get<std::string>()),
              what);
        errors += c.contains("error") ? 1 : 0;
        Json saved;
        for (const Json& p : store.load()["projects"]) {
            if (p["id"] == "m-main") {
                saved = p;
            }
        }
        same(c["project"], saved, what + " (projects.json)");
        auto lib = gc::core::read_json_file(data / "library" / "library.json");
        REQUIRE(lib);
        same(c["library"], *lib, what + " (library.json)");
        check(c["catalog"], scene::material_catalog(project(Json("m-main")), library), what + " (catálogo)");
    }
    CHECK(errors == 8);
    std::error_code ec;
    std::filesystem::remove_all(tmp, ec);
}
