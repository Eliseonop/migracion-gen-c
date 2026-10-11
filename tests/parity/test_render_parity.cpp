// Paridad del RENDER con el export de video-yt: el fotograma que compone gc_render (evaluate +
// compose_frame) frente al que da compose.render_frame (la tubería del export con un solo PNG).
//
// Solo se compila con FFmpeg y Skia (gc_media y gc_render, presets msvc-vcpkg-*).
// Fixture (tools/fixtures/export_fixtures.py → synthetic_render_fixtures):
//   tests/fixtures/render/synthetic.json   media_cases.RENDER: la timeline normalizada por
//                                          Python, el instante y el PNG del compuesto real.
//
// No es exacta: el export compone en yuv420p con swscale (crop, scale y rotate de FFmpeg) y
// redondea el destino a píxeles pares (dest_rect_even); el C++ dibuja en RGBA con Skia y el
// destino en coma flotante. Se compara la luma (BT.709) por PSNR, como en el plan (≥ 35 dB):
// la crominancia 4:2:0 del export no cuenta. Mientras no se decida si el render redondea la
// geometría como el export (notas de F3), el PSNR excluye una banda de kEdge px alrededor del
// borde de lo que cubren los clips: ahí el export cae hasta un píxel más allá y, contra el
// fondo negro, eso solo ya baja el fotograma entero a ~31 dB (dentro de la banda, ~11 dB).

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
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
#include "gc/render/compose.hpp"

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

constexpr int kEdge = 2;

double luma(const gc::render::Image& img, std::size_t k) {
    return 0.2126 * img.pixels[4 * k] + 0.7152 * img.pixels[4 * k + 1] + 0.0722 * img.pixels[4 * k + 2];
}

// Los píxeles a kEdge px o menos (en las dos direcciones) de un borde entre lo que cubren los
// clips y el fondo. `coverage`: el fotograma con cada material sustituido por blanco opaco.
std::vector<bool> edge_band(const gc::render::Image& coverage) {
    const int w = coverage.width;
    const int h = coverage.height;
    const auto covered = [&](int x, int y) {
        return luma(coverage, static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)) > 0;
    };
    std::vector<bool> band(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), false);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            bool in = false;
            bool out = false;
            for (int dy = -kEdge; dy <= kEdge; ++dy) {
                for (int dx = -kEdge; dx <= kEdge; ++dx) {
                    const int nx = x + dx;
                    const int ny = y + dy;
                    if (nx >= 0 && ny >= 0 && nx < w && ny < h) {
                        (covered(nx, ny) ? in : out) = true;
                    }
                }
            }
            band[static_cast<std::size_t>(y) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x)] = in && out;
        }
    }
    return band;
}

// PSNR de la luma BT.709 (R, G y B a pantalla completa, 0–255) entre dos imágenes RGBA opacas,
// sin los píxeles de `skip` (si hay).
double psnr_luma(const gc::render::Image& a, const gc::render::Image& b, const std::vector<bool>* skip = nullptr) {
    double sum = 0.0;
    std::size_t n = 0;
    for (std::size_t i = 0; i < a.pixels.size() / 4; ++i) {
        if (skip != nullptr && (*skip)[i]) {
            continue;
        }
        const double d = luma(a, i) - luma(b, i);
        sum += d * d;
        ++n;
    }
    const double mse = sum / static_cast<double>(n);
    return mse == 0.0 ? std::numeric_limits<double>::infinity() : 10.0 * std::log10(255.0 * 255.0 / mse);
}

} // namespace

TEST_CASE("paridad del render: el fotograma compuesto frente al del export", "[parity][render]") {
    const auto path = kFixtures / "render" / "synthetic.json";
    REQUIRE(std::filesystem::exists(path)); // faltan: ejecuta tools/fixtures/export_fixtures.py
    auto file = read_json_file(path);
    REQUIRE(file);
    gc::media::silence_ffmpeg_log();
    std::map<std::string, gc::render::Image> sources;
    std::map<std::string, gc::render::Image> blanks; // el material en blanco opaco, para la banda
    std::size_t cases = 0;
    for (const Json& c : *file) {
        const auto name = c.at("name").get<std::string>();
        const double t = c.at("t").get<double>();
        INFO(name << " en " << t);
        auto timeline = timeline_from_json(c.at("timeline"));
        REQUIRE(timeline);
        const auto media_path = [](const TimelineClip& clip) {
            return kFixtures / "render" / "files" / path_from_utf8(clip.filename);
        };
        // Dimensiones del material: su primer fotograma.
        const auto source_of = [&](const TimelineClip& clip) -> const gc::render::Image& {
            auto [it, fresh] = sources.try_emplace(clip.filename);
            if (fresh) {
                it->second = load(media_path(clip));
            }
            return it->second;
        };
        // El fotograma que el export compone en `t` (export_frame_at), uno por capa.
        std::vector<gc::render::Image> frames;
        frames.reserve(timeline->clips.size() * 2);
        const int fps = static_cast<int>(timeline->fps);
        // Pares, como el export (W -= W % 2): las de la fixture ya lo son.
        const auto out_w = static_cast<double>(timeline->width - timeline->width % 2);
        const auto out_h = static_cast<double>(timeline->height - timeline->height % 2);
        auto layers = evaluate(*timeline, t, FrameSpec{out_w, out_h, kRenderLayers},
                               [&](const TimelineClip& clip) -> std::optional<MediaSize> {
                                   const auto& img = source_of(clip);
                                   return MediaSize{static_cast<double>(img.width), static_cast<double>(img.height)};
                               });
        REQUIRE(layers);
        const int w = static_cast<int>(out_w);
        const int h = static_cast<int>(out_h);
        auto frame = gc::render::compose_frame(
            *layers, w, h, [&](const FrameLayer& layer) -> gc::Result<const gc::render::Image*> {
                auto got = gc::media::export_frame_at(*layer.clip, media_path(*layer.clip), fps, t);
                if (!got) {
                    return std::unexpected(got.error());
                }
                REQUIRE(got->has_value());
                frames.push_back({(*got)->width, (*got)->height, std::move((*got)->pixels)});
                return &frames.back();
            });
        REQUIRE(frame);
        auto coverage = gc::render::compose_frame(
            *layers, w, h, [&](const FrameLayer& layer) -> gc::Result<const gc::render::Image*> {
                const auto& src = source_of(*layer.clip); // mismas dimensiones
                auto [it, fresh] = blanks.try_emplace(layer.clip->filename);
                if (fresh) {
                    it->second = {src.width, src.height, std::vector<std::uint8_t>(src.pixels.size(), 255)};
                }
                return &it->second;
            });
        REQUIRE(coverage);
        const auto expected = load(kFixtures / "render" / path_from_utf8(c.at("frame").get<std::string>()));
        REQUIRE(frame->width == expected.width);
        REQUIRE(frame->height == expected.height);
        const auto band = edge_band(*coverage);
        const double whole = psnr_luma(*frame, expected);
        const double psnr = psnr_luma(*frame, expected, &band);
        UNSCOPED_INFO(name << ": PSNR de luma " << psnr << " dB sin la banda del borde (" << whole
                           << " dB con ella)");
        CHECK(psnr >= 35.0);
        ++cases;
    }
    CHECK(cases >= 5);
}
