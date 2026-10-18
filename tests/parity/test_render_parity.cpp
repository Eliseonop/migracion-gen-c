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
// la crominancia 4:2:0 del export no cuenta (las escenas de efectos la comparan además por bloques
// de 2×2, con un suelo propio: kEffectFloors). Mientras no se decida si el render redondea la
// geometría como el export (notas de F3), el PSNR excluye una banda de kEdge px alrededor del
// borde de lo que cubren los clips: ahí el export cae hasta un píxel más allá y, contra el
// fondo negro, eso solo ya baja el fotograma entero a ~31 dB (dentro de la banda, ~11 dB).

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <limits>
#include <map>
#include <string>
#include <vector>

#include "gc/core/font_metrics.hpp"
#include "gc/core/frame.hpp"
#include "gc/core/io.hpp"
#include "gc/core/serialize.hpp"
#include "gc/media/decode.hpp"
#include "gc/media/probe.hpp"
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

// PSNR de la crominancia (Cb y Cr BT.709) por bloques de 2×2 enteros fuera de `skip`: la media
// de cada bloque, para que el 4:2:0 del export no cuente. Ve lo que la luma no ve (la sepia, la
// saturación de un eq o el tono).
double psnr_chroma(const gc::render::Image& a, const gc::render::Image& b, const std::vector<bool>& skip) {
    const auto chroma = [](const gc::render::Image& img, std::size_t k, double& cb, double& cr) {
        const double y = luma(img, k);
        cb += (img.pixels[4 * k + 2] - y) / 1.8556;
        cr += (img.pixels[4 * k] - y) / 1.5748;
    };
    double sum = 0.0;
    std::size_t n = 0;
    for (int y = 0; y + 2 <= a.height; y += 2) {
        for (int x = 0; x + 2 <= a.width; x += 2) {
            const std::size_t k0 = (static_cast<std::size_t>(y) * static_cast<std::size_t>(a.width)) +
                                   static_cast<std::size_t>(x);
            const std::size_t ks[] = {k0, k0 + 1, k0 + static_cast<std::size_t>(a.width),
                                      k0 + static_cast<std::size_t>(a.width) + 1};
            if (std::ranges::any_of(ks, [&](std::size_t k) { return skip[k]; })) {
                continue;
            }
            double cb_a = 0.0;
            double cr_a = 0.0;
            double cb_b = 0.0;
            double cr_b = 0.0;
            for (const std::size_t k : ks) {
                chroma(a, k, cb_a, cr_a);
                chroma(b, k, cb_b, cr_b);
            }
            sum += (((cb_a - cb_b) / 4.0) * ((cb_a - cb_b) / 4.0)) + (((cr_a - cr_b) / 4.0) * ((cr_a - cr_b) / 4.0));
            n += 2;
        }
    }
    const double mse = sum / static_cast<double>(n);
    return mse == 0.0 ? std::numeric_limits<double>::infinity() : 10.0 * std::log10(255.0 * 255.0 / mse);
}

// Suelos de las escenas de efectos (luma y crominancia, sin la banda del borde): 3 dB por debajo
// de lo medido (2026-10-17). Con solo los 35 dB de luma de las demás pasaban una nitidez al doble,
// una sepia o una saturación del VHS cambiadas, porque la luma casi no ve el color.
struct Floor {
    double luma;
    double chroma;
};
const std::map<std::string, Floor, std::less<>> kEffectFloors = {
    {"efecto: desenfoque 2", {42, 50}},
    {"efecto: desenfoque 6,5 en un png con alfa (también se desenfoca el alfa)", {42, 51}},
    {"efecto: nitidez 1,5", {42, 49}},
    {"efecto: blanco y negro", {43, 52}},
    {"efecto: sepia", {42, 51}},
    {"efecto: VHS", {42, 50}},
    {"efecto: brillo, contraste y saturación", {41, 48}},
    {"efecto: contraste 10 (la tabla de eq)", {40, 42}},
    {"efecto: exposición, temperatura y tono", {41, 48}},
    {"efecto: desenfoque, nitidez, sepia, brillo y blancos (el orden de la cadena)", {42, 51}},
    {"efecto: efectos con un filtro de color", {42, 51}},
    {"efecto: desenfoque y saturación en un vídeo con fundido de entrada", {39, 50}},
    {"efecto: VHS y desenfoque en un clip al 60 %", {42, 51}},
    {"efecto: desenfoque y brillo con máscara de ajuste", {41, 50}},
    {"efecto: pixelado 8 en relleno", {41, 35}},
    {"efecto: figura con desenfoque", {42, 52}},
};

// Con grano (aleatorio: no es el mismo que el de FFmpeg) no se comparan los píxeles. Por bloques
// de 8×8 enteros fuera de la banda del borde: el PSNR de sus medias de luma y la fuerza del
// ruido de cada imagen (la desviación de la luma respecto a la media de su bloque).
struct NoiseStats {
    double psnr = 0.0;
    double noise_a = 0.0;
    double noise_b = 0.0;
};

NoiseStats noise_stats(const gc::render::Image& a, const gc::render::Image& b, const std::vector<bool>& skip) {
    constexpr int kBlock = 8;
    double mse = 0.0;
    double var_a = 0.0;
    double var_b = 0.0;
    std::size_t blocks = 0;
    std::size_t pixels = 0;
    for (int by = 0; by + kBlock <= a.height; by += kBlock) {
        for (int bx = 0; bx + kBlock <= a.width; bx += kBlock) {
            std::vector<std::size_t> idx;
            for (int y = by; y < by + kBlock; ++y) {
                for (int x = bx; x < bx + kBlock; ++x) {
                    idx.push_back((static_cast<std::size_t>(y) * static_cast<std::size_t>(a.width)) +
                                  static_cast<std::size_t>(x));
                }
            }
            if (std::ranges::any_of(idx, [&](std::size_t k) { return skip[k]; })) {
                continue;
            }
            double ma = 0.0;
            double mb = 0.0;
            for (const std::size_t k : idx) {
                ma += luma(a, k);
                mb += luma(b, k);
            }
            ma /= static_cast<double>(idx.size());
            mb /= static_cast<double>(idx.size());
            mse += (ma - mb) * (ma - mb);
            for (const std::size_t k : idx) {
                var_a += (luma(a, k) - ma) * (luma(a, k) - ma);
                var_b += (luma(b, k) - mb) * (luma(b, k) - mb);
            }
            ++blocks;
            pixels += idx.size();
        }
    }
    mse /= static_cast<double>(blocks);
    return {mse == 0.0 ? std::numeric_limits<double>::infinity() : 10.0 * std::log10(255.0 * 255.0 / mse),
            std::sqrt(var_a / static_cast<double>(pixels)), std::sqrt(var_b / static_cast<double>(pixels))};
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
    const char* dump = std::getenv("GC_RENDER_DUMP"); // carpeta donde dejar los PNG del C++ (para mirarlos)
    // Las fuentes de las máscaras de texto.
    const gc::render::TextFonts fonts(
        FontDirs{default_system_font_dir(), path_from_utf8(GC_RESOURCES_DIR) / "fonts"});
    std::size_t cases = 0;
    for (const Json& c : *file) {
        const auto name = c.at("name").get<std::string>();
        const double at = c.at("t").get<double>(); // el `at` que recibió compose.render_frame
        INFO(name << " en " << at);
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
        const std::int64_t tick = gc::media::export_frame_tick(at, fps);
        const double t = static_cast<double>(tick) / fps;
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
                auto got = gc::media::export_frame_at(*layer.clip, media_path(*layer.clip), fps, tick);
                if (!got) {
                    return std::unexpected(got.error());
                }
                REQUIRE(got->has_value());
                frames.push_back({(*got)->width, (*got)->height, std::move((*got)->pixels)});
                return &frames.back();
            },
            &fonts);
        REQUIRE(frame);
        auto coverage = gc::render::compose_frame(
            *layers, w, h, [&](const FrameLayer& layer) -> gc::Result<const gc::render::Image*> {
                const auto& src = source_of(*layer.clip); // mismas dimensiones
                auto [it, fresh] = blanks.try_emplace(layer.clip->filename);
                if (fresh) {
                    it->second = {src.width, src.height, std::vector<std::uint8_t>(src.pixels.size(), 255)};
                }
                return &it->second;
            },
            &fonts);
        REQUIRE(coverage);
        const auto expected = load(kFixtures / "render" / path_from_utf8(c.at("frame").get<std::string>()));
        REQUIRE(frame->width == expected.width);
        REQUIRE(frame->height == expected.height);
        if (dump != nullptr) {
            (void)gc::render::write_png(*frame, path_from_utf8(dump) / path_from_utf8(c.at("frame").get<std::string>()));
        }
        const auto band = edge_band(*coverage);
        if (c.value("noise", false)) {
            const NoiseStats st = noise_stats(*frame, expected, band);
            UNSCOPED_INFO(name << " en " << at << ": PSNR de las medias de 8×8 " << st.psnr << " dB; ruido "
                               << st.noise_a << " (C++) frente a " << st.noise_b << " (export)");
            CHECK(st.psnr >= 35.0);
            CHECK(st.noise_a >= 0.75 * st.noise_b);
            CHECK(st.noise_a <= 1.33 * st.noise_b);
            ++cases;
            continue;
        }
        const double whole = psnr_luma(*frame, expected);
        const double psnr = psnr_luma(*frame, expected, &band);
        const double chroma = psnr_chroma(*frame, expected, band);
        UNSCOPED_INFO(name << " en " << at << ": PSNR de luma " << psnr << " dB sin la banda del borde (" << whole
                           << " dB con ella); de crominancia " << chroma << " dB");
        CHECK(psnr >= 35.0);
        if (name.starts_with("efecto: ")) {
            const auto floor = kEffectFloors.find(name);
            REQUIRE(floor != kEffectFloors.end()); // una escena de efectos nueva necesita su suelo
            CHECK(psnr >= floor->second.luma);
            CHECK(chroma >= floor->second.chroma);
        }
        ++cases;
    }
    CHECK(cases >= 5);
}
