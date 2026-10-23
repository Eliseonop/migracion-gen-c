// Paridad de las URLs con Python 3.14: urlparse, hostname, parse_qsl y unquote de
// urllib.parse (py_url.hpp), y los enlaces de YouTube de youtube_audio.py (youtube_url.hpp).
//
// Fixture: tests/fixtures/url/synthetic.json (export_fixtures.py --solo url, url_cases.py).

#include <catch2/catch_test_macros.hpp>

#include <vector>

#include "gc/core/asset_import.hpp"
#include "gc/core/io.hpp"
#include "gc/core/youtube_url.hpp"
#include "py_url.hpp"

using gc::Json;
using namespace gc::core;
using namespace gc::core::detail;

namespace {

Json opt(const std::optional<std::string>& s) { return s ? Json(*s) : Json(); }

} // namespace

TEST_CASE("paridad de las URLs: urllib.parse y los enlaces de YouTube", "[parity][url]") {
    const auto fixture = read_json_file(path_from_utf8(GC_FIXTURES_DIR) / "url" / "synthetic.json");
    REQUIRE(fixture); // faltan: ejecuta tools/fixtures/export_fixtures.py --solo url
    const auto specials = nfkc_url_specials();
    CHECK(Json(std::vector<char32_t>(specials.begin(), specials.end())) == fixture->at("nfkc"));
    for (const Json& c : fixture->at("parse")) {
        const auto u = c[0].get<std::string>();
        INFO("urlparse(" << c[0].dump() << ")");
        const auto p = py_urlparse(u);
        if (c[1].is_null()) {
            CHECK_FALSE(p);
            continue;
        }
        REQUIRE(p);
        const Json got{p->scheme, p->netloc, p->path, p->params, p->query, p->fragment, opt(py_url_hostname(p->netloc))};
        CHECK(got == c[1]);
    }
    for (const Json& c : fixture->at("youtube")) {
        const auto u = c[0].get<std::string>();
        INFO("youtube(" << c[0].dump() << ")");
        CHECK(is_youtube_url(u) == c[1].get<bool>());
        CHECK(opt(youtube_id_from_url(u)) == c[2]);
    }
    for (const Json& c : fixture->at("unquote")) {
        const auto s = c[0].get<std::string>();
        INFO("unquote(" << c[0].dump() << ")");
        CHECK(py_unquote(s) == c[1].get<std::string>());
        CHECK(py_unquote_plus(s) == c[2].get<std::string>());
    }
    for (const Json& c : fixture->at("qsl")) {
        INFO("parse_qsl(" << c[0].dump() << ")");
        Json got = Json::array();
        for (const auto& [k, v] : py_parse_qsl(c[0].get<std::string>())) {
            got.push_back({k, v});
        }
        CHECK(got == c[1]);
    }
    for (const Json& c : fixture->at("download")) { // asset_import.allowed_download_url
        INFO("allowed_download_url(" << c[0].dump() << ")");
        const auto r = allowed_download_url(c[0].get<std::string>());
        if (c[1].is_null()) {
            CHECK_FALSE(r);
        } else {
            REQUIRE(r);
            CHECK(*r == c[1].get<bool>());
        }
    }
}
