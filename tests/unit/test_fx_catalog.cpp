// FxCatalog::resolve_params (resolve_params de fx_catalog.py): defectos, rangos y catálogos rotos.

#include <catch2/catch_test_macros.hpp>

#include <utility>

#include "gc/core/fx_catalog.hpp"

using gc::Json;
using gc::core::FxCatalog;

namespace {

Json manifest(Json params) { return Json{{"id", "fx"}, {"params", std::move(params)}}; }

} // namespace

TEST_CASE("resolve_params: defectos, rangos acotados y conmutadores", "[fx]") {
    const Json m = manifest(Json::array({
        {{"id", "amt"}, {"type", "range"}, {"min", 0}, {"max", 1}, {"def", 0.5}},
        {{"id", "on"}, {"type", "toggle"}, {"def", false}},
    }));
    const auto r = FxCatalog::resolve_params(m, Json{{"amt", 2}, {"on", 1}});
    REQUIRE(r);
    CHECK(*r == Json{{"amt", 1.0}, {"on", true}});
    CHECK(*FxCatalog::resolve_params(m, Json{{"amt", "0.25"}}) == Json{{"amt", 0.25}, {"on", false}});
    CHECK(*FxCatalog::resolve_params(m, Json{{"amt", "x"}}) == Json{{"amt", 0.5}, {"on", false}});
    CHECK(*FxCatalog::resolve_params(m, Json()) == Json{{"amt", 0.5}, {"on", false}});
}

TEST_CASE("resolve_params: un catálogo roto es un error, no un fallo de memoria", "[fx]") {
    // range sin min: Python lanza KeyError al acotar un valor; sin valor, devuelve el defecto.
    const Json no_min = manifest(Json::array({{{"id", "amt"}, {"type", "range"}, {"max", 1}, {"def", 0.5}}}));
    const auto bad = FxCatalog::resolve_params(no_min, Json{{"amt", 0.7}});
    REQUIRE_FALSE(bad);
    CHECK(bad.error().code == gc::ErrorCode::Internal);
    CHECK(*FxCatalog::resolve_params(no_min, Json()) == Json{{"amt", 0.5}});
    // max que no es un número (float("x") → ValueError).
    const Json bad_max =
        manifest(Json::array({{{"id", "amt"}, {"type", "range"}, {"min", 0}, {"max", "x"}, {"def", 0.5}}}));
    CHECK_FALSE(FxCatalog::resolve_params(bad_max, Json{{"amt", 0.7}}));
    // Parámetro sin id de texto (p["id"]: KeyError).
    CHECK_FALSE(FxCatalog::resolve_params(manifest(Json::array({{{"type", "toggle"}}})), Json()));
    CHECK_FALSE(FxCatalog::resolve_params(manifest(Json::array({{{"id", 3}, {"type", "toggle"}}})), Json()));
}
