// Paridad de la ANIMACIÓN con video-yt (clip_keyframes.py, clip_mask.py).
//
// Fixtures (tools/fixtures/export_fixtures.py):
//   tests/fixtures/keyframes/synthetic.json   kf_cases.py (en el repo)
//   tests/fixtures/local/keyframes/real.json  tus clips con keyframes/máscaras/texto
//
// En cada instante: propiedades (clip_props_at), estilo de texto animado,
// máscaras y volumen. Los números se comparan con una tolerancia de 1e-12
// (relativa): las curvas cúbicas usan pow() y su último bit depende de la libm.
// Textos, colores, tipos y orden de claves, exactos.

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

#include "gc/core/io.hpp"
#include "gc/core/keyframes.hpp"
#include "gc/core/masks.hpp"
#include "gc/core/serialize.hpp"
#include "json_diff.hpp"

using gc::Json;
using namespace gc::core;

namespace {

constexpr double kTol = 1e-12;
const std::filesystem::path kFixtures = path_from_utf8(GC_FIXTURES_DIR);

Json masks_json(const std::vector<Json>& masks) {
    Json out = Json::array();
    for (const auto& m : masks) {
        out.push_back(m);
    }
    return out;
}

std::size_t check_clips(const Json& file, const std::string& fname) {
    std::size_t samples = 0;
    for (const Json& c : file.at("clips")) {
        const auto name = c.at("name").get<std::string>();
        INFO(fname << " :: " << name);
        auto clip = clip_from_json(c.at("clip"));
        REQUIRE(clip);
        for (const Json& s : c.at("samples")) {
            const double t = s.at("t").get<double>();
            INFO("t = " << t);
            const auto props = gc::test::json_diff(s.at("props"), clip_props_at(*clip, t).to_json(), true, kTol);
            INFO("props: " << props.value_or(""));
            CHECK_FALSE(props);
            const auto style = gc::test::json_diff(s.at("style"), text_style_at(*clip, t), true, kTol);
            INFO("estilo: " << style.value_or(""));
            CHECK_FALSE(style);
            const auto masks =
                gc::test::json_diff(s.at("masks"), masks_json(clip_masks_at(*clip, t, true)), true, kTol);
            INFO("máscaras: " << masks.value_or(""));
            CHECK_FALSE(masks);
            const double vol = s.at("volume").get<double>();
            CHECK(std::abs(clip_volume_at(*clip, t) - vol) <= kTol * std::max(1.0, std::abs(vol)));
            ++samples;
        }
        CHECK(text_style_animates(*clip) == c.at("text_style_animates").get<bool>());
        CHECK(has_mask(*clip) == c.at("has_mask").get<bool>());
        CHECK(mask_animates(*clip, c.at("duration").get<double>()) == c.at("mask_animates").get<bool>());
    }
    return samples;
}

} // namespace

TEST_CASE("paridad de animación: casos sintéticos", "[parity][keyframes]") {
    const auto path = kFixtures / "keyframes" / "synthetic.json";
    REQUIRE(std::filesystem::exists(path)); // faltan: ejecuta tools/fixtures/export_fixtures.py
    auto file = read_json_file(path);
    REQUIRE(file);

    CHECK(check_clips(*file, "synthetic.json") > 100);

    for (const Json& e : file->at("ease")) {
        const double u = e.at("u").get<double>();
        const Json& kind = e.at("kind");
        INFO("ease_t(" << u << ", " << kind.dump() << ", " << e.at("bezier").dump() << ")");
        const double want = e.at("y").get<double>();
        const double got = ease_t(u, normalize_interp(kind), e.at("bezier"));
        CHECK(std::abs(got - want) <= kTol * std::max(1.0, std::abs(want)));
    }

    for (const Json& m : file->at("masks")) {
        INFO("normalize_mask(" << m.at("input").dump() << ")");
        const auto diff = gc::test::json_diff(m.at("expected"), normalize_mask(m.at("input"))); // exacto (int ≠ float)
        INFO(diff.value_or(""));
        CHECK_FALSE(diff);
    }

    for (const Json& s : file->at("styles")) {
        INFO("with_text_style_kf(" << s.at("style").dump() << ", " << s.at("anim").dump() << ")");
        const auto diff = gc::test::json_diff(s.at("expected"), with_text_style_kf(s.at("style"), s.at("anim")));
        INFO(diff.value_or(""));
        CHECK_FALSE(diff);
    }
}

TEST_CASE("paridad de animación: tus clips reales", "[parity][keyframes][local]") {
    const auto path = kFixtures / "local" / "keyframes" / "real.json";
    if (!std::filesystem::exists(path)) {
        SKIP("Sin fixtures locales: export_fixtures.py --real las genera desde data/projects.json");
    }
    auto file = read_json_file(path);
    REQUIRE(file);
    CHECK(check_clips(*file, "real.json") > 1000);
}
