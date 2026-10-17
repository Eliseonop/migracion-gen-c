// Paridad de la GEOMETRÍA de las figuras con video-yt (shapes.py): shape_geometry de todos los
// tipos y sus variantes, path_points, trim_polyline, dash_runs, dash_pattern, flip_geometry y
// shape_draw_at.
//
// Fixture (tools/fixtures/export_fixtures.py --solo shapes, casos en shape_cases.py):
//   tests/fixtures/shapes/geometry.json
//
// Los puntos se comparan con una tolerancia de 1e-9 (cos, sin y pow de la libm frente a los de
// Python); el número de puntos y de anillos, exacto.

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <string>

#include "gc/core/io.hpp"
#include "gc/core/serialize.hpp"
#include "gc/core/shape_geometry.hpp"

using gc::Json;
using namespace gc::core;

namespace {

const std::filesystem::path kFixtures = path_from_utf8(GC_FIXTURES_DIR);

bool same_points(const Polyline& got, const Json& want) {
    if (got.size() != want.size()) {
        return false;
    }
    for (std::size_t i = 0; i < got.size(); ++i) {
        const double x = want[i][0].get<double>();
        const double y = want[i][1].get<double>();
        if (std::abs(got[i].x - x) > 1e-9 || std::abs(got[i].y - y) > 1e-9) {
            return false;
        }
    }
    return true;
}

bool same_lines(const std::vector<Polyline>& got, const Json& want) {
    if (got.size() != want.size()) {
        return false;
    }
    for (std::size_t i = 0; i < got.size(); ++i) {
        if (!same_points(got[i], want[i])) {
            return false;
        }
    }
    return true;
}

Polyline polyline(const Json& j) {
    Polyline out;
    for (const Json& p : j) {
        out.push_back({p[0].get<double>(), p[1].get<double>()});
    }
    return out;
}

} // namespace

TEST_CASE("paridad de las figuras: la geometría de shapes.py", "[parity][shapes]") {
    const auto path = kFixtures / "shapes" / "geometry.json";
    REQUIRE(std::filesystem::exists(path)); // faltan: export_fixtures.py --solo shapes
    auto file = read_json_file(path);
    REQUIRE(file);
    const Json& f = *file;

    for (const Json& c : f.at("geometry")) {
        INFO("shape_geometry(" << c.at("type") << ", " << c.at("opts").dump() << ")");
        const auto g = shape_geometry(c.at("type").get<std::string>(), c.at("opts"));
        CHECK(same_lines(g.fills, c.at("out").at("fills")));
        CHECK(same_lines(g.strokes, c.at("out").at("strokes")));
    }
    for (const Json& c : f.at("paths")) {
        INFO("path_points(" << c.at("points").dump() << ", " << c.at("closed") << ", " << c.at("smooth") << ")");
        CHECK(same_points(path_points(c.at("points"), c.at("closed").get<bool>(), c.at("smooth").get<bool>()),
                          c.at("out")));
    }
    for (const Json& c : f.at("trims")) {
        INFO("trim_polyline(" << c.at("pts").dump() << ", " << c.at("frac") << ")");
        CHECK(same_points(trim_polyline(polyline(c.at("pts")), c.at("frac").get<double>()), c.at("out")));
    }
    for (const Json& c : f.at("dashes")) {
        INFO("dash_runs(" << c.at("pts").dump() << ", " << c.at("pattern").dump() << ")");
        const auto d = dash_runs(polyline(c.at("pts")), {c.at("pattern")[0].get<double>(), c.at("pattern")[1].get<double>()});
        CHECK(same_lines(d.runs, c.at("runs")));
        CHECK(same_points(d.dots, c.at("dots")));
    }
    for (const Json& c : f.at("patterns")) {
        INFO("dash_pattern(" << c.at("dash") << ", " << c.at("lw") << ")");
        const auto p = dash_pattern(c.at("dash").get<std::string>(), c.at("lw").get<double>());
        if (c.at("out").is_null()) {
            CHECK_FALSE(p);
        } else {
            REQUIRE(p);
            CHECK(p->first == c.at("out")[0].get<double>());
            CHECK(p->second == c.at("out")[1].get<double>());
        }
    }
    for (const Json& c : f.at("flips")) {
        INFO("flip_geometry(" << c.at("type") << ", " << c.at("flip").dump() << ")");
        const auto g = flip_geometry(shape_geometry(c.at("type").get<std::string>(), Json::object()),
                                     c.at("flip")[0].get<bool>(), c.at("flip")[1].get<bool>());
        CHECK(same_lines(g.fills, c.at("out").at("fills")));
        CHECK(same_lines(g.strokes, c.at("out").at("strokes")));
    }
    for (const Json& c : f.at("draws")) {
        auto timeline = timeline_from_json(c.at("timeline"));
        REQUIRE(timeline);
        const TimelineClip& clip = *timeline->clips.at(0);
        for (const Json& s : c.at("samples")) {
            INFO("shape_draw_at(" << clip.shape.value_or(Json()).dump() << ", " << s[0] << ")");
            auto d = shape_draw_at(clip, s[0].get<double>());
            REQUIRE(d);
            CHECK(std::abs(*d - s[1].get<double>()) < 1e-12);
        }
    }
}

TEST_CASE("shape_plan: grosor, vértices al doble y opacidad como rasterize_shape", "[shapes]") {
    // Un rectángulo sin redondear de 0,5 × 0,25 en el centro de 100 × 200, trazo 7.2 y relleno.
    const Json shape = {{"type", "rect"}, {"w", 0.5}, {"h", 0.25}, {"cornerRadius", 0}, {"strokeWidth", 7.2},
                        {"fill", "#102030"}, {"stroke", "#ffffff"}};
    auto plan = shape_plan(shape, ShapePose{0.5, 0.5, 1.0, 0.0, 0.5}, 1.0, Flip{}, 100, 200);
    REQUIRE(plan);
    // sw = max(1, round(7.2 · 400 / 720)) = round(4) = 4 en el doble → 2 px.
    CHECK(plan->line_width == 2.0);
    CHECK(plan->opacity == 0.5);
    CHECK(plan->fill.r == 0x10);
    CHECK(plan->fill.b == 0x30);
    // El contorno cerrado y luego el relleno; vértices del doble en enteros, llevados a la salida.
    REQUIRE(plan->ops.size() == 2);
    CHECK(plan->ops[0].kind == ShapeOp::Kind::Line);
    CHECK(plan->ops[0].closed);
    CHECK(plan->ops[1].kind == ShapeOp::Kind::Fill);
    // La caja del rect en 0–100: x 8…92, y 22…78; en el doble: 200 × 100 de caja centrada en (100, 200).
    // x = 100 + (0,08 − 0,5) · 100 = 58 → 58 / 2 + 0,25 = 29,25.
    CHECK(plan->ops[1].points[0].x == 29.25);
    // Opacidad ≥ 0,999: entera, como el export.
    auto opaque = shape_plan(shape, ShapePose{0.5, 0.5, 1.0, 0.0, 0.9995}, 1.0, Flip{}, 100, 200);
    REQUIRE(opaque);
    CHECK(opaque->opacity == 1.0);
    // Un valor que no es un número es un error, como float() en Python.
    CHECK_FALSE(shape_plan(Json{{"type", "rect"}, {"w", "ancho"}}, ShapePose{}, 1.0, Flip{}, 100, 200));
}
