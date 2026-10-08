// Paridad de la BIBLIOTECA con video-yt (library.py).
//
// Fixtures (tools/fixtures/export_fixtures.py):
//   tests/fixtures/library/synthetic.json   library_cases.py (en el repo)
//   tests/fixtures/local/library/real.json  tu library.json (solo local)
//
// Se repiten los pasos sobre un data/ temporal y se comparan lo que devuelve cada uno (o
// si falla) y library.json tal como queda. Aparte, material_dto de cada material suelto.

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>

#include "gc/core/io.hpp"
#include "gc/core/library.hpp"
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
