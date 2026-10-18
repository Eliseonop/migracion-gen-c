// Paridad del TEXTO en el render con el export de video-yt: los textos que dibuja
// gc::render::draw_timeline_texts frente a los de compose.render_frame (el .ass de text_ass.py
// dibujado por libass encima del vídeo).
//
// Solo se compila con FFmpeg y Skia (gc_media y gc_render, presets msvc-vcpkg-*).
// Fixture (tools/fixtures/export_fixtures.py → synthetic_text_render_fixtures):
//   tests/fixtures/render/text.json   media_cases.RENDER_TEXT: la timeline, el instante y el PNG.
//
// No es exacta: libass rasteriza y engorda el contorno a su manera y mezcla en yuv420p; Skia
// dibuja los mismos glifos (mismo TTF, forma de HarfBuzz) en RGBA. Se compara la luma BT.709 por
// PSNR en la zona del texto (lo que difiere del fondo en cualquiera de las dos, ampliado 3 px),
// que es donde están las diferencias; en el fotograma entero sale mucho más alto.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <map>
#include <string>
#include <vector>

#include "gc/core/frame.hpp"
#include "gc/core/io.hpp"
#include "gc/core/serialize.hpp"
#include "gc/media/decode.hpp"
#include "gc/media/probe.hpp"
#include "gc/render/canvas.hpp"
#include "gc/render/compose.hpp"
#include "gc/render/png.hpp"
#include "gc/render/text.hpp"

using gc::Json;
using namespace gc::core;

namespace {

const std::filesystem::path kFixtures = path_from_utf8(GC_FIXTURES_DIR);

gc::render::Image load(const std::filesystem::path& path) {
    auto frame = gc::media::frame_at(path, 0.0);
    REQUIRE(frame);
    REQUIRE(frame->has_value());
    return {(*frame)->width, (*frame)->height, std::move((*frame)->pixels)};
}

double luma(const gc::render::Image& img, std::size_t k) {
    return 0.2126 * img.pixels[4 * k] + 0.7152 * img.pixels[4 * k + 1] + 0.0722 * img.pixels[4 * k + 2];
}

// La zona del texto: los píxeles que difieren del fondo (más de 8 de luma) en alguna de las dos
// imágenes, ampliados 3 px.
std::vector<bool> text_zone(const gc::render::Image& bg, const gc::render::Image& a, const gc::render::Image& b) {
    const int w = bg.width;
    const int h = bg.height;
    std::vector<bool> ink(static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
    for (std::size_t i = 0; i < ink.size(); ++i) {
        ink[i] = std::abs(luma(a, i) - luma(bg, i)) > 8 || std::abs(luma(b, i) - luma(bg, i)) > 8;
    }
    std::vector<bool> zone(ink.size(), false);
    constexpr int kGrow = 3;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            if (!ink[static_cast<std::size_t>(y) * w + x]) {
                continue;
            }
            for (int dy = -kGrow; dy <= kGrow; ++dy) {
                for (int dx = -kGrow; dx <= kGrow; ++dx) {
                    const int nx = x + dx;
                    const int ny = y + dy;
                    if (nx >= 0 && ny >= 0 && nx < w && ny < h) {
                        zone[static_cast<std::size_t>(ny) * w + nx] = true;
                    }
                }
            }
        }
    }
    return zone;
}

// PSNR que perdona hasta 1 px de desplazamiento: el error de cada píxel de `a` es lo que se sale
// del rango [mín, máx] de la luma de `b` en su vecindad 3 × 3. Así cuentan un color, una palabra o
// un salto de línea distintos, pero no el antialias ni la deriva de menos de un píxel del avance
// de los glifos (libass usa el hinting de FreeType; Skia, los contornos sin retocar).
double tolerant_psnr(const gc::render::Image& a, const gc::render::Image& b, const std::vector<bool>& only) {
    const int w = a.width;
    const int h = a.height;
    double sum = 0.0;
    std::size_t n = 0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const std::size_t i = static_cast<std::size_t>(y) * w + x;
            if (!only[i]) {
                continue;
            }
            double lo = 255.0;
            double hi = 0.0;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const int nx = std::clamp(x + dx, 0, w - 1);
                    const int ny = std::clamp(y + dy, 0, h - 1);
                    const double v = luma(b, static_cast<std::size_t>(ny) * w + nx);
                    lo = std::min(lo, v);
                    hi = std::max(hi, v);
                }
            }
            const double v = luma(a, i);
            const double d = v < lo ? lo - v : (v > hi ? v - hi : 0.0);
            sum += d * d;
            ++n;
        }
    }
    const double mse = n == 0 ? 0.0 : sum / static_cast<double>(n);
    return mse == 0.0 ? std::numeric_limits<double>::infinity() : 10.0 * std::log10(255.0 * 255.0 / mse);
}

double psnr_luma(const gc::render::Image& a, const gc::render::Image& b, const std::vector<bool>* only = nullptr) {
    double sum = 0.0;
    std::size_t n = 0;
    for (std::size_t i = 0; i < a.pixels.size() / 4; ++i) {
        if (only != nullptr && !(*only)[i]) {
            continue;
        }
        const double d = luma(a, i) - luma(b, i);
        sum += d * d;
        ++n;
    }
    const double mse = n == 0 ? 0.0 : sum / static_cast<double>(n);
    return mse == 0.0 ? std::numeric_limits<double>::infinity() : 10.0 * std::log10(255.0 * 255.0 / mse);
}

} // namespace

TEST_CASE("paridad del render: el texto frente al de libass en el export", "[parity][render][text]") {
    const auto path = kFixtures / "render" / "text.json";
    REQUIRE(std::filesystem::exists(path)); // faltan: ejecuta tools/fixtures/export_fixtures.py
    auto file = read_json_file(path);
    REQUIRE(file);
    gc::media::silence_ffmpeg_log();
    const gc::render::TextFonts fonts(
        FontDirs{default_system_font_dir(), path_from_utf8(GC_RESOURCES_DIR) / "fonts"});
    const char* dump = std::getenv("GC_TEXT_DUMP"); // carpeta donde dejar los PNG del C++ (para mirarlos)
    std::size_t cases = 0;
    std::size_t pending = 0;
    for (const Json& c : *file) {
        const auto name = c.at("name").get<std::string>();
        const double at = c.at("t").get<double>();
        INFO(name << " en " << at);
        auto timeline = timeline_from_json(c.at("timeline"));
        REQUIRE(timeline);
        const int fps = static_cast<int>(timeline->fps);
        const std::int64_t tick = gc::media::export_frame_tick(at, fps);
        const double t = static_cast<double>(tick) / fps;
        const int w = static_cast<int>(timeline->width - timeline->width % 2);
        const int h = static_cast<int>(timeline->height - timeline->height % 2);
        const auto media_path = [](const TimelineClip& clip) {
            return kFixtures / "render" / "files" / path_from_utf8(clip.filename);
        };
        // El fondo (el material, si hay), como en la paridad del render.
        std::vector<gc::render::Image> frames;
        frames.reserve(4);
        auto layers = evaluate(*timeline, t, FrameSpec{static_cast<double>(w), static_cast<double>(h), kRenderLayers},
                               [&](const TimelineClip& clip) -> std::optional<MediaSize> {
                                   const auto img = load(media_path(clip));
                                   return MediaSize{static_cast<double>(img.width), static_cast<double>(img.height)};
                               });
        REQUIRE(layers);
        std::erase_if(*layers, [](const FrameLayer& l) { return l.clip->kind == "text"; }); // el texto va aparte
        auto bg = gc::render::compose_frame(
            *layers, w, h, [&](const FrameLayer& layer) -> gc::Result<const gc::render::Image*> {
                auto got = gc::media::export_frame_at(*layer.clip, media_path(*layer.clip), fps, tick);
                if (!got) {
                    return std::unexpected(got.error());
                }
                REQUIRE(got->has_value());
                frames.push_back({(*got)->width, (*got)->height, std::move((*got)->pixels)});
                return &frames.back();
            });
        REQUIRE(bg);
        auto canvas = gc::render::Canvas::make(w, h);
        REQUIRE(canvas);
        REQUIRE(canvas->draw_image(*bg, SourceCrop{0, 0, static_cast<double>(w), static_cast<double>(h)},
                                   Affine{1, 0, 0, 1, 0, 0}));
        const auto drawn = gc::render::draw_timeline_texts(*canvas, *timeline, tick, fonts);
        INFO((drawn ? std::string() : drawn.error().message));
        if (!drawn && c.value("pending", false) && drawn.error().code == gc::ErrorCode::Unsupported) {
            ++pending; // escena de una etapa siguiente: aún no se dibuja
            continue;
        }
        REQUIRE(drawn);
        const auto frame = canvas->snapshot();
        const auto expected = load(kFixtures / "render" / path_from_utf8(c.at("frame").get<std::string>()));
        REQUIRE(frame.width == expected.width);
        REQUIRE(frame.height == expected.height);
        if (dump != nullptr) {
            (void)gc::render::write_png(frame, path_from_utf8(dump) / path_from_utf8(c.at("frame").get<std::string>()));
        }
        const auto zone = text_zone(*bg, frame, expected);
        const double whole = psnr_luma(frame, expected);
        const double psnr = psnr_luma(frame, expected, &zone);
        const double tolerant = std::min(tolerant_psnr(frame, expected, zone), tolerant_psnr(expected, frame, zone));
        UNSCOPED_INFO(name << " en " << at << ": PSNR de luma " << psnr << " dB en la zona del texto, "
                           << tolerant << " dB perdonando 1 px (" << whole << " dB en todo el fotograma)");
        if (c.contains("divergence")) {
            // el export se aparta de lo que pide el texto (decisión de Edu): solo se dibuja
            UNSCOPED_INFO("  divergencia: " << c.at("divergence").get<std::string>());
        } else if (c.contains("loose")) {
            // el export se aparta un poco (decisión de Edu): solo el PSNR que perdona 1 px
            UNSCOPED_INFO("  se aparta un poco: " << c.at("loose").get<std::string>());
            CHECK(tolerant >= 30.0);
        } else {
            CHECK(psnr >= c.value("min_psnr", 16.0)); // algunas escenas, con su suelo propio
            CHECK(tolerant >= 30.0);
        }
        ++cases;
    }
    UNSCOPED_INFO(pending << " escenas pendientes (aún Unsupported)");
    CHECK(cases >= 20);
}
