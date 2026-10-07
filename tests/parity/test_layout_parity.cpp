// Paridad del ENCUADRE con video-yt (clip_layout.py).
//
// Fixtures (tools/fixtures/export_fixtures.py):
//   tests/fixtures/layout/synthetic.json   layout_cases.py (en el repo)
//   tests/fixtures/local/layout/real.json  tus clips de vídeo e imagen
//
// Ventana de recorte (crop_window), recorte en píxeles (source_crop_px) y rectángulo
// de destino (dest_rect, dest_rect_even): exactos, bit a bit y con el mismo tipo.
// Puntos de la fuente en la salida (source_point_to_output): tolerancia relativa de
// 1e-12, porque pasan por sin() y cos() y su último bit depende de la libm.

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <optional>
#include <string>

#include "gc/core/clip_layout.hpp"
#include "gc/core/io.hpp"
#include "gc/core/serialize.hpp"
#include "json_diff.hpp"

using gc::Json;
using namespace gc::core;

namespace {

constexpr double kTrigTol = 1e-12;
const std::filesystem::path kFixtures = path_from_utf8(GC_FIXTURES_DIR);

// NaN e infinito como los escribe la fixture (repr de Python).
Json num(double v) {
    if (std::isnan(v)) {
        return "nan";
    }
    if (std::isinf(v)) {
        return v > 0 ? "inf" : "-inf";
    }
    return v;
}

double arg(const Json& args, std::size_t i) { return args.at(i).get<double>(); }

// {"ok": [...]} o {"error": tipo} de la fixture frente a C++ (nullopt = error).
void check_outcome(const Json& want, const std::optional<Json>& got) {
    if (want.contains("error")) {
        INFO("Python falla con " << want.at("error").get<std::string>());
        CHECK_FALSE(got);
        return;
    }
    REQUIRE(got);
    const auto diff = gc::test::json_diff(want.at("ok"), *got);
    INFO(diff.value_or(""));
    CHECK_FALSE(diff);
}

std::size_t check_clips(const Json& file, const std::string& fname) {
    std::size_t samples = 0;
    for (const Json& c : file.at("clips")) {
        INFO(fname << " :: " << c.at("name").get<std::string>());
        auto clip = clip_from_json(c.at("clip"));
        REQUIRE(clip);
        CHECK(is_overlay(*clip) == c.at("is_overlay").get<bool>());
        const Flip flip = clip_flip(*clip);
        CHECK(Json::array({flip.h, flip.v}) == c.at("flip"));
        CHECK(flip_filters(*clip) == c.at("flip_filters").get<std::string>());

        for (const Json& s : c.at("crops")) {
            const CropWindow w = crop_window(*clip, s.at("src_aspect").get<double>(), s.at("out_aspect").get<double>(),
                                             s.at("src_t").get<double>(), s.at("local_t").get<double>());
            INFO("crop_window: " << s.dump());
            const auto diff = gc::test::json_diff(s.at("crop"), Json::array({w.cx, w.cy, w.wf, w.hf}));
            INFO(diff.value_or(""));
            CHECK_FALSE(diff);
            ++samples;
        }
        for (const Json& p : c.at("points")) {
            const Json& a = p.at("args");
            const Point q = source_point_to_output(*clip, arg(a, 0), arg(a, 1), arg(a, 2), arg(a, 3), arg(a, 4),
                                                   arg(a, 5), arg(a, 6), arg(a, 7));
            INFO("source_point_to_output: " << p.dump());
            const auto diff = gc::test::json_diff(p.at("out"), Json::array({q.x, q.y}), true, kTrigTol);
            INFO(diff.value_or(""));
            CHECK_FALSE(diff);
            ++samples;
        }
    }
    return samples;
}

std::size_t check_dest(const Json& file) {
    for (const Json& d : file.at("dest")) {
        const Json& t = d.at("transform");
        const double sw = d.at("crop_sw").get<double>();
        const double sh = d.at("crop_sh").get<double>();
        const double ow = d.at("out_w").get<double>();
        const double oh = d.at("out_h").get<double>();
        INFO("dest_rect(" << t.dump() << ", " << sw << ", " << sh << ", " << ow << ", " << oh << ")");
        const auto r = dest_rect(t, sw, sh, ow, oh);
        check_outcome(d.at("rect"), r ? std::optional<Json>(Json::array({num(r->dx), num(r->dy), num(r->dw),
                                                                         num(r->dh), num(r->rotation)}))
                                      : std::nullopt);
        const auto e = dest_rect_even(t, sw, sh, ow, oh);
        check_outcome(d.at("even"),
                      e ? std::optional<Json>(Json::array({e->dx, e->dy, e->dw, e->dh, num(e->rotation)}))
                        : std::nullopt);
    }
    return file.at("dest").size();
}

} // namespace

TEST_CASE("paridad del encuadre: casos sintéticos", "[parity][layout]") {
    const auto path = kFixtures / "layout" / "synthetic.json";
    REQUIRE(std::filesystem::exists(path)); // faltan: ejecuta tools/fixtures/export_fixtures.py
    auto file = read_json_file(path);
    REQUIRE(file);

    CHECK(check_clips(*file, "synthetic.json") > 2000);
    CHECK(check_dest(*file) > 100);

    for (const Json& c : file->at("crop_px")) {
        const Json& a = c.at("args");
        INFO("source_crop_px" << a.dump());
        const SourceCrop s = source_crop_px(arg(a, 0), arg(a, 1), arg(a, 2), arg(a, 3), arg(a, 4), arg(a, 5));
        const auto diff = gc::test::json_diff(c.at("out"), Json::array({s.sx, s.sy, s.sw, s.sh}));
        INFO(diff.value_or(""));
        CHECK_FALSE(diff);
    }
}

TEST_CASE("paridad del encuadre: tus clips reales", "[parity][layout][local]") {
    const auto path = kFixtures / "local" / "layout" / "real.json";
    if (!std::filesystem::exists(path)) {
        SKIP("Sin fixtures locales: export_fixtures.py --real las genera desde data/projects.json");
    }
    auto file = read_json_file(path);
    REQUIRE(file);
    CHECK(check_clips(*file, "real.json") > 1000);
    CHECK(check_dest(*file) > 500);
}
