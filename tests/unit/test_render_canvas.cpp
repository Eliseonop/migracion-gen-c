// gc_render: el lienzo (Skia por CPU). Solo se compila con Skia (presets msvc-vcpkg-*).
// Valores calculados a mano: src-over sin antialias sobre RGBA de 8 bits.

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <numbers>

#include "gc/render/canvas.hpp"

using gc::render::Canvas;
using gc::render::Image;
using gc::render::Rgba8;

namespace {

Rgba8 pixel(const Image& img, int x, int y) {
    const auto i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(img.width) + static_cast<std::size_t>(x)) * 4;
    return {img.pixels[i], img.pixels[i + 1], img.pixels[i + 2], img.pixels[i + 3]};
}

bool same(Rgba8 a, Rgba8 b) { return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a; }

} // namespace

TEST_CASE("lienzo: dimensiones no válidas son un error", "[render]") {
    CHECK_FALSE(Canvas::make(0, 10));
    CHECK_FALSE(Canvas::make(10, -1));
}

TEST_CASE("lienzo: limpiar y rellenar un rectángulo", "[render]") {
    auto canvas = Canvas::make(8, 6);
    REQUIRE(canvas);
    CHECK(canvas->width() == 8);
    CHECK(canvas->height() == 6);

    canvas->clear({0, 0, 0, 0});
    canvas->fill_rect(2, 1, 3, 2, {255, 0, 0, 255});
    const Image img = canvas->snapshot();
    REQUIRE(img.width == 8);
    REQUIRE(img.height == 6);
    REQUIRE(img.pixels.size() == std::size_t{8 * 6 * 4});

    std::size_t red = 0;
    for (int y = 0; y < 6; ++y) {
        for (int x = 0; x < 8; ++x) {
            const bool inside = x >= 2 && x < 5 && y >= 1 && y < 3;
            const Rgba8 got = pixel(img, x, y);
            CHECK(same(got, inside ? Rgba8{255, 0, 0, 255} : Rgba8{0, 0, 0, 0}));
            red += inside ? 1 : 0;
        }
    }
    CHECK(red == 6);
}

TEST_CASE("lienzo: src-over de un color semitransparente, sin premultiplicar al leer", "[render]") {
    auto canvas = Canvas::make(2, 1);
    REQUIRE(canvas);
    canvas->clear({0, 0, 255, 255});
    canvas->fill_rect(0, 0, 1, 1, {255, 0, 0, 128});
    const Image img = canvas->snapshot();
    const Rgba8 mixed = pixel(img, 0, 0);
    // 255·128/255 ≈ 128 de rojo y 255·(1 − 128/255) ≈ 127 de azul, opaco (±1 por el redondeo).
    CHECK(mixed.a == 255);
    CHECK(mixed.g == 0);
    CHECK(mixed.r >= 127);
    CHECK(mixed.r <= 129);
    CHECK(mixed.b >= 126);
    CHECK(mixed.b <= 128);
    CHECK(same(pixel(img, 1, 0), {0, 0, 255, 255}));

    // Sobre transparente, el color se lee tal cual aunque por dentro esté premultiplicado.
    canvas->clear({0, 0, 0, 0});
    canvas->fill_rect(0, 0, 2, 1, {200, 100, 50, 128});
    const Rgba8 alone = pixel(canvas->snapshot(), 1, 0);
    CHECK(alone.a == 128);
    CHECK(alone.r >= 199);
    CHECK(alone.r <= 201);
    CHECK(alone.g >= 99);
    CHECK(alone.g <= 101);
    CHECK(alone.b >= 49);
    CHECK(alone.b <= 51);
}

namespace {

// 3×2 con un color distinto por píxel, opaco.
Image palette() {
    Image img{3, 2, {}};
    const Rgba8 colors[] = {{255, 0, 0, 255}, {0, 255, 0, 255}, {0, 0, 255, 255},
                            {255, 255, 0, 255}, {0, 255, 255, 255}, {255, 0, 255, 255}};
    for (const Rgba8& c : colors) {
        img.pixels.insert(img.pixels.end(), {c.r, c.g, c.b, c.a});
    }
    return img;
}

} // namespace

TEST_CASE("lienzo: un blit sin escala ni giro copia el recorte tal cual", "[render]") {
    const Image src = palette();
    auto canvas = Canvas::make(6, 4);
    REQUIRE(canvas);
    canvas->clear({0, 0, 0, 255});

    // Imagen entera, trasladada (2, 1): así llegan los blits de evaluate (m incluye el origen).
    REQUIRE(canvas->draw_image(src, {0, 0, 3, 2}, gc::core::Affine{1, 0, 0, 1, 2, 1}));
    Image img = canvas->snapshot();
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 3; ++x) {
            CHECK(same(pixel(img, x + 2, y + 1), pixel(src, x, y)));
        }
    }
    CHECK(same(pixel(img, 1, 1), {0, 0, 0, 255}));
    CHECK(same(pixel(img, 5, 1), {0, 0, 0, 255}));

    // Solo la columna central: la matriz lleva el píxel (1, y) de la fuente al (0, y) del cuadro.
    canvas->clear({0, 0, 0, 255});
    REQUIRE(canvas->draw_image(src, {1, 0, 1, 2}, gc::core::Affine{1, 0, 0, 1, -1, 0}));
    img = canvas->snapshot();
    CHECK(same(pixel(img, 0, 0), {0, 255, 0, 255}));
    CHECK(same(pixel(img, 0, 1), {0, 255, 255, 255}));
    CHECK(same(pixel(img, 1, 0), {0, 0, 0, 255})); // nada fuera del recorte
}

TEST_CASE("lienzo: un blit con escala 2 y volteo horizontal", "[render]") {
    Image src{1, 1, {10, 20, 30, 255}};
    auto canvas = Canvas::make(4, 2);
    REQUIRE(canvas);
    canvas->clear({0, 0, 0, 0});
    // x' = −2·x + 4: el píxel de la fuente cubre x ∈ [2, 4) del cuadro, volteado.
    REQUIRE(canvas->draw_image(src, {0, 0, 1, 1}, gc::core::Affine{-2, 0, 0, 2, 4, 0}));
    const Image img = canvas->snapshot();
    for (int y = 0; y < 2; ++y) {
        CHECK(same(pixel(img, 2, y), {10, 20, 30, 255}));
        CHECK(same(pixel(img, 3, y), {10, 20, 30, 255}));
        CHECK(pixel(img, 1, y).a == 0);
    }
}

TEST_CASE("lienzo: opacidad del blit y recorte de la cortinilla", "[render]") {
    Image src{2, 1, {255, 255, 255, 255, 255, 255, 255, 255}};
    auto canvas = Canvas::make(2, 1);
    REQUIRE(canvas);
    canvas->clear({0, 0, 0, 255});
    // Cortinilla: solo la mitad izquierda del cuadro.
    const std::array<gc::core::Point, 4> wipe{{{0, 0}, {1, 0}, {1, 1}, {0, 1}}};
    REQUIRE(canvas->draw_image(src, {0, 0, 2, 1}, gc::core::Affine{}, 0.5, wipe));
    const Image img = canvas->snapshot();
    const Rgba8 left = pixel(img, 0, 0);
    CHECK(left.a == 255);
    CHECK(left.r >= 127);
    CHECK(left.r <= 128);
    CHECK(left.r == left.g);
    CHECK(left.g == left.b);
    CHECK(same(pixel(img, 1, 0), {0, 0, 0, 255}));
}

TEST_CASE("lienzo: un recorte fuera de la imagen o una imagen mal formada son un error", "[render]") {
    const Image src = palette();
    auto canvas = Canvas::make(4, 4);
    REQUIRE(canvas);
    CHECK_FALSE(canvas->draw_image(src, {2, 0, 2, 2}, gc::core::Affine{}));
    CHECK_FALSE(canvas->draw_image(src, {0, 0, 0, 2}, gc::core::Affine{}));
    CHECK_FALSE(canvas->draw_image(src, {-1, 0, 1, 1}, gc::core::Affine{}));
    CHECK_FALSE(canvas->draw_image(Image{2, 2, {1, 2, 3}}, {0, 0, 1, 1}, gc::core::Affine{}));
}

TEST_CASE("lienzo: un blit girado 90° (el orden de a, b, c, d del contexto 2D)", "[render]") {
    Image src{2, 1, {255, 0, 0, 255, 0, 255, 0, 255}};
    auto canvas = Canvas::make(2, 2);
    REQUIRE(canvas);
    canvas->clear({0, 0, 0, 0});
    // translated(1, 0) y rotated(90°), como los arma frame.cpp: x' = −y + 1, y' = x.
    const auto m = gc::core::rotated(gc::core::translated(gc::core::Affine{}, 1, 0), std::numbers::pi / 2);
    REQUIRE(canvas->draw_image(src, {0, 0, 2, 1}, m));
    const Image img = canvas->snapshot();
    CHECK(same(pixel(img, 0, 0), {255, 0, 0, 255}));
    CHECK(same(pixel(img, 0, 1), {0, 255, 0, 255}));
    CHECK(pixel(img, 1, 0).a == 0);
    CHECK(pixel(img, 1, 1).a == 0);
}
