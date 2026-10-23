// Paridad de lo que decide fx_bake.py de video-yt antes de hornear los efectos de vídeo: qué
// efectos aplica un clip (active_vfx, has_vfx), su margen (pad_frac, pad_allowed) y el clip que
// compone el export con el horneado (_remap_reframe, baked_clip).
//
// Fixture: tests/fixtures/fx/bake.json (export_fixtures.py --solo fx_bake, vfx_cases.BAKE_CLIPS).

#include <catch2/catch_test_macros.hpp>

#include <memory>

#include "gc/core/fx_bake.hpp"
#include "gc/core/fx_catalog.hpp"
#include "gc/core/io.hpp"
#include "gc/core/serialize.hpp"
#include "json_diff.hpp"

using gc::Json;
using namespace gc::core;

TEST_CASE("paridad de fx_bake: efectos aplicables, margen y clip horneado", "[parity][fx]") {
    auto catalog = FxCatalog::load(path_from_utf8(GC_RESOURCES_DIR) / "fx" / "catalog.json");
    REQUIRE(catalog);
    set_fx_catalog(std::make_shared<FxCatalog>(std::move(*catalog)));
    const auto fixture = read_json_file(path_from_utf8(GC_FIXTURES_DIR) / "fx" / "bake.json");
    REQUIRE(fixture); // faltan: export_fixtures.py --solo fx_bake
    for (const Json& c : *fixture) {
        INFO("clip " << c["clip"]["id"]);
        auto clip = clip_from_json(c["clip"]);
        REQUIRE(clip);
        auto got = active_vfx(*clip);
        if (c.contains("error")) { // int() de Python
            INFO("Python: " << c["error"]);
            CHECK_FALSE(got);
            CHECK_FALSE(has_vfx(*clip));
            continue;
        }
        REQUIRE(got);
        const Json& active = *got;
        auto diff = gc::test::json_diff(c["active"], active);
        INFO("active_vfx: " << diff.value_or(""));
        CHECK_FALSE(diff);
        const auto has = has_vfx(*clip);
        REQUIRE(has);
        CHECK(*has == c["has"].get<bool>());
        CHECK(vfx_pad_fraction(active) == c["pad_frac"].get<double>());
        for (const Json& p : c["pad_allowed"]) {
            INFO("pad_allowed " << p.dump());
            CHECK(vfx_pad_allowed(*clip, p[0].get<double>(), p[1].get<double>(), p[2].get<double>(),
                                  p[3].get<double>()) == p[4].get<bool>());
        }
        const auto rf = remap_reframe_to_local(*clip);
        diff = gc::test::json_diff(c["reframe"], rf ? to_json(*rf) : Json());
        INFO("_remap_reframe: " << diff.value_or(""));
        CHECK_FALSE(diff);
        diff = gc::test::json_diff(c["baked"], to_json(baked_clip(*clip, "horneado.webm")));
        INFO("baked_clip: " << diff.value_or(""));
        CHECK_FALSE(diff);
    }
}
