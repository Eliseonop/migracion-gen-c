// Paridad del marco de TikTok/Shorts del preview (gc/render/platform.hpp) con
// features/editor/render/platformChrome.js de video-yt, dibujado por el JS real en el Canvas 2D de
// Chromium (por software).
//
// Fixture (export_fixtures.py --solo plataforma):
//   tests/fixtures/render/platform.json   las escenas (lienzo, recuadro, marco, fondo) y el PNG.
//
// No es exacta: Skia por CPU frente al Canvas 2D de Chromium (también Skia, pero con su propio
// antialias de trazados y sombras) y los textos sin HarfBuzz (sin interletraje). Se compara la luma
// de los colores premultiplicados y el alfa por PSNR; los casos que no dibujan nada, exactos.

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <string>
#include <utility>

#include "gc/core/io.hpp"
#include "gc/media/decode.hpp"
#include "gc/media/probe.hpp"
#include "gc/render/canvas.hpp"
#include "gc/render/platform.hpp"
#include "gc/render/png.hpp"

using gc::Json;

namespace {

const std::filesystem::path kFixtures = gc::core::path_from_utf8(GC_FIXTURES_DIR);

gc::render::Image load(const std::filesystem::path& path) {
    auto frame = gc::media::frame_at(path, 0.0);
    REQUIRE(frame);
    REQUIRE(frame->has_value());
    return {(*frame)->width, (*frame)->height, std::move((*frame)->pixels)};
}

struct Psnr {
    double luma = 0;
    double alpha = 0;
};

// La luma BT.709 de los colores premultiplicados (así un píxel transparente no cuenta su color) y
// el alfa.
Psnr psnr(const gc::render::Image& a, const gc::render::Image& b) {
    double sl = 0.0;
    double sa = 0.0;
    const std::size_t n = a.pixels.size() / 4;
    const auto luma = [](const gc::render::Image& img, std::size_t k) {
        const double al = img.pixels[(4 * k) + 3] / 255.0;
        return al * ((0.2126 * img.pixels[4 * k]) + (0.7152 * img.pixels[(4 * k) + 1]) +
                     (0.0722 * img.pixels[(4 * k) + 2]));
    };
    for (std::size_t k = 0; k < n; ++k) {
        const double dl = luma(a, k) - luma(b, k);
        const double da = static_cast<double>(a.pixels[(4 * k) + 3]) - b.pixels[(4 * k) + 3];
        sl += dl * dl;
        sa += da * da;
    }
    const auto db = [&](double sum) {
        const double mse = sum / static_cast<double>(n);
        return mse == 0.0 ? std::numeric_limits<double>::infinity() : 10.0 * std::log10(255.0 * 255.0 / mse);
    };
    return {db(sl), db(sa)};
}

gc::render::Rgba8 hex_color(const std::string& hex) {
    const auto v = static_cast<unsigned>(std::stoul(hex.substr(1), nullptr, 16));
    return {static_cast<std::uint8_t>((v >> 16U) & 255U), static_cast<std::uint8_t>((v >> 8U) & 255U),
            static_cast<std::uint8_t>(v & 255U), 255};
}

} // namespace

TEST_CASE("paridad del marco de plataforma (platformChrome.js en Chromium)", "[parity][render][platform]") {
    const auto path = kFixtures / "render" / "platform.json";
    REQUIRE(std::filesystem::exists(path)); // faltan: export_fixtures.py --solo plataforma
    auto file = gc::core::read_json_file(path);
    REQUIRE(file);
    gc::media::silence_ffmpeg_log();
    const char* dump = std::getenv("GC_PLATFORM_DUMP"); // NOLINT(concurrency-mt-unsafe): carpeta donde dejar los PNG del C++
    std::size_t drawn = 0;
    for (const Json& c : *file) {
        const auto name = c.at("name").get<std::string>();
        INFO(name);
        // Las etiquetas del selector, como PLATFORM_OVERLAYS y overlayLabel.
        const Json& overlays = c.at("overlays");
        REQUIRE(overlays.size() == gc::render::kPlatformOverlays.size());
        for (std::size_t i = 0; i < overlays.size(); ++i) {
            CHECK(overlays[i].at("id").get<std::string>() == gc::render::kPlatformOverlays[i].id);
            CHECK(overlays[i].at("label").get<std::string>() == gc::render::kPlatformOverlays[i].label);
        }
        for (const Json& l : c.at("labels")) {
            CHECK(gc::render::overlay_label(l.at(0).get<std::string>()) == l.at(1).get<std::string>());
        }

        const int w = c.at("width").get<int>();
        const int h = c.at("height").get<int>();
        auto canvas = gc::render::Canvas::make(w, h);
        REQUIRE(canvas);
        const bool has_bg = !c.at("background").is_null();
        canvas->clear(has_bg ? hex_color(c.at("background").get<std::string>()) : gc::render::Rgba8{0, 0, 0, 0});
        const Json& fr = c.at("frame");
        gc::render::draw_platform_chrome(*canvas, fr.at(0).get<double>(), fr.at(1).get<double>(), fr.at(2).get<double>(),
                                         fr.at(3).get<double>(), c.at("kind").get<std::string>());
        const auto got = canvas->snapshot();
        const auto want = load(kFixtures / "render" / gc::core::path_from_utf8(c.at("png").get<std::string>()));
        REQUIRE(got.width == want.width);
        REQUIRE(got.height == want.height);
        if (dump != nullptr) {
            (void)gc::render::write_png(got, gc::core::path_from_utf8(dump) /
                                                 gc::core::path_from_utf8(c.at("png").get<std::string>()).filename());
        }
        const Psnr p = psnr(got, want);
        UNSCOPED_INFO(name << ": PSNR de luma " << p.luma << " dB, de alfa " << p.alpha << " dB");
        const std::string kind = c.at("kind").get<std::string>();
        if ((kind != "tiktok" && kind != "shorts") || !(fr.at(2).get<double>() > 0)) {
            const bool same = got.pixels == want.pixels; // no dibuja nada: el fondo tal cual
            CHECK(same);
            continue;
        }
        // Medido: luma de 35,2 a 40,5 dB (lo que queda es el antialias de los glifos y de las
        // sombras) y alfa de 40,3 dB o más (inf con fondo). Sin la sombra fallan 7 de los 9 casos.
        CHECK(p.luma >= 35.0);
        CHECK(p.alpha >= 40.0);
        ++drawn;
    }
    CHECK(drawn == 9);
}
