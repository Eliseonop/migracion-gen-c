// Paridad de las PALABRAS CLAVE LOCALES de «Explorar» con video-yt (explore_keywords.py, sin IA).
//
// Fixture: tests/fixtures/keywords/synthetic.json (export_fixtures.py --solo keywords, casos de
// keyword_cases.py).

#include <catch2/catch_test_macros.hpp>

#include "gc/core/io.hpp"
#include "gc/core/keywords.hpp"
#include "gc/core/serialize.hpp"

using gc::Json;
using namespace gc::core;

TEST_CASE("paridad de las palabras clave de Explorar: sin IA", "[parity][keywords]") {
    const auto fixture = read_json_file(path_from_utf8(GC_FIXTURES_DIR) / "keywords" / "synthetic.json");
    REQUIRE(fixture); // faltan: ejecuta tools/fixtures/export_fixtures.py --solo keywords
    for (const Json& c : fixture->at("extract")) {
        INFO("extract_keywords(" << c[0].dump() << ", " << c[1] << ")");
        CHECK(Json(extract_keywords(c[0].get<std::string>(), c[1].get<std::size_t>())) == c[2]);
    }
    for (const Json& c : fixture->at("parse")) {
        INFO("parse_keyword_list(" << c[0].dump() << ")");
        CHECK(Json(parse_keyword_list(c[0].get<std::string>())) == c[1]);
    }
    for (const Json& c : fixture->at("gather")) {
        const auto project = project_from_json(c[0]);
        REQUIRE(project);
        CHECK(gather_theme_text(*project) == c[1].get<std::string>());
    }
    for (const Json& c : fixture->at("suggest")) {
        INFO("suggest_keywords(" << c[0].dump() << ")");
        CHECK(suggest_local_keywords(c[0].get<std::string>()) == c[1]);
    }
}
