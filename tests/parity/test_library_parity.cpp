// Paridad de la BIBLIOTECA con video-yt (library.py).
//
// Fixtures (tools/fixtures/export_fixtures.py):
//   tests/fixtures/library/synthetic.json   library_cases.py (en el repo)
//   tests/fixtures/local/library/real.json  tu library.json (solo local)
//
// Se repiten los pasos sobre un data/ temporal y se comparan lo que devuelve cada uno (o
// si falla) y library.json tal como queda. Aparte, material_dto de cada material suelto.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "disk_steps.hpp"
#include "gc/core/ids.hpp"
#include "gc/core/io.hpp"
#include "gc/core/library.hpp"
#include "gc/core/projects_store.hpp"
#include "gc/core/serialize.hpp"
#include "json_diff.hpp"

using gc::Json;
using namespace gc::core;

namespace {

const std::filesystem::path kFixtures = path_from_utf8(GC_FIXTURES_DIR);

struct Outcome {
    bool ok = true;
    Json result;
    std::string message; // del error
};

void check_result(const Json& s, const Outcome& r) {
    const bool ok = r.ok;
    const Json& result = r.result;
    if (s.contains("error")) {
        INFO("Python falla con " << s.at("error").get<std::string>());
        CHECK_FALSE(ok);
        if (s.contains("message")) {
            CHECK(r.message == s.at("message").get<std::string>());
        }
        return;
    }
    CHECK(ok);
    const auto diff = gc::test::json_diff(s.at("result"), result);
    INFO("resultado: " << diff.value_or(""));
    CHECK_FALSE(diff);
}

template <class T>
Outcome take(const gc::Result<T>& r) {
    return r ? Outcome{true, Json(*r), {}} : Outcome{false, Json(), r.error().message};
}

std::size_t run_steps(const Json& fixture, const std::string& fname) {
    auto projects = projects_file_from_json(fixture.at("projects"));
    REQUIRE(projects);
    std::random_device rd;
    const auto dir = std::filesystem::temp_directory_path() / ("gc_library_" + std::to_string(rd()));
    std::filesystem::create_directories(dir);
    LibraryFile lib(dir);
    std::size_t n = 0;
    for (const Json& s : fixture.at("steps")) {
        const Json& step = s.at("step");
        INFO(fname << " :: paso " << n << ": " << step.dump().substr(0, 160));
        const auto kind = step[0].get<std::string>();
        Outcome r;
        if (kind == "write") {
            std::filesystem::create_directories(lib.file().parent_path());
            std::ofstream out(lib.file(), std::ios::binary | std::ios::trunc);
            out << (step[1].is_string() ? step[1].get<std::string>() : step[1].dump());
        } else if (kind == "files") {
            for (const Json& rel : step[1]) {
                const auto f = dir / "library" / path_from_utf8(rel.get<std::string>());
                std::filesystem::create_directories(f.parent_path());
                std::ofstream(f, std::ios::binary) << "x";
            }
        } else if (kind == "dirs") {
            for (const Json& rel : step[1]) {
                std::filesystem::create_directories(dir / "library" / path_from_utf8(rel.get<std::string>()));
            }
        } else if (kind == "exists") {
            r.result = std::filesystem::exists(dir / "library" / path_from_utf8(step[1].get<std::string>()));
        } else if (kind == "load") {
            r.result = lib.load();
        } else if (kind == "list") {
            r = take(lib.list());
        } else if (kind == "update") {
            r = take(lib.update_item(step[1].get<std::string>(), step[2]));
        } else if (kind == "semantic") {
            r = take(lib.set_semantic(step[1].get<std::string>(), step[2]));
        } else if (kind == "unsave") {
            r = take(lib.unsave(step[1].get<std::string>(), *projects));
        }
        check_result(s, r);
        if (s.contains("file_text")) {
            const std::ifstream in(lib.file(), std::ios::binary);
            std::stringstream text;
            text << in.rdbuf();
            CHECK(text.str() == s.at("file_text").get<std::string>());
        } else if (s.at("file").is_null()) {
            CHECK_FALSE(std::filesystem::exists(lib.file()));
        } else {
            auto on_disk = read_json_file(lib.file());
            REQUIRE(on_disk);
            const auto diff = gc::test::json_diff(s.at("file"), *on_disk);
            INFO("library.json: " << diff.value_or(""));
            CHECK_FALSE(diff);
        }
        ++n;
    }
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    return n;
}

} // namespace

TEST_CASE("paridad de la biblioteca: pasos y materiales sintéticos", "[parity][library]") {
    const auto path = kFixtures / "library" / "synthetic.json";
    REQUIRE(std::filesystem::exists(path)); // faltan: ejecuta tools/fixtures/export_fixtures.py
    auto file = read_json_file(path);
    REQUIRE(file);
    CHECK(run_steps(*file, "synthetic.json") > 20);
    for (const Json& d : file->at("dto")) {
        INFO("material_dto(" << d.at("item").dump() << ", " << d.at("scope").get<std::string>() << ")");
        check_result(d, take(material_dto(d.at("item"), d.at("scope").get<std::string>())));
    }
}

TEST_CASE("paridad de la biblioteca: la tuya", "[parity][library][local]") {
    const auto path = kFixtures / "local" / "library" / "real.json";
    if (!std::filesystem::exists(path)) {
        SKIP("Sin fixtures locales: export_fixtures.py --real las genera desde data/library");
    }
    auto file = read_json_file(path);
    REQUIRE(file);
    CHECK(run_steps(*file, "real.json") >= 3);
}

// «Guardar en la biblioteca»: los pasos de library_cases.SAVE_STEPS con save_from_project sobre
// un data/ y unas carpetas de clips temporales, comparando el resultado (o el error y su
// mensaje), library.json, projects.json y lo que queda en disco tras cada paso.
namespace {

std::size_t run_save_steps(const Json& steps) {
    namespace fs = std::filesystem;
    std::random_device rd;
    const fs::path tmp = fs::temp_directory_path() / ("gc_save_" + std::to_string(rd()));
    fs::create_directories(tmp);
    const std::string posix = gc::test::replace_all(path_to_utf8(tmp), "\\", "/");
    ProjectsStore projects(tmp / "data" / "projects.json", tmp / "clips");
    LibraryFile library(tmp / "data");
    SequentialIdSource ids;
    int clock = 0; // el _now() fijado del exportador
    const auto now = [&clock] {
        ++clock;
        return std::format("2026-01-01T{:02d}:{:02d}:{:02d}+00:00", clock / 3600, clock / 60 % 60, clock % 60);
    };
    const auto tmp_of = [&](const Json& v) { return gc::test::replace_in_strings(v, posix, "{TMP}"); };
    const auto read = [&](const fs::path& f) -> Json {
        if (!fs::exists(f)) {
            return nullptr;
        }
        auto j = read_json_file_py(f);
        return j ? *j : Json{{"texto", gc::test::read_file(f)}};
    };
    const fs::path lib_file = tmp / "data" / "library" / "library.json";
    std::size_t n = 0;
    for (const Json& s : steps) {
        const Json& step = s.at("step");
        INFO("paso " << n++ << ": " << step.dump().substr(0, 160));
        const Json a =
            gc::test::replace_in_strings(gc::test::replace_in_strings(step, "{TMP}", posix), "{DRIVE}", posix.substr(0, 2));
        const auto kind = a[0].get<std::string>();
        gc::Result<Json> r = Json();
        if (kind == "projects" || kind == "library") {
            const Json& content = a[1];
            REQUIRE(gc::test::write_file(kind == "projects" ? projects.file() : lib_file,
                                         content.is_string() ? content.get<std::string>()
                                                             : *py_json_dumps(content)));
        } else if (kind == "disk") {
            for (const Json& rel : a[1]) {
                REQUIRE(gc::test::write_file(tmp / path_from_utf8(rel.get<std::string>()), rel.get<std::string>()));
            }
        } else if (kind == "save") {
            r = library.save_from_project(projects, a[1].get<std::string>(), a[2].get<std::string>(),
                                          a[3].get<std::string>(), ids, now);
        } else {
            FAIL("paso desconocido: " << kind);
        }
        if (s.contains("error")) {
            INFO("Python falla con " << s.at("error").get<std::string>());
            REQUIRE_FALSE(r);
            if (s.contains("message")) {
                CHECK(r.error().message == s.at("message").get<std::string>());
            }
        } else {
            INFO("error: " << (r ? std::string() : r.error().to_string()));
            REQUIRE(r);
            const auto diff = gc::test::json_diff(s.at("result"), tmp_of(*r));
            INFO("resultado: " << diff.value_or(""));
            CHECK_FALSE(diff);
        }
        for (const auto& [key, f] : {std::pair{"library", lib_file}, std::pair{"projects", projects.file()}}) {
            const auto diff = gc::test::json_diff(s.at(key), tmp_of(read(f)));
            INFO(key << ": " << diff.value_or(""));
            CHECK_FALSE(diff);
        }
        std::vector<std::pair<std::string, Json>> disk;
        for (const auto& e : fs::recursive_directory_iterator(tmp)) {
            if (e.path() == lib_file || e.path() == projects.file()) {
                continue;
            }
            disk.emplace_back(gc::test::replace_all(path_to_utf8(fs::relative(e.path(), tmp)), "\\", "/"),
                              e.is_regular_file() ? Json(gc::test::read_file(e.path())) : Json());
        }
        std::ranges::sort(disk, {}, &std::pair<std::string, Json>::first);
        Json listed = Json::array();
        for (auto& [rel, content] : disk) {
            listed.push_back(Json::array({rel, std::move(content)}));
        }
        const auto diff = gc::test::json_diff(s.at("disk"), listed);
        INFO("disco: " << diff.value_or(""));
        CHECK_FALSE(diff);
    }
    std::error_code ec;
    fs::remove_all(tmp, ec);
    return n;
}

} // namespace

TEST_CASE("paridad de la biblioteca: guardar desde un proyecto", "[parity][library]") {
    if constexpr (!gc::test::kWindows) {
        SKIP(gc::test::kWindowsPathOnly);
    }
    const auto path = kFixtures / "library" / "synthetic.json";
    REQUIRE(std::filesystem::exists(path));
    auto file = read_json_file(path);
    REQUIRE(file);
    CHECK(run_save_steps(file->at("save")) > 30);
}

TEST_CASE("paridad de la biblioteca: guardar desde una copia de tus proyectos", "[parity][library][local]") {
    const auto path = kFixtures / "local" / "library" / "real.json";
    if (!std::filesystem::exists(path)) {
        SKIP("Sin fixtures locales: export_fixtures.py --real las genera desde tu projects.json");
    }
    auto file = read_json_file(path);
    REQUIRE(file);
    if (!file->contains("save") || file->at("save").empty()) {
        SKIP("Tus proyectos no tienen materiales que guardar");
    }
    CHECK(run_save_steps(file->at("save")) >= 3);
}
