// El bicúbico separable de Canvas::draw_image (src/render/src/resample.hpp) frente al de Skia, que
// es la referencia: los mismos píxeles pintados y, en cada uno, como mucho 1 nivel de diferencia
// en muy pocos (el orden de las sumas en float). Escalas que suben y bajan, volteos, traslaciones
// con fracción, recortes (enteros, con fracción y al borde), opacidades, matrices de color (las de
// los filtros, y una que se sale de 0–1), fuentes con alfa, dibujos que se salen del lienzo y dentro
// de una capa.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

#include "gc/core/clip_filters.hpp"
#include "gc/render/canvas.hpp"
#include "resample.hpp"

using namespace gc;

namespace {

// Una fuente con detalle en los dos ejes, bordes duros y alfa variable (también 0 y 255).
render::Image source(int w, int h, std::uint32_t seed) {
    render::Image img{w, h, std::vector<std::uint8_t>(static_cast<std::size_t>(w) * h * 4)};
    std::uint32_t s = seed;
    const auto next = [&] {
        s = (s * 1664525U) + 1013904223U;
        return s >> 24U;
    };
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const auto i = ((static_cast<std::size_t>(y) * w) + x) * 4;
            img.pixels[i] = static_cast<std::uint8_t>((x * 37 + y * 11) & 0xFF);
            img.pixels[i + 1] = static_cast<std::uint8_t>(((x / 3) % 2 == 0) ? 230 : 20);
            img.pixels[i + 2] = static_cast<std::uint8_t>(next());
            const std::uint32_t a = next();
            img.pixels[i + 3] = static_cast<std::uint8_t>(a < 40 ? 0 : (a > 200 ? 255 : a));
        }
    }
    return img;
}

struct Case {
    std::string name;
    core::SourceCrop src;
    core::Affine m;
    double alpha = 1.0;
    bool layer = false;
    std::optional<core::AffineColorMatrix> color;
    // Tras el dibujo, la capa de ajuste (apply_color_matrix, que hace una instantánea del lienzo) y
    // otra vez el mismo dibujo: el segundo escribe en un lienzo con una instantánea por medio.
    std::optional<core::AffineColorMatrix> adjust;
};

// Dibuja el caso sobre un fondo con dibujo y devuelve el lienzo (sin premultiplicar).
render::Image draw(const render::Image& img, const Case& c, bool fast) {
    render::detail::set_fast_scaled_draw(fast);
    auto canvas = render::Canvas::make(96, 72);
    REQUIRE(canvas);
    canvas->clear({30, 60, 90, 255});
    canvas->fill_rect(10, 8, 50, 30, {200, 40, 10, 255});
    if (c.layer) {
        canvas->begin_layer("multiply");
    }
    REQUIRE(canvas->draw_image(img, c.src, c.m, c.alpha, std::nullopt, c.color));
    if (c.adjust) {
        canvas->apply_color_matrix(*c.adjust);
        REQUIRE(canvas->draw_image(img, c.src, c.m, c.alpha, std::nullopt, c.color));
    }
    if (c.layer) {
        canvas->end_layer();
    }
    render::detail::set_fast_scaled_draw(true);
    return canvas->snapshot();
}

} // namespace

TEST_CASE("draw_image: el bicúbico separable da lo mismo que el de Skia", "[render][resample]") {
    const render::Image img = source(40, 30, 7);
    const core::SourceCrop full{0, 0, 40, 30};
    const std::vector<Case> cases = {
        {"identidad", full, {1, 0, 0, 1, 3, 2}},
        {"traslación con fracción", full, {1, 0, 0, 1, 3.3, 2.71}},
        {"bordes justo en ,5 (floor(v + 0,5), no al par)", full, {1, 0, 0, 1, 2.5, 4.5}},
        {"escala ×1,915", full, {1.915, 0, 0, 1.915, -4.2, -3.7}},
        {"escala ×2 entera", full, {2, 0, 0, 2, 0, 0}},
        {"escala ×4,12 (como el tráiler)", full, {4.1244, 0, 0, 4.1244, -30.5, -20.25}},
        {"escala distinta en x e y", full, {1.5, 0, 0, 0.8, 5.5, 7.25}},
        {"reducción ×0,5", full, {0.5, 0, 0, 0.5, 10.1, 9.9}},
        {"reducción ×0,3", full, {0.3, 0, 0, 0.3, 1.0, 1.0}},
        {"casi 1", full, {1.0001, 0, 0, 0.9999, 0.49, 0.51}},
        {"volteo horizontal", full, {-1.7, 0, 0, 1.7, 80.3, 2.2}},
        {"volteo vertical", full, {1.3, 0, 0, -1.3, 4.4, 60.6}},
        {"los dos volteos", full, {-2.2, 0, 0, -2.2, 90.1, 70.7}},
        {"recorte entero", {8, 5, 20, 15}, {2.5, 0, 0, 2.5, -10.0, -6.0}},
        {"recorte con fracción", {7.4, 3.6, 21.3, 17.9}, {2.25, 0, 0, 2.25, -12.3, -4.1}},
        {"recorte desde el origen", {0, 0, 25, 20}, {3.1, 0, 0, 3.1, 0.7, 0.2}},
        {"recorte hasta el borde", {13.5, 9.25, 26.5, 20.75}, {1.8, 0, 0, 1.8, -2.0, 3.0}},
        {"recorte de una columna", {19.5, 0, 1, 30}, {20, 0, 0, 2, 10, 5}},
        {"medio fuera del lienzo", full, {3.3, 0, 0, 3.3, 40.2, 30.6}},
        {"fuera por arriba a la izquierda", full, {2.9, 0, 0, 2.9, -50.5, -40.25}},
        {"enorme", {18, 12, 4, 3}, {60, 0, 0, 60, -1000.3, -700.7}},
        {"opacidad 0,5", full, {1.915, 0, 0, 1.915, -4.2, -3.7}, 0.5},
        {"opacidad 0,01", full, {2.2, 0, 0, 2.2, 1, 1}, 0.01},
        {"opacidad 0", full, {2.2, 0, 0, 2.2, 1, 1}, 0.0},
        {"en una capa con fusión", full, {1.915, 0, 0, 1.915, -4.2, -3.7}, 0.8, true},
        {"filtro vintage", full, {1.915, 0, 0, 1.915, -4.2, -3.7}, 1.0, false, core::filter_matrix("vintage")},
        {"filtro desvaído (con desplazamiento)", full, {2.3, 0, 0, 2.3, 1.5, 0.5}, 1.0, false, core::filter_matrix("faded")},
        {"filtro noir con opacidad", full, {1.5, 0, 0, 1.5, 2.2, 3.3}, 0.6, false, core::filter_matrix("noir")},
        {"filtro cálido en una capa", {7.4, 3.6, 21.3, 17.9}, {2.25, 0, 0, 2.25, -12.3, -4.1}, 0.9, true, core::filter_matrix("warm")},
        {"matriz que se sale de 0–1", full, {1.2, 0, 0, 1.2, 0.3, 0.3}, 1.0, false,
         core::AffineColorMatrix{{{1.8, -0.4, 0.1, 0.2}, {0.3, 1.5, -0.6, -0.3}, {-0.2, 0.2, 2.0, 0.05}}}},
        {"otra vez tras la capa de ajuste", full, {1.915, 0, 0, 1.915, -4.2, -3.7}, 0.7, false, std::nullopt,
         core::filter_matrix("noir")},
    };
    std::size_t total_off = 0;
    std::size_t total = 0;
    for (const Case& c : cases) {
        INFO(c.name);
        const render::Image ref = draw(img, c, false);
        const render::Image got = draw(img, c, true);
        REQUIRE(got.pixels.size() == ref.pixels.size());
        int worst = 0;
        std::size_t off = 0;
        for (std::size_t i = 0; i < got.pixels.size(); ++i) {
            const int d = std::abs(int{got.pixels[i]} - int{ref.pixels[i]});
            worst = std::max(worst, d);
            off += d > 0 ? 1 : 0;
        }
        UNSCOPED_INFO(c.name << ": " << off << " de " << got.pixels.size() << " canales distintos, como mucho " << worst);
        CHECK(worst <= 1);
        total_off += off;
        total += got.pixels.size();
    }
    // Medido: 5 canales de ~860 000. Un fallo que mueva ±1 en unos cientos de canales no pasa.
    CHECK(total_off <= 30);
    CHECK(total > 0);
}

TEST_CASE("draw_image: el bicúbico separable no se usa con giro, sesgo ni una escala rara", "[render][resample]") {
    const render::Image img = source(20, 16, 3);
    const core::SourceCrop full{0, 0, 20, 16};
    render::detail::PremulPixels px;
    std::vector<std::uint8_t> buf(std::size_t{64} * 64 * 4);
    px.pixels = buf.data();
    px.row_bytes = std::size_t{64} * 4;
    px.width = 64;
    px.height = 64;
    const render::detail::PixelRect all{0, 0, 64, 64};
    CHECK_FALSE(render::detail::draw_scaled_bicubic(img, full, {1, 0.2, 0, 1, 0, 0}, 1.0F, std::nullopt, px, all));
    CHECK_FALSE(render::detail::draw_scaled_bicubic(img, full, {1, 0, -0.1, 1, 0, 0}, 1.0F, std::nullopt, px, all));
    CHECK_FALSE(render::detail::draw_scaled_bicubic(img, full, {0, 0, 0, 1, 0, 0}, 1.0F, std::nullopt, px, all));
    CHECK_FALSE(render::detail::draw_scaled_bicubic(img, full, {1, 0, 0, 1, 1e40, 0}, 1.0F, std::nullopt, px, all));
    CHECK(std::ranges::all_of(buf, [](std::uint8_t v) { return v == 0; })); // sin tocar nada
    CHECK(render::detail::draw_scaled_bicubic(img, full, {2, 0, 0, 2, 3, 3}, 1.0F, std::nullopt, px, all));
}

// Cuánto cuesta un dibujo a pantalla completa en 4K (la imagen de fondo de «passagers»: ~1128×2005
// escalada ×1,915), con el bicúbico separable y con el de Skia. Oculto: `gc_tests "[bench]"`.
TEST_CASE("draw_image: tiempo de un dibujo a pantalla completa en 4K", "[.][bench][resample]") {
    const render::Image img = source(1128, 2005, 1);
    auto canvas = render::Canvas::make(2160, 3840);
    REQUIRE(canvas);
    const core::SourceCrop full{0, 0, 1128, 2005};
    const core::Affine m{1.915, 0, 0, 1.915, -0.3, 0.2};
    for (const bool vintage : {false, true}) {
        const auto color = vintage ? std::optional(core::filter_matrix("vintage")) : std::nullopt;
        for (const bool fast : {true, false, true, false}) {
            render::detail::set_fast_scaled_draw(fast);
            canvas->clear({0, 0, 0, 255});
            const auto t0 = std::chrono::steady_clock::now();
            constexpr int kRuns = 5;
            for (int k = 0; k < kRuns; ++k) {
                REQUIRE(canvas->draw_image(img, full, m, 1.0, std::nullopt, color));
            }
            const double ms =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / kRuns;
            WARN((fast ? "separable" : "Skia") << (vintage ? " con el filtro vintage: " : ": ") << ms
                                               << " ms por dibujo");
        }
    }
    render::detail::set_fast_scaled_draw(true);
}
