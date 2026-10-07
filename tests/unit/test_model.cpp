// Lectura/escritura del modelo: valores por defecto, claves desconocidas, ida y vuelta.

#include <catch2/catch_test_macros.hpp>

#include "gc/core/serialize.hpp"
#include "json_diff.hpp"

using gc::Json;
using gc::core::DumpMode;
using gc::core::project_from_json;
using gc::core::to_json;

namespace {

Json minimal_project() { return {{"id", "p1"}, {"name", "Prueba"}, {"created_at", "2026-10-06T00:00:00+00:00"}}; }

} // namespace

TEST_CASE("proyecto mínimo: valores por defecto de Pydantic", "[model]") {
    const auto p = project_from_json(minimal_project());
    REQUIRE(p);
    CHECK(p->clips.empty());
    CHECK_FALSE(p->timeline);
    CHECK(p->scene_directions == Json::object());

    const Json out = to_json(*p);
    const std::vector<std::string> keys = {"id", "name", "created_at", "folder", "group_id", "clips", "transcripts",
                                           "audios", "images", "timeline", "motion_compositions",
                                           "scene_directions", "visual_blueprint"};
    std::vector<std::string> got;
    for (const auto& [k, v] : out.items()) {
        got.push_back(k);
    }
    CHECK(got == keys);
    CHECK(out["folder"].is_null());
}

TEST_CASE("timeline {} se convierte en la timeline por defecto", "[model]") {
    Json raw = minimal_project();
    raw["timeline"] = Json::object();
    const auto p = project_from_json(raw);
    REQUIRE(p);
    REQUIRE(p->timeline);
    CHECK(p->timeline->schema_version == gc::core::kCurrentSchemaVersion);
    CHECK(p->timeline->fps == 30);
    CHECK(p->timeline->width == 720);
    CHECK(p->timeline->height == 1280);
    CHECK(p->timeline->audio_target_db == -14.0);
}

TEST_CASE("los clips de material sin id toman su index", "[model]") {
    Json raw = minimal_project();
    raw["clips"] = Json::array({{{"index", 7}, {"filename", "a"}, {"url", "u"}, {"start", 0}, {"end", 1}},
                                {{"index", 8}, {"id", "propio"}, {"filename", "b"}, {"url", "u"}, {"start", 0}, {"end", 1}}});
    const auto p = project_from_json(raw);
    REQUIRE(p);
    CHECK(p->clips[0].id == "7");
    CHECK(p->clips[1].id == "propio");
}

TEST_CASE("las claves desconocidas: fuera en Model, conservadas en Preserve", "[model]") {
    Json raw = minimal_project();
    raw["futuro"] = {{"a", 1}};
    raw["timeline"] = {{"schema_version", 5},
                       {"nuevo_en_timeline", true},
                       {"clips", Json::array({{{"id", "c"}, {"track_id", "V1"}, {"kind", "video"},
                                               {"asset_kind", "clips"}, {"asset_id", "0"}, {"filename", "a"},
                                               {"campo_raro", 1.5},
                                               {"reframe", {{"keyframes", Json::array({{{"t", 0}, {"cx", 0.5}, {"x", "y"}}})}}}}})}};
    const auto p = project_from_json(raw);
    REQUIRE(p);

    const Json model = to_json(*p, DumpMode::Model);
    CHECK_FALSE(model.contains("futuro"));
    CHECK_FALSE(model["timeline"].contains("nuevo_en_timeline"));
    CHECK_FALSE(model["timeline"]["clips"][0].contains("campo_raro"));

    const Json preserve = to_json(*p, DumpMode::Preserve);
    CHECK(preserve["futuro"] == Json{{"a", 1}});
    CHECK(preserve["timeline"]["nuevo_en_timeline"] == true);
    CHECK(preserve["timeline"]["clips"][0]["campo_raro"] == 1.5);
    CHECK(preserve["timeline"]["clips"][0]["reframe"]["keyframes"][0]["x"] == "y");
    // Las claves extra van detrás de las del modelo.
    CHECK(preserve.items().begin().key() == "id");
}

TEST_CASE("ida y vuelta: leer lo escrito da lo mismo", "[model]") {
    Json raw = minimal_project();
    raw["timeline"] = {{"clips", Json::array({{{"id", "t"}, {"track_id", "T1"}, {"kind", "text"},
                                               {"asset_kind", "text"}, {"asset_id", "t"}, {"filename", ""},
                                               {"words", Json::array({{{"text", "hola"}, {"start", 0}, {"end", 1}}})},
                                               {"style", {{"font", "Inter"}, {"size", 64}}}}})}};
    raw["otra"] = 1;
    const auto first = project_from_json(raw);
    REQUIRE(first);
    for (const auto mode : {DumpMode::Model, DumpMode::Preserve}) {
        const Json once = to_json(*first, mode);
        const auto second = project_from_json(once);
        REQUIRE(second);
        const auto d = gc::test::json_diff(once, to_json(*second, mode));
        INFO(d.value_or(""));
        CHECK_FALSE(d);
    }
}

TEST_CASE("los errores llevan la ruta del campo", "[model]") {
    Json raw = minimal_project();
    raw["timeline"] = {{"schema_version", 5}, {"clips", Json::array({{{"id", "c"}}})}};
    const auto p = project_from_json(raw);
    REQUIRE_FALSE(p);
    CHECK(p.error().code == gc::ErrorCode::Validation);
    CHECK(p.error().path == "timeline.clips[0].track_id");
    CHECK(p.error().message == "campo obligatorio");

    CHECK_FALSE(project_from_json(Json::array()));
    CHECK_FALSE(project_from_json(Json{{"id", "x"}}));
}
