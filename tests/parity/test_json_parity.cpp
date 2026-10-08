// JSON como Python: repr() de los reales, json.dumps y json.loads (tests/fixtures/json/
// synthetic.json, de tools/fixtures/json_cases.py con el CPython de video-yt).

#include <catch2/catch_test_macros.hpp>

#include <bit>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <random>
#include <string>

#include "disk_steps.hpp"
#include "gc/core/io.hpp"
#include "json_diff.hpp"
#include "py.hpp"

namespace fs = std::filesystem;
using gc::Json;
using namespace gc::core;

namespace {

const fs::path kFixtures = path_from_utf8(GC_FIXTURES_DIR);

Json fixture() {
    const auto path = kFixtures / "json" / "synthetic.json";
    REQUIRE(fs::exists(path)); // faltan: ejecuta tools/fixtures/export_fixtures.py
    auto file = read_json_file(path);
    REQUIRE(file);
    return *file;
}

} // namespace

TEST_CASE("py_repr es repr() de los reales de Python", "[json][py]") {
    const Json f = fixture();
    std::size_t n = 0;
    for (const Json& c : f.at("floats")) {
        const auto& hex = c[0].get_ref<const std::string&>();
        std::uint64_t bits = 0;
        std::from_chars(hex.data(), hex.data() + hex.size(), bits, 16);
        const double x = std::bit_cast<double>(bits);
        INFO("bits " << hex);
        CHECK(detail::py_repr(x) == c[1].get<std::string>());
        ++n;
    }
    CHECK(n > 5000);
}

TEST_CASE("py_json_dumps es json.dumps(ensure_ascii=False) de Python", "[json]") {
    const Json f = fixture();
    for (const Json& c : f.at("dumps")) {
        const int indent = c[1].is_null() ? -1 : c[1].get<int>();
        INFO("indent " << indent << ": " << c[2].get<std::string>().substr(0, 120));
        const auto text = py_json_dumps(c[0], indent);
        REQUIRE(text);
        CHECK(*text == c[2].get<std::string>());
    }
}

TEST_CASE("read_json_file_py lee como json.loads: NaN, Infinity y lo que se desborda", "[json]") {
    const Json f = fixture();
    std::random_device rd;
    const fs::path dir = fs::temp_directory_path() / ("gc_json_" + std::to_string(rd()));
    fs::create_directories(dir);
    std::size_t n = 0;
    for (const Json& c : f.at("loads")) {
        const auto& text = c.at("text").get_ref<const std::string&>();
        INFO("texto: " << text);
        const fs::path file = dir / ("c" + std::to_string(n++) + ".json");
        REQUIRE(gc::test::write_file(file, text));
        const auto r = read_json_file_py(file);
        if (c.contains("error")) {
            CHECK_FALSE(r);
            continue;
        }
        REQUIRE(r);
        const auto diff = gc::test::json_diff(c.at("result"), *r);
        INFO("resultado: " << diff.value_or(""));
        CHECK_FALSE(diff);
    }
    std::error_code ec;
    fs::remove_all(dir, ec);
}
