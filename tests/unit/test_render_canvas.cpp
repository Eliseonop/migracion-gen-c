// gc_render: el lienzo (Skia por CPU). Solo se compila con Skia (presets msvc-vcpkg-*).
// Valores calculados a mano: src-over sin antialias sobre RGBA de 8 bits.

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>

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
