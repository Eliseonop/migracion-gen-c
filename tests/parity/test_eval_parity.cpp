// Paridad de la EVALUACIÓN por clip con video-yt: lo que se dibuja de cada clip en un
// instante (fx_anims.py, clip_anim.render_pose, clip_fx.py, clip_blend.py, clip_adjust.py).
//
// Fixtures (tools/fixtures/export_fixtures.py):
//   tests/fixtures/eval/synthetic.json   eval_cases.py (en el repo)
//   tests/fixtures/local/eval/real.json  tus clips (todo menos el audio)
//
// Exactas, bit a bit y con el mismo tipo, salvo lo que pasa por pow(), sin() o cos(): las
// curvas outCubic, inOutQuad, inOutCubic, outBack, outElastic e inOutSine y la matriz de
// color con exposición o tono. Ahí, tolerancia relativa de 4e-15 (unos 18 ulps), porque el último bit
// depende de la libm de cada plataforma (con tolerancia 0 fallan 3 muestras, de 1 ulp).
// NaN e infinito van como texto ("nan", "inf", "-inf"), como el repr de Python.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include "gc/core/blend.hpp"
#include "gc/core/clip_adjust.hpp"
#include "gc/core/clip_filters.hpp"
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

constexpr double kLibmTol = 4e-15;
// Curvas que pasan por pow(), sin() o cos().
constexpr std::array<std::string_view, 6> kLibmEases{"outCubic", "inOutQuad", "inOutCubic",
                                                     "outBack",  "outElastic", "inOutSine"};
const std::filesystem::path kFixtures = path_from_utf8(GC_FIXTURES_DIR);

void load_catalog() {
    auto catalog = FxCatalog::load(path_from_utf8(GC_RESOURCES_DIR) / "fx" / "catalog.json");
    REQUIRE(catalog);
    set_fx_catalog(std::make_shared<const FxCatalog>(std::move(*catalog)));
}

// NaN e infinito como texto, igual que la fixture (_nan_safe del exportador).
Json nan_safe(const Json& j) {
    if (j.is_number_float() && !std::isfinite(j.get<double>())) {
        const double v = j.get<double>();
        return std::isnan(v) ? "nan" : (v > 0 ? "inf" : "-inf");
    }
    if (j.is_structured()) {
        Json out = j;
        for (auto& v : out) {
            v = nan_safe(v);
        }
        return out;
    }
    return j;
}

double num(const Json& j) {
    if (j.is_string()) {
        const std::string& s = j.get_ref<const std::string&>();
        return s == "nan" ? std::numeric_limits<double>::quiet_NaN()
                          : (s == "-inf" ? -1 : 1) * std::numeric_limits<double>::infinity();
    }
    return j.get<double>();
}

bool libm_ease(const Json& keys) {
    if (!keys.is_array()) {
        return false;
    }
    return std::ranges::any_of(keys, [](const Json& k) {
        return k.is_array() && k.size() > 2 && k[2].is_string() &&
               std::ranges::find(kLibmEases, k[2].get_ref<const std::string&>()) != kLibmEases.end();
    });
}

// ¿Usa alguna de las animaciones válidas del clip una curva que pase por la libm?
bool anims_use_libm(const ClipAnimations& anims) {
    return std::ranges::any_of(anims.items, [](const ClipAnimation& a) {
        const Json& tracks = a.anim->value("tracks", Json::object());
        return std::ranges::any_of(tracks.items(), [](const auto& kv) { return libm_ease(kv.value()); });
    });
}

// La matriz pasa por pow() con exposición y por cos()/sin() con tono.
double matrix_tol(const Json& effects) {
    return adjust_value(effects, "exposure") != 0 || adjust_value(effects, "hue") != 0 ? kLibmTol : 0.0;
}

void check_same(std::string_view what, const Json& want, const Json& got_raw, double tol = 0.0) {
    const Json got = nan_safe(got_raw);
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

Json affine_json(const std::optional<AffineColorMatrix>& m) {
    if (!m) {
        return nullptr;
    }
    Json out = Json::array();
    for (const auto& row : *m) {
        out.push_back(Json::array({row[0], row[1], row[2], row[3]}));
    }
    return out;
}

// Filtros de color (clip_filters.py): exactos, sin libm salvo la matriz de una capa de ajuste
// con exposición o tono (color_matrix).
std::size_t check_filters(const Json& file, const std::string& fname) {
    std::size_t n = 0;
    for (const Json& c : file.at("filters")) {
        INFO(fname << " :: filtros de " << c.at("name").get<std::string>());
        auto parsed = clip_from_json(c.at("clip"));
        REQUIRE(parsed);
        const TimelineClip& clip = *parsed;
        Json got = Json::array();
        for (const ClipFilter& f : clip_filters(clip)) {
            got.push_back(Json{{"id", f.id}, {"amount", f.amount}});
        }
        check_same("clip_filters", c.at("filters"), got);
        check_same("stack_matrix", c.at("stack"), affine_json(stack_matrix(clip_filters(clip))));
        const Json& effects = clip.effects && clip.effects->is_object() ? *clip.effects : Json::object();
        check_same("adjustment_matrix", c.at("adjustment"), affine_json(adjustment_matrix(clip)), matrix_tol(effects));
        CHECK(filters_ffmpeg(clip) == c.at("ffmpeg").get<std::string>());
        ++n;
    }
    for (const Json& f : file.at("filter_matrix")) {
        const std::string id = f.at("id").get<std::string>();
        const double k = num(f.at("k"));
        INFO("filter_matrix(" << id << ", " << k << ")");
        const AffineColorMatrix m = filter_matrix(id, k);
        check_same("filter_matrix", f.at("matrix"), affine_json(m));
        CHECK(matrix_ffmpeg(m) == f.at("ffmpeg").get<std::string>());
        ++n;
    }
    const auto catalog = color_filters();
    CHECK((file.at("filter_catalog").empty() || file.at("filter_catalog").size() == catalog.size()));
    for (std::size_t i = 0; i < file.at("filter_catalog").size() && i < catalog.size(); ++i) {
        const Json& want = file.at("filter_catalog")[i];
        const ColorFilterDef& f = catalog[i];
        INFO("catálogo de filtros, " << f.id);
        CHECK(std::string(f.id) == want.at("id").get<std::string>());
        CHECK(std::string(f.label) == want.at("label").get<std::string>());
        CHECK(std::string(f.group) == want.at("group").get<std::string>());
        check_same("matriz del catálogo", want.at("matrix"), affine_json(f.matrix));
        ++n;
    }
    return n;
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
        const Json& clip_effects = clip->effects ? *clip->effects : Json::object();
        check_same("color_matrix", c.at("color_matrix"), matrix_json(color_matrix(*clip)), matrix_tol(clip_effects));
        CHECK(has_color_adjust(*clip) == c.at("has_color_adjust").get<bool>());
        Json got_passes = Json::array();
        for (const ClipPass& p : adjust_passes(clip)) {
            got_passes.push_back(Json{{"ghost", p.ghost}, {"clip", to_json(*p.clip)}});
        }
        check_same("adjust_passes", c.at("passes"), got_passes);

        const double tol = anims_use_libm(anims) ? kLibmTol : 0.0;
        for (const Json& s : c.at("samples")) {
            const double t = num(s.at("t"));
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
    CHECK(check_filters(*file, "synthetic.json") > 200);

    for (const Json& e : file->at("ease")) {
        const std::string name = e.at("name").get<std::string>();
        const double x = e.at("x").get<double>();
        INFO("anim_ease(" << name << ", " << x << ")");
        const bool libm = std::ranges::find(kLibmEases, name) != kLibmEases.end();
        check_same("anim_ease", e.at("y"), Json(anim_ease(name, x)), libm ? kLibmTol : 0.0);
    }
    for (const Json& k : file->at("tracks")) {
        INFO("eval_anim_track(" << k.at("keys").dump() << ", " << k.at("u").dump() << ")");
        const auto v = eval_anim_track(k.at("keys"), num(k.at("u")));
        check_same("eval_anim_track", k.at("v"), v ? Json(*v) : Json(), libm_ease(k.at("keys")) ? kLibmTol : 0.0);
    }
    for (const Json& w : file->at("fx_windows")) {
        const auto [a, b] = fx_windows(num(w.at("duration")));
        INFO("fx_windows(" << w.at("duration").dump() << ")");
        check_same("fx_windows", w.at("windows"), Json::array({a, b}));
    }
    for (const Json& e : file->at("effects")) {
        const Json& fx = e.at("effects");
        INFO("efectos " << fx.dump());
        check_same("color_matrix", e.at("color_matrix"), matrix_json(color_matrix(fx)), matrix_tol(fx));
        CHECK(has_color_adjust(fx) == e.at("has_color_adjust").get<bool>());
        Json values = Json::object();
        for (const std::string_view k : kAdjustKeys) {
            values[std::string(k)] = adjust_value(fx, k);
        }
        check_same("adjust_value", e.at("values"), values);
        check_same("strip_color", e.at("strip_color"), strip_color(fx));
    }
    for (const Json& b : file->at("blend")) {
        INFO("normalize_blend(" << b.at("mode").dump() << ")");
        CHECK(std::string(normalize_blend(b.at("mode").get<std::string>())) == b.at("normalized").get<std::string>());
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
    CHECK(check_filters(*file, "real.json") > 900);
}
