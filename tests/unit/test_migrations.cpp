// Migraciones del JSON de la timeline (espejo de backend/app/migrations.py).

#include <catch2/catch_test_macros.hpp>

#include "gc/core/migrations.hpp"
#include "gc/core/model.hpp"

using gc::Json;
using gc::core::migrate_timeline;
using gc::core::resolve_text_role;
using gc::core::timeline_version;

TEST_CASE("versión declarada de la timeline", "[migrations]") {
    CHECK(timeline_version(Json::object()) == 1);
    CHECK(timeline_version(Json{{"schema_version", 3}}) == 3);
    CHECK(timeline_version(Json{{"schema_version", 0}}) == 1);
    CHECK(timeline_version(Json{{"schema_version", -2}}) == 1);
    CHECK(timeline_version(Json{{"schema_version", 5.0}}) == 1); // un real no es int en Python
    CHECK(timeline_version(Json{{"schema_version", "5"}}) == 1);
    CHECK(timeline_version(Json{{"schema_version", true}}) == 1);
    CHECK(timeline_version(Json::array()) == 1);
}

TEST_CASE("rol de los clips de texto", "[migrations]") {
    CHECK_FALSE(resolve_text_role(Json{{"kind", "video"}}));
    CHECK(resolve_text_role(Json{{"kind", "text"}}) == "free");
    CHECK(resolve_text_role(Json{{"kind", "text"}, {"text_role", "free"}, {"origin", {{"a", 1}}}}) == "free");
    CHECK(resolve_text_role(Json{{"kind", "text"}, {"origin", {{"a", 1}}}}) == "caption");
    CHECK(resolve_text_role(Json{{"kind", "text"}, {"origin", Json::object()}}) == "free");
    CHECK(resolve_text_role(Json{{"kind", "text"}, {"words", Json::array({1})}}) == "caption");
    CHECK(resolve_text_role(Json{{"kind", "text"}, {"text_role", "otro"}}) == "free");
}

TEST_CASE("v1 → v5 completa la timeline", "[migrations]") {
    const Json tl = {
        {"tracks", Json::array({{{"id", "T1"}, {"kind", "text"}}, {{"id", "V1"}, {"kind", "video"}}})},
        {"clips", Json::array({{{"kind", "text"}, {"words", Json::array({1})}}, {{"keep_pitch", false}}})},
    };
    const Json out = migrate_timeline(tl);
    CHECK(out["schema_version"] == gc::core::kCurrentSchemaVersion);
    CHECK(out["clips"][0]["text_role"] == "caption");
    CHECK(out["clips"][1]["keep_pitch"] == true);
    CHECK(out["tracks"][0]["id"] == "V1");
    CHECK(out["tracks"][1]["id"] == "T1");
    // La entrada no se toca.
    CHECK(tl["tracks"][0]["id"] == "T1");
}

TEST_CASE("migrar es idempotente y no degrada", "[migrations]") {
    const Json v1 = {{"clips", Json::array({{{"kind", "text"}}})}};
    const Json once = migrate_timeline(v1);
    CHECK(migrate_timeline(once) == once);

    const Json future = {{"schema_version", 99}, {"clips", Json::array({{{"keep_pitch", false}}})}};
    CHECK(migrate_timeline(future) == future);
    CHECK(migrate_timeline(Json::object()) == Json::object());
    CHECK(migrate_timeline(Json(nullptr)).is_null());
}

TEST_CASE("v3 → v4 solo cambia keep_pitch false (bool)", "[migrations]") {
    const Json out = migrate_timeline(
        Json{{"schema_version", 3}, {"clips", Json::array({{{"keep_pitch", false}}, {{"keep_pitch", 0}}})}});
    CHECK(out["clips"][0]["keep_pitch"] == true);
    CHECK(out["clips"][1]["keep_pitch"] == 0);
}

TEST_CASE("v4 → v5 reordena vídeo y texto en sus huecos; el audio no se mueve", "[migrations]") {
    const Json tracks = Json::array({{{"id", "T1"}, {"kind", "text"}},
                                     {{"id", "A1"}, {"kind", "audio"}},
                                     {{"id", "V1"}, {"kind", "video"}},
                                     {{"id", "T2"}, {"kind", "text"}},
                                     {{"id", "V2"}, {"kind", "video"}}});
    const Json out = migrate_timeline(Json{{"schema_version", 4}, {"tracks", tracks}});
    std::vector<std::string> ids;
    for (const auto& t : out["tracks"]) {
        ids.push_back(t["id"].get<std::string>());
    }
    CHECK(ids == std::vector<std::string>{"V1", "A1", "V2", "T1", "T2"});
}
