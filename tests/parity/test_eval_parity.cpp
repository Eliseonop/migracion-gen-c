// Paridad de la EVALUACIÓN por clip con video-yt: lo que se dibuja de cada clip en un
// instante (fx_anims.py, clip_anim.render_pose, clip_fx.py, clip_blend.py, clip_adjust.py).
//
// Fixtures (tools/fixtures/export_fixtures.py):
//   tests/fixtures/eval/synthetic.json   eval_cases.py (en el repo)
//   tests/fixtures/local/eval/real.json  tus clips (todo menos el audio)
//
// Exactas, bit a bit y con el mismo tipo, salvo lo que pasa por pow(), sin() o cos()
// (curvas elásticas y senoidales, la matriz de color con exposición o tono): tolerancia
// relativa de 1e-12, porque el último bit depende de la libm de cada plataforma.

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "gc/core/blend.hpp"
#include "gc/core/clip_adjust.hpp"
#include "gc/core/clip_fx.hpp"
#include "gc/core/fx_anims.hpp"
#include "gc/core/fx_catalog.hpp"
#include "gc/core/io.hpp"
#include "gc/core/serialize.hpp"
#include "gc/core/timeline_rules.hpp"
#include "json_diff.hpp"

using gc::Cow;
using gc::Json;
using namespace gc::core;

namespace {

constexpr double kLibmTol = 1e-12;
const std::filesystem::path kFixtures = path_from_utf8(GC_FIXTURES_DIR);

void load_catalog() {
    auto catalog = FxCatalog::load(path_from_utf8(GC_RESOURCES_DIR) / "fx" / "catalog.json");
    REQUIRE(catalog);
    set_fx_catalog(std::make_shared<const FxCatalog>(std::move(*catalog)));
}

void check_same(std::string_view what, const Json& want, const Json& got, double tol = 0.0) {
    const auto diff = gc::test::json_diff(want, got, true, tol);
    INFO(what << ": " << diff.value_or(""));
    CHECK_FALSE(diff);
}

Json pose_json(const Pose& p) {
    return Json{{"x", p.x}, {"y", p.y}, {"scale", p.scale}, {"rotation", p.rotation}, {"opacity", p.opacity}};
}

Json delta_json(const std::optional<PoseDelta>& d) {
    const PoseDelta v = d.value_or(PoseDelta{}); // nullopt = IDENTITY de Python
    return Json{{"x", v.x}, {"y", v.y}, {"scale", v.scale}, {"rotation", v.rotation}, {"opacity", v.opacity}};
}

Json fx_json(const ClipFx& f) {
    return Json{{"opacity", f.opacity}, {"scale", f.scale}, {"tx", f.tx},
                {"ty", f.ty},           {"look", f.look},   {"wipe", f.wipe}};
}

Json matrix_json(const std::optional<ColorMatrix>& m) {
    if (!m) {
        return nullptr;
    }
    Json out = Json::array();
    for (const auto& row : *m) {
        out.push_back(Json::array({row[0], row[1], row[2]}));
    }
    return out;
}

// ¿Pasa la animación del clip por pow(), sin() o cos()? (curvas del catálogo y giro)
bool uses_libm(const Json& clip) {
    const Json& a = clip.value("animations", Json());
    return a.is_object() && !a.empty();
}

std::size_t check_clips(const Json& file, const std::string& fname) {
    std::size_t samples = 0;
    for (const Json& c : file.at("clips")) {
        INFO(fname << " :: " << c.at("name").get<std::string>());
        auto parsed = clip_from_json(c.at("clip"));
        REQUIRE(parsed);
        const Cow<TimelineClip> clip(std::move(*parsed));
        const double dur = clip_timeline_duration(*clip);
        CHECK(Json(dur) == c.at("duration"));

        const ClipAnimations anims = clip_animations(*clip, dur);
        Json got_anims = Json::array();
        for (const ClipAnimation& a : anims.items) {
            got_anims.push_back(Json{{"slot", a.slot}, {"id", a.anim->at("id")}, {"dur", a.dur}});
        }
        check_same("animaciones", c.at("animations"), got_anims);
        CHECK(has_animations(*clip) == c.at("has_animations").get<bool>());
        check_same("animation_sample_times", c.at("sample_times"), Json(animation_sample_times(*clip, dur)));

        CHECK(std::string(clip_blend(*clip)) == c.at("blend").get<std::string>());
        check_same("color_matrix", c.at("color_matrix"), matrix_json(color_matrix(*clip)), kLibmTol);
        CHECK(has_color_adjust(*clip) == c.at("has_color_adjust").get<bool>());
        Json got_passes = Json::array();
        for (const ClipPass& p : adjust_passes(clip)) {
            got_passes.push_back(Json{{"ghost", p.ghost}, {"clip", to_json(*p.clip)}});
        }
        check_same("adjust_passes", c.at("passes"), got_passes);

        const double tol = uses_libm(c.at("clip")) ? kLibmTol : 0.0;
        for (const Json& s : c.at("samples")) {
            const double t = s.at("t").get<double>();
            INFO("t = " << t);
            check_same("animation_delta", s.at("delta"), delta_json(animation_delta(*clip, t, dur)), tol);
            check_same("render_pose", s.at("pose"), pose_json(render_pose(*clip, t)), tol);
            check_same("anim_opacity", s.at("anim_opacity"), Json(anim_opacity(*clip, t)), tol);
            check_same("clip_fx_at", s.at("fx"), fx_json(clip_fx_at(*clip, t, dur)));
            ++samples;
        }
    }
    return samples;
}

} // namespace

TEST_CASE("paridad de la evaluación: casos sintéticos", "[parity][eval]") {
    load_catalog();
    const auto path = kFixtures / "eval" / "synthetic.json";
    REQUIRE(std::filesystem::exists(path)); // faltan: ejecuta tools/fixtures/export_fixtures.py
    auto file = read_json_file(path);
    REQUIRE(file);

    CHECK(check_clips(*file, "synthetic.json") > 800);

    for (const Json& e : file->at("ease")) {
        const std::string name = e.at("name").get<std::string>();
        const double x = e.at("x").get<double>();
        INFO("anim_ease(" << name << ", " << x << ")");
        // Las elásticas y senoidales pasan por pow()/sin()/cos().
        const bool libm = name == "outElastic" || name == "inOutSine";
        check_same("anim_ease", e.at("y"), Json(anim_ease(name, x)), libm ? kLibmTol : 0.0);
    }
    for (const Json& k : file->at("tracks")) {
        INFO("eval_anim_track(" << k.at("keys").dump() << ", " << k.at("u").dump() << ")");
        const auto v = eval_anim_track(k.at("keys"), k.at("u").get<double>());
        check_same("eval_anim_track", k.at("v"), v ? Json(*v) : Json(), kLibmTol);
    }
    for (const Json& w : file->at("fx_windows")) {
        const auto [a, b] = fx_windows(w.at("duration").get<double>());
        INFO("fx_windows(" << w.at("duration").dump() << ")");
        check_same("fx_windows", w.at("windows"), Json::array({a, b}));
    }
    for (const Json& e : file->at("effects")) {
        const Json& fx = e.at("effects");
        INFO("efectos " << fx.dump());
        check_same("color_matrix", e.at("color_matrix"), matrix_json(color_matrix(fx)), kLibmTol);
        CHECK(has_color_adjust(fx) == e.at("has_color_adjust").get<bool>());
        Json values = Json::object();
        for (const std::string_view k : kAdjustKeys) {
            values[std::string(k)] = adjust_value(fx, k);
        }
        check_same("adjust_value", e.at("values"), values);
        check_same("strip_color", e.at("strip_color"), strip_color(fx));
    }
}

TEST_CASE("paridad de la evaluación: tus clips reales", "[parity][eval][local]") {
    const auto path = kFixtures / "local" / "eval" / "real.json";
    if (!std::filesystem::exists(path)) {
        SKIP("Sin fixtures locales: export_fixtures.py --real las genera desde data/projects.json");
    }
    load_catalog();
    auto file = read_json_file(path);
    REQUIRE(file);
    CHECK(check_clips(*file, "real.json") > 5000);
}
