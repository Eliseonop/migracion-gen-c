// Coerciones de tipos al leer el JSON: las mismas que Pydantic v2 en modo lax.

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <string>

#include "gc/core/serialize.hpp"

using gc::Json;
using gc::core::timeline_from_json;

namespace {

Json clip_with(const char* key, Json value) {
    Json c = {{"id", "c"}, {"track_id", "V1"}, {"kind", "video"}, {"asset_kind", "clips"},
              {"asset_id", "0"}, {"filename", "a.mp4"}};
    c[key] = std::move(value);
    return Json{{"schema_version", 5}, {"clips", Json::array({c})}};
}

gc::Result<gc::core::TimelineClip> parse_clip(const char* key, Json value) {
    auto tl = timeline_from_json(clip_with(key, std::move(value)));
    if (!tl) {
        return std::unexpected(tl.error());
    }
    return *tl->clips.at(0);
}

gc::Result<gc::core::Timeline> parse_timeline(const char* key, Json value) {
    Json raw = {{"schema_version", 5}};
    raw[key] = std::move(value);
    return timeline_from_json(raw);
}

} // namespace

TEST_CASE("float acepta enteros, bool y texto numérico", "[coercion]") {
    CHECK(parse_clip("start", 5)->start == 5.0);
    CHECK(parse_clip("start", true)->start == 1.0);
    CHECK(parse_clip("start", "1.5")->start == 1.5);
    CHECK(parse_clip("start", " 1_000.5 ")->start == 1000.5);
    CHECK(parse_clip("start", "1e3")->start == 1000.0);
    CHECK(parse_clip("start", "-2.5E-1")->start == -0.25);
    CHECK(std::isinf(parse_clip("start", "inf")->start));
    CHECK(std::isnan(parse_clip("start", "NaN")->start));
}

TEST_CASE("float rechaza lo que no es número", "[coercion]") {
    CHECK_FALSE(parse_clip("start", "abc"));
    CHECK_FALSE(parse_clip("start", ""));
    CHECK_FALSE(parse_clip("start", "1__0"));
    CHECK_FALSE(parse_clip("start", "_1"));
    CHECK_FALSE(parse_clip("start", "0x10"));
    CHECK_FALSE(parse_clip("start", Json::array()));
    const auto err = parse_clip("start", nullptr);
    REQUIRE_FALSE(err);
    CHECK(err.error().code == gc::ErrorCode::Validation);
    CHECK(err.error().path == "clips[0].start");
}

TEST_CASE("int acepta reales sin decimales y texto entero", "[coercion]") {
    CHECK(parse_timeline("fps", 30)->fps == 30);
    CHECK(parse_timeline("fps", 30.0)->fps == 30);
    CHECK(parse_timeline("fps", "25")->fps == 25);
    CHECK(parse_timeline("fps", " +24 ")->fps == 24);
    CHECK(parse_timeline("fps", "7.0")->fps == 7);
    CHECK(parse_timeline("fps", "1_000")->fps == 1000);
    CHECK(parse_timeline("fps", true)->fps == 1);
    CHECK(parse_timeline("fps", "-9223372036854775808")->fps == INT64_MIN);
}

TEST_CASE("int rechaza decimales y desbordes", "[coercion]") {
    CHECK_FALSE(parse_timeline("fps", 30.5));
    CHECK_FALSE(parse_timeline("fps", "30.5"));
    CHECK_FALSE(parse_timeline("fps", 1e20));
    CHECK_FALSE(parse_timeline("fps", "9223372036854775808"));
    CHECK_FALSE(parse_timeline("fps", "uno"));
}

TEST_CASE("bool acepta 0/1 y las palabras de Pydantic", "[coercion]") {
    CHECK(parse_clip("muted", 1)->muted);
    CHECK_FALSE(parse_clip("muted", 0)->muted);
    CHECK(parse_clip("muted", 1.0)->muted);
    for (const char* yes : {"1", "on", "t", "true", "y", "yes", "TRUE", "Yes"}) {
        CHECK(parse_clip("muted", yes)->muted);
    }
    for (const char* no : {"0", "off", "f", "false", "n", "no", "OFF"}) {
        CHECK_FALSE(parse_clip("muted", no)->muted);
    }
    CHECK_FALSE(parse_clip("muted", 2));
    CHECK_FALSE(parse_clip("muted", 0.5));
    CHECK_FALSE(parse_clip("muted", "2"));
    CHECK_FALSE(parse_clip("muted", "sí"));
}

TEST_CASE("str solo acepta texto", "[coercion]") {
    CHECK(parse_clip("asset_id", "3")->asset_id == "3");
    CHECK_FALSE(parse_clip("asset_id", 3));
    CHECK_FALSE(parse_clip("asset_id", true));
    CHECK_FALSE(parse_clip("asset_id", nullptr));
}

TEST_CASE("los rangos de Field(ge, le) se validan tras convertir", "[coercion]") {
    const auto reframe = [](Json r) { return parse_clip("reframe", std::move(r)); };
    CHECK(reframe({{"zoom", 0.1}}));
    CHECK(reframe({{"zoom", "1"}}));
    CHECK_FALSE(reframe({{"zoom", 0.09}}));
    CHECK_FALSE(reframe({{"crop_w", 0.01}}));
    CHECK(reframe({{"crop_w", nullptr}}));
    const auto err = reframe({{"keyframes", Json::array({{{"t", 0}, {"cx", 1.5}}})}});
    REQUIRE_FALSE(err);
    CHECK(err.error().path == "clips[0].reframe.keyframes[0].cx");
}
