// Paridad de los EFECTOS DE VÍDEO (`clip.vfx`, motor «shader») con el runtime de video-yt.
//
// Fixture: tests/fixtures/render/vfx.json y render/vfx/*.png (export_fixtures.py --solo vfx): cada
// efecto pasado por `window.__fx.renderVfx` del runtime real en Chromium (el de fx_bake.py) sobre
// render/files/vfx_in.png, con los parámetros por defecto en dos instantes, con otros parámetros y,
// si tiene margen, con él. Aquí se hace lo mismo con gc_render (SkSL por CPU) y se compara el RGBA
// premultiplicado por PSNR: WebGL corre en la GPU y Skia en la CPU, con la misma precisión de float;
// el filtrado bilineal de la GPU (pesos de 8 bits) se imita en el SkSL. Medido: mediana de 64 dB; lo
// peor, los de ruido y bordes (sketch 37,8, pixel_pulse 39,8, tv_static y night_vision 42–45).

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <filesystem>
#include <format>
#include <memory>

#include "gc/core/fx_catalog.hpp"
#include "gc/core/io.hpp"
#include "gc/media/decode.hpp"
#include "gc/render/png.hpp"
#include "gc/render/vfx.hpp"

namespace fs = std::filesystem;
using gc::Json;
using namespace gc::core;

namespace {

const fs::path kFixtures = path_from_utf8(GC_FIXTURES_DIR);

gc::render::Image load_png(const fs::path& path) {
    auto frame = gc::media::frame_at(path, 0.0);
    REQUIRE(frame);
    REQUIRE(*frame);
    return {(*frame)->width, (*frame)->height, std::move((*frame)->pixels)};
}

// PSNR del RGBA premultiplicado (lo transparente no cuenta por su color).
double psnr_premul(const gc::render::Image& a, const gc::render::Image& b) {
    double se = 0.0;
    for (std::size_t i = 0; i < a.pixels.size(); i += 4) {
        const double aa = a.pixels[i + 3] / 255.0;
        const double ba = b.pixels[i + 3] / 255.0;
        for (std::size_t c = 0; c < 3; ++c) {
            const double d = (a.pixels[i + c] * aa) - (b.pixels[i + c] * ba);
            se += d * d;
        }
        const double da = static_cast<double>(a.pixels[i + 3]) - b.pixels[i + 3];
        se += da * da;
    }
    const double mse = se / static_cast<double>(a.pixels.size());
    return mse == 0.0 ? 99.0 : 10.0 * std::log10(255.0 * 255.0 / mse);
}

// Fracción de píxeles con algún canal (sin premultiplicar) a más de 2 niveles.
double differing(const gc::render::Image& a, const gc::render::Image& b) {
    std::size_t n = 0;
    for (std::size_t i = 0; i < a.pixels.size(); i += 4) {
        for (std::size_t c = 0; c < 4; ++c) {
            if (std::abs(static_cast<int>(a.pixels[i + c]) - static_cast<int>(b.pixels[i + c])) > 2) {
                ++n;
                break;
            }
        }
    }
    return static_cast<double>(n) / static_cast<double>(a.pixels.size() / 4);
}

// Efectos que ELIGEN una muestra con un umbral duro (pixel_drift: la más brillante de 24 por
// encima de `threshold`, con la luma de `straight`, que divide por el alfa): una diferencia de un
// nivel en un píxel cercano al umbral cambia la muestra entera. Con estos no vale el PSNR: se pide
// que difieran pocos píxeles (medido: 0,8 % del cuadro, en el borde semitransparente).
bool threshold_effect(const std::string& id) { return id == "pixel_drift"; }

} // namespace

TEST_CASE("efectos de vídeo: el SkSL de todo el catálogo compila", "[render][vfx]") {
    auto catalog = FxCatalog::load(path_from_utf8(GC_RESOURCES_DIR) / "fx" / "catalog.json");
    REQUIRE(catalog);
    set_fx_catalog(std::make_shared<FxCatalog>(std::move(*catalog)));
    std::size_t n = 0;
    for (const Json& e : fx_catalog()->data()["effects"]) {
        if (e["engine"] != "shader") {
            continue;
        }
        const auto id = e["id"].get<std::string>();
        INFO(id);
        const gc::render::Image one{4, 4, std::vector<std::uint8_t>(64, 200)};
        const Json entries = gc::render::clip_vfx(Json::array({Json{{"effect", id}}}));
        auto r = gc::render::render_vfx(one, entries, 0.5, 2.0, false);
        INFO((r ? std::string() : r.error().to_string()));
        CHECK(r);
        ++n;
    }
    CHECK(n == 41);
}

TEST_CASE("paridad de los efectos de vídeo con el runtime de video-yt en Chromium", "[parity][render][vfx]") {
    auto catalog = FxCatalog::load(path_from_utf8(GC_RESOURCES_DIR) / "fx" / "catalog.json");
    REQUIRE(catalog);
    set_fx_catalog(std::make_shared<FxCatalog>(std::move(*catalog)));
    const auto fixture = read_json_file(kFixtures / "render" / "vfx.json");
    REQUIRE(fixture); // faltan: export_fixtures.py --solo vfx
    gc::render::set_vfx_assets_dir(path_from_utf8(GC_RESOURCES_DIR) / "fx");
    const auto dump = std::getenv("GC_VFX_DUMP"); // carpeta para dejar los fotogramas de C++
    double worst = 99.0;
    for (const Json& c : *fixture) {
        const std::string what = std::format("{} {} t={}{}", c["effect"].get<std::string>(), c["params"].dump(),
                                             c["t"].get<double>(), c["pad_allowed"].get<bool>() ? " con margen" : "");
        INFO(what);
        const auto input = load_png(kFixtures / "render" / "files" / c["input"].get<std::string>());
        const auto want = load_png(kFixtures / "render" / path_from_utf8(c["frame"].get<std::string>()));
        const Json entries = gc::render::clip_vfx(Json::array(
            {Json{{"effect", c["effect"]}, {"v", c["v"]}, {"params", c["params"]}, {"from", 0}, {"to", nullptr}}}));
        auto got = gc::render::render_vfx(input, entries, c["t"].get<double>(), c["clip_dur"].get<double>(),
                                          c["pad_allowed"].get<bool>(), 1e300);
        INFO((got ? std::string() : got.error().to_string()));
        REQUIRE(got);
        REQUIRE(got->image.width == want.width);
        REQUIRE(got->image.height == want.height);
        if (dump != nullptr) {
            (void)gc::render::write_png(got->image, path_from_utf8(dump) / path_from_utf8(c["frame"].get<std::string>()).filename());
        }
        const double db = psnr_premul(got->image, want);
        UNSCOPED_INFO(what << ": " << db << " dB");
        if (threshold_effect(c["effect"].get<std::string>())) {
            const double frac = differing(got->image, want);
            INFO(db << " dB, " << frac * 100 << " % de píxeles distintos");
            CHECK(frac <= 0.015);
            CHECK(db >= 25.0);
            continue;
        }
        worst = std::min(worst, db);
        if (c["effect"] == "paper_cutout") {
            // Papel recortado (Canvas 2D y el filtro SVG, también con Skia en el navegador): 44–61 dB;
            // la sombra de color, 55,7 (con el difuminado entero en vez de la mitad, 46,4); el borde
            // rasgado ancho, 37,7 (el ruido fractal desplaza muchos píxeles del borde).
            const Json& params = c["params"];
            const double floor = params.contains("shadowBlur") ? 50.0 : params.contains("width") ? 35.0 : 40.0;
            CHECK(db >= floor);
            continue;
        }
        CHECK(db >= 35.0); // el suelo de las fixtures de render del plan
    }
    INFO("peor: " << worst << " dB");
    CHECK(worst >= 35.0);
}

TEST_CASE("efectos de vídeo: el plan de horneado (velocidad, revés, paper_cutout y versión rara)", "[render][vfx]") {
    auto catalog = FxCatalog::load(path_from_utf8(GC_RESOURCES_DIR) / "fx" / "catalog.json");
    REQUIRE(catalog);
    set_fx_catalog(std::make_shared<FxCatalog>(std::move(*catalog)));
    TimelineClip clip;
    clip.id = "c1";
    clip.kind = "video";
    clip.asset_kind = "clips";
    clip.in_point = 0.0;
    clip.out_point = 2.0;
    clip.source_duration = 2.0;
    clip.vfx = Json::array({Json{{"effect", "paper_cutout"}}}); // de Canvas 2D: también se dibuja
    auto plan = gc::render::plan_vfx_bake(clip, 64, 48, 360, 640, 30);
    REQUIRE(plan);
    CHECK(plan->has_value());
    clip.vfx = Json::array({Json{{"effect", "crt"}}});
    clip.speed = 1.0005; // video_speed_filters: a menos de 1e-3 de 1 es velocidad 1
    plan = gc::render::plan_vfx_bake(clip, 64, 48, 360, 640, 30);
    REQUIRE(plan);
    CHECK(plan->has_value());
    clip.speed = 1.5; // con la velocidad y el revés antes del fps, como prepare_input
    clip.reverse = true;
    plan = gc::render::plan_vfx_bake(clip, 64, 48, 360, 640, 30);
    REQUIRE(plan);
    CHECK(plan->has_value());
    clip.reverse = false;
    clip.vfx = Json::array({Json{{"effect", "crt"}, {"v", "abc"}}}); // int("abc") de Python
    clip.speed = 1.0;
    CHECK_FALSE(gc::render::plan_vfx_bake(clip, 64, 48, 360, 640, 30));
}
