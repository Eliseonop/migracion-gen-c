// Paridad de los PERSONAJES STICK con video-yt (stick_library.py).
//
// Fixtures (tools/fixtures/export_fixtures.py):
//   tests/fixtures/stick/synthetic.json   stick_cases.py (en el repo)
//   tests/fixtures/local/stick/real.json  tus sticks, solo lectura (solo local)
//
// Se repiten los pasos sobre una carpeta temporal (o la tuya) y se compara lo que devuelve
// cada uno o, si falla, que falle (y con el mismo mensaje si es uno de los que ve el usuario).

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "disk_steps.hpp"
#include "gc/core/collections.hpp"
#include "gc/core/io.hpp"
#include "gc/core/stick_library.hpp"
#include "json_diff.hpp"

namespace fs = std::filesystem;
using gc::Json;
using namespace gc::core;
using gc::test::replace_all;
using gc::test::text_of;
using gc::test::write_file;

namespace {

const fs::path kFixtures = path_from_utf8(GC_FIXTURES_DIR);

// [ruta relativa, contenido o null si es carpeta] de lo que hay bajo `top`, por ruta.
Json tree_of(const fs::path& top) {
    std::vector<std::pair<std::string, Json>> entries;
    for (const auto& e : fs::recursive_directory_iterator(top)) {
        std::string rel = replace_all(path_to_utf8(fs::relative(e.path(), top)), "\\", "/");
        // read_text() de Python: los CRLF se leen como «\n».
        Json content = e.is_regular_file() ? Json(replace_all(gc::test::read_file(e.path()), "\r\n", "\n")) : Json();
        entries.emplace_back(std::move(rel), std::move(content));
    }
    std::ranges::sort(entries, {}, &std::pair<std::string, Json>::first); // por puntos de código
    Json out = Json::array();
    for (auto& [rel, content] : entries) {
        out.push_back(Json::array({rel, std::move(content)}));
    }
    return out;
}

void check_result(const Json& s, const gc::Result<Json>& r) {
    if (s.contains("error")) {
        INFO("Python falla con " << s.at("error").get<std::string>());
        REQUIRE_FALSE(r);
        if (s.contains("message")) {
            CHECK(r.error().message == s.at("message").get<std::string>());
        }
        return;
    }
    INFO("error: " << (r ? std::string() : r.error().to_string()));
    REQUIRE(r);
    const auto diff = gc::test::json_diff(s.at("result"), *r);
    INFO("resultado: " << diff.value_or(""));
    CHECK_FALSE(diff);
}

std::size_t run_steps(const Json& fixture, const std::string& fname) {
    std::random_device rd;
    const fs::path tmp = fs::temp_directory_path() / ("gc_stick_" + std::to_string(rd()));
    fs::create_directories(tmp);
    const fs::path root = fixture.at("root").is_null() ? tmp : path_from_utf8(fixture.at("root").get<std::string>());
    fs::path default_dir = tmp / "__sin_carpeta__";

    std::size_t n = 0;
    for (const Json& s : fixture.at("steps")) {
        const Json& step = s.at("step");
        INFO(fname << " :: paso " << n << ": " << step.dump().substr(0, 200));
        const auto kind = step[0].get<std::string>();
        const auto base = collections_root(Json::object(), default_dir);
        gc::Result<Json> r = Json();
        const auto files_of = [](const Json& list) {
            std::vector<IncomingFile> files;
            for (const Json& f : list) {
                const std::string bytes = f[1].get<std::string>();
                files.push_back({f[0], [bytes](const fs::path& target) -> gc::Result<void> {
                                     if (!write_file(target, bytes)) {
                                         return gc::fail(gc::ErrorCode::Io, "no se pudo escribir");
                                     }
                                     return {};
                                 }});
            }
            return files;
        };
        if (kind == "files") {
            for (const Json& rel : step[1]) {
                write_file(root / path_from_utf8(rel.get<std::string>()), "x");
            }
        } else if (kind == "file") {
            write_file(root / path_from_utf8(step[1].get<std::string>()), text_of(step[2]));
        } else if (kind == "dirs") {
            for (const Json& rel : step[1]) {
                fs::create_directories(root / path_from_utf8(rel.get<std::string>()));
            }
        } else if (kind == "rm") {
            fs::remove(root / path_from_utf8(step[1].get<std::string>()));
        } else if (kind == "assets") {
            default_dir = root / path_from_utf8(step[1].get<std::string>()) / "material";
        } else if (kind == "list") {
            r = list_sticks(base);
        } else if (kind == "detail") {
            r = stick_detail(base, step[1].get<std::string>());
        } else if (kind == "create") {
            r = create_stick(base, step[1], step[2], step[3]);
        } else if (kind == "add") {
            r = add_stick_files(base, step[1].get<std::string>(), files_of(step[2]), step[3]);
        } else if (kind == "set") {
            r = set_stick_expression(base, step[1].get<std::string>(), step[2], step[3]);
        } else if (kind == "norm") {
            r = Json(stick_norm(step[1]));
        } else if (kind == "slug") {
            r = Json(stick_slug(step[1], step[2].get<std::string>()));
        } else if (kind == "pretty") {
            r = Json(stick_pretty_title(step[1].get<std::string>()));
        } else if (kind == "title") {
            r = Json(detail::py_title(step[1].get<std::string>()));
        } else if (kind == "raw") {
            const fs::path f = root / path_from_utf8(step[1].get<std::string>());
            r = fs::is_regular_file(f) ? Json(gc::test::read_file(f)) : Json();
        } else if (kind == "tree") {
            r = tree_of(root / path_from_utf8(step[1].get<std::string>()));
        } else {
            FAIL("paso desconocido: " << kind);
        }
        check_result(s, r);
        ++n;
    }
    std::error_code ec;
    fs::remove_all(tmp, ec);
    return n;
}

} // namespace

TEST_CASE("paridad de los personajes stick: pasos sintéticos", "[parity][stick]") {
    const auto path = kFixtures / "stick" / "synthetic.json";
    REQUIRE(fs::exists(path)); // faltan: ejecuta tools/fixtures/export_fixtures.py
    auto file = read_json_file(path);
    REQUIRE(file);
    CHECK(run_steps(*file, "synthetic.json") > 200);
}

TEST_CASE("paridad de los personajes stick: los tuyos", "[parity][stick][local]") {
    const auto path = kFixtures / "local" / "stick" / "real.json";
    if (!fs::exists(path)) {
        SKIP("Sin fixtures locales: export_fixtures.py --real las genera desde tu carpeta de material");
    }
    auto file = read_json_file(path);
    REQUIRE(file);
    if (file->at("root").is_null() || !fs::is_directory(path_from_utf8(file->at("root").get<std::string>()))) {
        SKIP("No está tu carpeta de material");
    }
    CHECK(run_steps(*file, "real.json") >= 2);
}
