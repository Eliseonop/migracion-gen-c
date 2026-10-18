// Paridad de la MÁSCARA DE TIPO TEXTO con video-yt (_paint_text de clip_mask.py, que dibuja con
// Pillow): las medidas de Pillow que usa (gc::render::pillow_text) y el alfa de mask_alpha frente al
// PNG de combined_alpha.
//
// Solo se compila con FFmpeg y Skia (gc_media y gc_render, presets msvc-vcpkg-*).
// Fixture (tools/fixtures/export_fixtures.py → synthetic_text_mask_fixtures):
//   tests/fixtures/render/text_masks.json   media_cases.TEXT_MASKS (la máscara, el cuadro y el
//                                           PNG del alfa) y TEXT_MASK_METRICS (getmetrics,
//                                           getlength y la caja con cada ancla).
//
// Las métricas y el ancla vertical son exactas; el ancho, con los avances redondeados de HarfBuzz
// en vez de los del hinting de FreeType (±1 px). Las letras no son las mismas: Pillow rasteriza el
// contorno con hinting y Skia sin él, así que el alfa se compara por PSNR.

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>

#include "gc/core/font_metrics.hpp"
#include "gc/core/io.hpp"
#include "gc/media/decode.hpp"
#include "gc/media/probe.hpp"
#include "gc/render/mask.hpp"
#include "gc/render/png.hpp"
#include "gc/render/text.hpp"

using gc::Json;
using namespace gc::core;

namespace {

const std::filesystem::path kFixtures = path_from_utf8(GC_FIXTURES_DIR);

gc::render::TextFonts fonts() {
    return gc::render::TextFonts(FontDirs{default_system_font_dir(), path_from_utf8(GC_RESOURCES_DIR) / "fonts"});
}

// PSNR entre el alfa del C++ y el canal gris del PNG (0–255).
double psnr(const gc::render::MaskAlpha& a, const gc::media::RgbaFrame& png) {
    double sum = 0.0;
    for (std::size_t i = 0; i < a.alpha.size(); ++i) {
        const double d = static_cast<double>(a.alpha[i]) - static_cast<double>(png.pixels[4 * i]);
        sum += d * d;
    }
    const double mse = sum / static_cast<double>(a.alpha.size());
    return mse == 0.0 ? std::numeric_limits<double>::infinity() : 10.0 * std::log10(255.0 * 255.0 / mse);
}

} // namespace

TEST_CASE("paridad de la máscara de texto: las medidas de Pillow", "[parity][render][mask]") {
    auto file = read_json_file(kFixtures / "render" / "text_masks.json");
    REQUIRE(file);
    const auto f = fonts();
    std::size_t checked = 0;
    for (const Json& c : file->at("metrics")) {
        const auto font = c.at("font").get<std::string>();
        const bool bold = c.at("bold").get<bool>();
        const int size = c.at("size").get<int>();
        const auto text = c.at("text").get<std::string>();
        INFO(font << (bold ? " negrita" : "") << " a " << size << " px: «" << text << "»");
        const auto ls = gc::render::pillow_text(f, font, bold, size, text, "ls");
        REQUIRE(ls);
        CHECK(ls->ascent == c.at("metrics")[0].get<int>());
        CHECK(ls->descent == c.at("metrics")[1].get<int>());
        CHECK(std::abs(ls->length - c.at("length").get<double>()) <= 1.0);
        if (text.empty()) {
            continue; // sin texto, Pillow da la caja (0, 0, 0, 0) con cualquier ancla
        }
        const Json& box = c.at("bbox");
        for (const char* anchor : {"mm", "lm", "rm"}) {
            INFO("ancla " << anchor);
            const auto p = gc::render::pillow_text(f, font, bold, size, text, anchor);
            REQUIRE(p);
            const double want_x = box.at(anchor)[0].get<double>() - box.at("ls")[0].get<double>();
            const double want_y = box.at(anchor)[1].get<double>() - box.at("ls")[1].get<double>();
            CHECK(p->origin_y == want_y);
            CHECK(std::abs(p->origin_x - want_x) <= 1.0);
        }
        ++checked;
    }
    CHECK(checked >= 100);
}

TEST_CASE("paridad de la máscara de texto: el alfa frente al de Pillow", "[parity][render][mask]") {
    auto file = read_json_file(kFixtures / "render" / "text_masks.json");
    REQUIRE(file);
    gc::media::silence_ffmpeg_log();
    const auto f = fonts();
    for (const Json& c : file->at("masks")) {
        const auto name = c.at("name").get<std::string>();
        INFO(name);
        const Json& m = c.at("mask");
        const Json& t = m.at("text");
        // La misma fuente que resolve_font_path de compose.py.
        CHECK(resolve_font_path(t.at("font").get<std::string>(), t.at("weight").get<double>() >= 600,
                                FontDirs{default_system_font_dir(), path_from_utf8(GC_RESOURCES_DIR) / "fonts"})
                  .filename()
                  .string() == c.at("font_file").get<std::string>());
        const int w = c.at("w").get<int>();
        const int h = c.at("h").get<int>();
        const auto alpha = gc::render::mask_alpha({m}, w, h, &f);
        REQUIRE(alpha);
        auto png = gc::media::frame_at(kFixtures / "render" / path_from_utf8(c.at("png").get<std::string>()), 0.0);
        REQUIRE(png);
        REQUIRE(png->has_value());
        REQUIRE((*png)->width == w);
        REQUIRE((*png)->height == h);
        if (const char* dump = std::getenv("GC_MASK_DUMP")) { // carpeta donde dejar el alfa del C++
            gc::render::Image img{w, h, std::vector<std::uint8_t>(alpha->alpha.size() * 4, 255)};
            for (std::size_t i = 0; i < alpha->alpha.size(); ++i) {
                img.pixels[4 * i] = img.pixels[4 * i + 1] = img.pixels[4 * i + 2] = alpha->alpha[i];
            }
            (void)gc::render::write_png(img, path_from_utf8(dump) / path_from_utf8(c.at("png").get<std::string>()));
        }
        const double db = psnr(*alpha, **png);
        UNSCOPED_INFO(name << ": PSNR del alfa " << db << " dB");
        CHECK(db >= 27.0); // la peor, 29,8 dB («TEXTO» en Anton)
    }
    // Sin fuentes, una máscara de texto no se puede dibujar.
    const Json text_mask = file->at("masks")[0].at("mask");
    CHECK_FALSE(gc::render::mask_unsupported({text_mask}).empty());
    CHECK(gc::render::mask_unsupported({text_mask}, &f).empty());
    CHECK_FALSE(gc::render::mask_alpha({text_mask}, 360, 640));
}
