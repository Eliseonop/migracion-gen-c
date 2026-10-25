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
#include <deque>
#include <filesystem>
#include <format>
#include <functional>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "gc/core/bg_matte.hpp"
#include "gc/core/font_metrics.hpp"
#include "gc/core/frame.hpp"
#include "gc/core/fx_bake.hpp"
#include "gc/core/fx_catalog.hpp"
#include "gc/core/io.hpp"
#include "gc/core/serialize.hpp"
#include "gc/media/decode.hpp"
#include "gc/media/probe.hpp"
#include "gc/render/compose.hpp"
#include "gc/render/png.hpp"
#include "gc/render/text.hpp"
#include "gc/render/vfx.hpp"

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

// Clips horneados con efectos de vídeo: el horneado del export (BT.601 de rango TV, Chromium y VP9)
// deja el clip más oscuro (medido: 3–4 niveles de luma, también sin ningún efecto activo). Se mide
// ese sesgo en la zona del clip (`covered`: donde el material en blanco cubre el cuadro) y se
// compara el PSNR de las medias de 8×8 sin él.
struct BakedStats {
    double bias = 0.0; // luma media del C++ menos la del export, en la zona del clip
    double psnr = 0.0; // medias de 8×8, con el sesgo quitado en la zona del clip
};

BakedStats baked_stats(const gc::render::Image& a, const gc::render::Image& b, const std::vector<bool>& skip,
                       const gc::render::Image& covered) {
    const auto n = static_cast<std::size_t>(a.width) * static_cast<std::size_t>(a.height);
    const auto in_clip = [&](std::size_t k) { return covered.pixels[(k * 4) + 1] > 250; };
    double sum = 0.0;
    std::size_t count = 0;
    for (std::size_t k = 0; k < n; ++k) {
        if (in_clip(k) && !skip[k]) {
            sum += luma(a, k) - luma(b, k);
            ++count;
        }
    }
    BakedStats st;
    st.bias = count > 0 ? sum / static_cast<double>(count) : 0.0;
    constexpr int kBlock = 8;
    double mse = 0.0;
    std::size_t blocks = 0;
    for (int by = 0; by + kBlock <= a.height; by += kBlock) {
        for (int bx = 0; bx + kBlock <= a.width; bx += kBlock) {
            double d = 0.0;
            bool skipped = false;
            for (int y = by; y < by + kBlock && !skipped; ++y) {
                for (int x = bx; x < bx + kBlock; ++x) {
                    const std::size_t k = (static_cast<std::size_t>(y) * static_cast<std::size_t>(a.width)) +
                                          static_cast<std::size_t>(x);
                    if (skip[k]) {
                        skipped = true;
                        break;
                    }
                    d += luma(a, k) - luma(b, k) - (in_clip(k) ? st.bias : 0.0);
                }
            }
            if (skipped) {
                continue;
            }
            d /= kBlock * kBlock;
            mse += d * d;
            ++blocks;
        }
    }
    mse /= static_cast<double>(std::max<std::size_t>(blocks, 1));
    st.psnr = mse == 0.0 ? std::numeric_limits<double>::infinity() : 10.0 * std::log10(255.0 * 255.0 / mse);
    return st;
}

} // namespace

TEST_CASE("paridad del render: el fotograma compuesto frente al del export", "[parity][render]") {
    const auto path = kFixtures / "render" / "synthetic.json";
    REQUIRE(std::filesystem::exists(path)); // faltan: ejecuta tools/fixtures/export_fixtures.py
    auto file = read_json_file(path);
    REQUIRE(file);
    gc::media::silence_ffmpeg_log();
    // Los efectos de vídeo (vfx) salen del catálogo de la app.
    auto catalog = FxCatalog::load(path_from_utf8(GC_RESOURCES_DIR) / "fx" / "catalog.json");
    REQUIRE(catalog);
    set_fx_catalog(std::make_shared<FxCatalog>(std::move(*catalog)));
    std::map<std::string, gc::render::Image> sources;
    std::map<std::string, gc::render::Image> blanks; // el material en blanco opaco, para la banda
    const char* dump = std::getenv("GC_RENDER_DUMP"); // NOLINT(concurrency-mt-unsafe): carpeta donde dejar los PNG del C++
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
        // «Eliminar fondo»: el matte de la caché de las escenas (render/files/bgcache), como
        // build_timeline_bg_masks (un error, sin matte).
        std::deque<MatteSpec> mattes; // los punteros siguen valiendo al añadir
        const auto matte_of = [&](const TimelineClip& clip) -> const MatteSpec* {
            auto spec = clip_matte_spec(clip, kFixtures / "render" / "files" / "bgcache");
            if (!spec || !*spec) {
                return nullptr;
            }
            mattes.push_back(std::move(**spec));
            return &mattes.back();
        };
        // Dimensiones del material: su primer fotograma.
        const auto source_of = [&](const TimelineClip& clip) -> const gc::render::Image& {
            auto [it, fresh] = sources.try_emplace(clip.filename);
            if (fresh) {
                it->second = load(media_path(clip));
            }
            return it->second;
        };
        // Los clips con efectos de vídeo, horneados como fx_bake: en la timeline va el clip horneado.
        std::map<std::string, gc::render::VfxBakePlan> bakes;
        for (auto& cow : timeline->clips) {
            if (const auto has = has_vfx(*cow); !has || !*has) {
                continue;
            }
            const auto& src = source_of(*cow);
            auto plan = gc::render::plan_vfx_bake(*cow, src.width, src.height, static_cast<int>(timeline->width),
                                                  static_cast<int>(timeline->height), static_cast<int>(timeline->fps));
            REQUIRE(plan);
            if (*plan) {
                cow = (*plan)->baked;
                bakes.emplace(cow->id, std::move(**plan));
            }
        }
        const auto material_size = [&](const TimelineClip& clip) {
            const auto& img = source_of(clip);
            if (const auto it = bakes.find(clip.id); it != bakes.end()) {
                return gc::render::vfx_baked_size(it->second, img.width, img.height);
            }
            return std::pair{img.width, img.height};
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
                                   const auto [mw, mh] = material_size(clip);
                                   return MediaSize{static_cast<double>(mw), static_cast<double>(mh)};
                               });
        REQUIRE(layers);
        const int w = static_cast<int>(out_w);
        const int h = static_cast<int>(out_h);
        auto frame = gc::render::compose_frame(
            *layers, w, h, [&](const FrameLayer& layer) -> gc::Result<const gc::render::Image*> {
                const auto bake = bakes.find(layer.clip->id);
                if (bake != bakes.end()) {
                    // El fotograma m del tramo del clip original, con sus efectos.
                    const auto& plan = bake->second;
                    const std::int64_t m = gc::render::vfx_baked_index(plan, tick, fps);
                    auto got = gc::media::bake_input_frame_at(plan.source, media_path(plan.source), fps, m,
                                                              matte_of(plan.source));
                    if (!got) {
                        return std::unexpected(got.error());
                    }
                    REQUIRE(got->has_value());
                    auto baked = gc::render::vfx_baked_frame(
                        plan, {(*got)->width, (*got)->height, std::move((*got)->pixels)}, m, fps);
                    if (!baked) {
                        return std::unexpected(baked.error());
                    }
                    frames.push_back(std::move(*baked));
                    return &frames.back();
                }
                auto got = gc::media::export_frame_at(*layer.clip, media_path(*layer.clip), fps, tick,
                                                      matte_of(*layer.clip));
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
                const auto [mw, mh] = material_size(*layer.clip); // mismas dimensiones
                auto [it, fresh] = blanks.try_emplace(std::format("{}:{}x{}", layer.clip->filename, mw, mh));
                if (fresh) {
                    it->second = {mw, mh, std::vector<std::uint8_t>(static_cast<std::size_t>(mw) * mh * 4, 255)};
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
        if (name.starts_with("efecto de vídeo")) {
            // El export hornea el clip en un VP9 4:2:0 intermedio (16 Mb/s, BT.601 de rango TV) y lo
            // vuelve a decodificar: pierde detalle y oscurece el clip (divergencia anotada en el TODO;
            // el C++ no lo copia). Se comparan las medias de 8×8 sin ese sesgo, que tiene que estar en
            // lo medido (0–6 niveles más oscuro), y píxel a píxel se pide un suelo menor.
            const BakedStats st = baked_stats(*frame, expected, band, *coverage);
            const double px = psnr_luma(*frame, expected, &band);
            UNSCOPED_INFO(name << " en " << at << ": medias de 8×8 sin el sesgo " << st.psnr << " dB (sesgo "
                               << st.bias << "); píxel a píxel " << px << " dB");
            // Sin el sesgo, el vídeo con CRT queda en 55–57 dB y la sombra en 47; las texturas de color
            // saturado (ondas, degradado), en 33,7–38: el error del horneado depende del color.
            CHECK(st.psnr >= 33.0);
            CHECK(st.bias >= 0.0);
            CHECK(st.bias <= 6.0);
            CHECK(px >= 30.0);
            ++cases;
            continue;
        }
        const double whole = psnr_luma(*frame, expected);
        const double psnr = psnr_luma(*frame, expected, &band);
        const double chroma = psnr_chroma(*frame, expected, band);
        UNSCOPED_INFO(name << " en " << at << ": PSNR de luma " << psnr << " dB sin la banda del borde (" << whole
                           << " dB con ella); de crominancia " << chroma << " dB");
        CHECK(psnr >= 35.0);
        if (name.starts_with("croma con") && name.contains("contorno")) {
            // El contorno del sujeto es fino: sin él, alguna escena aún pasaría de 35 dB (miden ~50,5).
            CHECK(psnr >= 45.0);
        }
        if (name.starts_with("eliminar fondo: ")) {
            // Medido: 50,96–51,10 dB. Con 35, pasaban el matte escalado con el vecino más cercano y
            // el croma sin el matte multiplicado.
            CHECK(psnr >= 48.0);
        }
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
