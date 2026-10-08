// Paridad de los AJUSTES con video-yt (settings.py, export_settings.py, transcribe_settings.py).
//
// Fixtures (tools/fixtures/export_fixtures.py):
//   tests/fixtures/settings/synthetic.json   settings_cases.py (claves falsas; en el repo)
//   tests/fixtures/local/settings/real.json  tus ajustes con las claves redactadas (solo local)
//
// Se repiten los pasos sobre un settings.json temporal y, tras cada uno, se comparan lo
// que devuelve (o si falla) y el archivo tal como queda.

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>

#include "gc/core/io.hpp"
#include "gc/core/settings.hpp"
#include "json_diff.hpp"

using gc::Json;
using namespace gc::core;

namespace {

const std::filesystem::path kFixtures = path_from_utf8(GC_FIXTURES_DIR);

template <class T>
Json to_result(const gc::Result<T>& r) {
    return r ? Json(*r) : Json();
}

std::size_t run_steps(const Json& steps, const std::string& fname) {
    std::random_device rd;
    const auto dir = std::filesystem::temp_directory_path() / ("gc_settings_" + std::to_string(rd()));
    std::filesystem::create_directories(dir);
    const auto file = dir / "settings.json";
    SettingsFile settings(file);
    std::size_t n = 0;
    for (const Json& s : steps) {
        const Json& step = s.at("step");
        INFO(fname << " :: paso " << n << ": " << step.dump().substr(0, 200));
        const auto kind = step[0].get<std::string>();
        bool ok = true;
        Json result;
        if (kind == "write") {
            std::ofstream out(file, std::ios::binary | std::ios::trunc);
            out << (step[1].is_string() ? step[1].get<std::string>() : step[1].dump());
        } else if (kind == "load") {
            result = settings.load();
        } else if (kind == "public") {
            const auto r = settings.public_view();
            ok = r.has_value();
            result = to_result(r);
        } else if (kind == "save") {
            const auto r = settings.save(step[1]);
            ok = r.has_value();
            result = to_result(r);
        } else if (kind == "api_key") {
            result = settings.api_key(step[1].get<std::string>());
        } else if (kind == "keys_for") {
            const auto r = settings.keys_for(step[1].get<std::string>());
            ok = r.has_value();
            result = to_result(r);
        } else if (kind == "add_key") {
            const auto r = settings.add_key(step[1].get<std::string>(), step[2].get<std::string>());
            ok = r.has_value();
            result = to_result(r);
        } else if (kind == "set_key") {
            const auto r = settings.set_key(step[1].get<std::string>(), step[2].get<std::int64_t>(),
                                            step[3].get<std::string>());
            ok = r.has_value();
            result = to_result(r);
        } else if (kind == "remove_key") {
            const auto r = settings.remove_key(step[1].get<std::string>(), step[2].get<std::int64_t>());
            ok = r.has_value();
            result = to_result(r);
        }
        if (s.contains("error")) {
            INFO("Python falla con " << s.at("error").get<std::string>());
            CHECK_FALSE(ok);
        } else {
            CHECK(ok);
            const auto diff = gc::test::json_diff(s.at("result"), result);
            INFO("resultado: " << diff.value_or(""));
            CHECK_FALSE(diff);
        }
        // El archivo tal como queda.
        if (s.contains("file_text")) {
            const std::ifstream in(file, std::ios::binary);
            std::stringstream text;
            text << in.rdbuf();
            CHECK(text.str() == s.at("file_text").get<std::string>());
        } else if (s.at("file").is_null()) {
            CHECK_FALSE(std::filesystem::exists(file));
        } else {
            auto on_disk = read_json_file(file);
            REQUIRE(on_disk);
            const auto diff = gc::test::json_diff(s.at("file"), *on_disk);
            INFO("archivo: " << diff.value_or(""));
            CHECK_FALSE(diff);
        }
        ++n;
    }
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    return n;
}

} // namespace

TEST_CASE("paridad de los ajustes: pasos sintéticos", "[parity][settings]") {
    const auto path = kFixtures / "settings" / "synthetic.json";
    REQUIRE(std::filesystem::exists(path)); // faltan: ejecuta tools/fixtures/export_fixtures.py
    auto file = read_json_file(path);
    REQUIRE(file);
    CHECK(run_steps(file->at("steps"), "synthetic.json") > 50);
}

TEST_CASE("paridad de los ajustes: los tuyos, con las claves redactadas", "[parity][settings][local]") {
    const auto path = kFixtures / "local" / "settings" / "real.json";
    if (!std::filesystem::exists(path)) {
        SKIP("Sin fixtures locales: export_fixtures.py --real las genera desde data/settings.json");
    }
    auto file = read_json_file(path);
    REQUIRE(file);
    CHECK(run_steps(file->at("steps"), "real.json") >= 3);
}
