// gc_render: los efectos de clip (effects.hpp) sobre imágenes pequeñas. La paridad con el export
// la miden las escenas «efecto: …» de test_render_parity.cpp; aquí, las propiedades de cada filtro.

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <vector>

#include "gc/core/clip_effects.hpp"
#include "gc/render/effects.hpp"

using gc::core::ClipEffects;
using gc::render::apply_effects;
using gc::render::Image;

namespace {

// Ondas de colores, opacas.
Image waves(int w, int h) {
    Image img{w, h, std::vector<std::uint8_t>(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4)};
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const auto i = 4 * ((static_cast<std::size_t>(y) * static_cast<std::size_t>(w)) + static_cast<std::size_t>(x));
            img.pixels[i] = static_cast<std::uint8_t>(128 + (110 * std::sin(y * 0.35)));
            img.pixels[i + 1] = static_cast<std::uint8_t>(128 + (55 * std::sin(x * 0.35)) + (55 * std::sin(y * 0.3)));
            img.pixels[i + 2] = static_cast<std::uint8_t>(128 + (110 * std::sin((x + y) * 0.2)));
            img.pixels[i + 3] = 255;
        }
    }
    return img;
}

Image flat(int w, int h, std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    Image img{w, h, {}};
    for (int i = 0; i < w * h; ++i) {
        img.pixels.insert(img.pixels.end(), {r, g, b, 255});
    }
    return img;
}

std::uint8_t at(const Image& img, int x, int y, int c) {
    return img.pixels[(4 * ((static_cast<std::size_t>(y) * static_cast<std::size_t>(img.width)) +
                            static_cast<std::size_t>(x))) +
                      static_cast<std::size_t>(c)];
}

} // namespace

TEST_CASE("efectos: sin efectos, la imagen no cambia", "[render][effects]") {
    Image img = waves(40, 30);
    const auto before = img.pixels;
    apply_effects(img, ClipEffects{}, 1);
    CHECK(img.pixels == before);
    ClipEffects zero;
    zero.eq = gc::core::EqParams{}; // un eq que se queda en 1 / 0 / 1 no cambia nada
    CHECK_FALSE(zero.any());
    apply_effects(img, zero, 1);
    CHECK(img.pixels == before);
}

TEST_CASE("efectos: el desenfoque deja igual un color liso y suaviza las ondas", "[render][effects]") {
    ClipEffects fx;
    fx.blur = 4.0;
    Image liso = flat(30, 20, 200, 40, 90);
    apply_effects(liso, fx, 0);
    for (int y = 0; y < 20; ++y) {
        for (int x = 0; x < 30; ++x) {
            CHECK(at(liso, x, y, 0) == 200);
            CHECK(at(liso, x, y, 1) == 40);
            CHECK(at(liso, x, y, 2) == 90);
            CHECK(at(liso, x, y, 3) == 255); // también el alfa (el borde se extiende)
        }
    }
    Image img = waves(60, 60);
    const Image before = img;
    apply_effects(img, fx, 0);
    // La variación de una fila a la siguiente baja (a la mitad con σ = 4 y ondas de ~18 px: el
    // filtro recursivo de gblur deja pasar más que una gaussiana).
    double var_before = 0;
    double var_after = 0;
    for (int y = 1; y < 60; ++y) {
        var_before += std::abs(at(before, 30, y, 0) - at(before, 30, y - 1, 0));
        var_after += std::abs(at(img, 30, y, 0) - at(img, 30, y - 1, 0));
    }
    CHECK(var_after < 0.6 * var_before);
}

TEST_CASE("efectos: blanco y negro deja R = G = B", "[render][effects]") {
    ClipEffects fx;
    fx.grayscale = true;
    Image img = waves(20, 20);
    apply_effects(img, fx, 0);
    for (int y = 0; y < 20; ++y) {
        for (int x = 0; x < 20; ++x) {
            CHECK(std::abs(at(img, x, y, 0) - at(img, x, y, 1)) <= 1);
            CHECK(std::abs(at(img, x, y, 2) - at(img, x, y, 1)) <= 1);
        }
    }
}

TEST_CASE("efectos: el pixelado repite el píxel del centro de cada bloque", "[render][effects]") {
    ClipEffects fx;
    fx.pixelate = 4;
    Image img = waves(16, 8);
    const Image before = img;
    apply_effects(img, fx, 0);
    for (int y = 0; y < 8; ++y) {
        for (int x = 0; x < 16; ++x) {
            // Bloques de 4: el centro (4k + 1,5) redondeado es 4k + 2.
            const int sx = (4 * (x / 4)) + 2;
            const int sy = (4 * (y / 4)) + 2;
            for (int c = 0; c < 4; ++c) {
                CHECK(at(img, x, y, c) == at(before, sx, sy, c));
            }
        }
    }
    // Más grande que la imagen: ese eje se queda como está (scale con 0 deja el tamaño).
    fx.pixelate = 32;
    Image small = waves(16, 8);
    const Image small_before = small;
    apply_effects(small, fx, 0);
    CHECK(small.pixels == small_before.pixels);
}

TEST_CASE("efectos: el grano cambia con la semilla, no toca el alfa y casi no mueve la media",
          "[render][effects]") {
    ClipEffects fx;
    fx.grain = 12;
    Image a = flat(64, 64, 128, 128, 128);
    Image b = a;
    Image c = a;
    apply_effects(a, fx, 7);
    apply_effects(b, fx, 7);
    apply_effects(c, fx, 8);
    CHECK(a.pixels == b.pixels);
    CHECK(a.pixels != c.pixels);
    double sum = 0;
    double sum2 = 0;
    for (std::size_t i = 0; i < a.pixels.size(); i += 4) {
        CHECK(a.pixels[i + 3] == 255);
        const double d = a.pixels[i + 1] - 128.0;
        sum += d;
        sum2 += d * d;
    }
    const double n = static_cast<double>(a.pixels.size() / 4);
    CHECK(std::abs(sum / n) < 1.0);
    // Desviación 12/√3 ≈ 6,9, algo menos al truncar a enteros.
    const double sd = std::sqrt(sum2 / n);
    CHECK(sd > 5.5);
    CHECK(sd < 7.5);
}

TEST_CASE("efectos: brillo, contraste y saturación de eq en la luma", "[render][effects]") {
    ClipEffects fx;
    fx.eq = gc::core::EqParams{0.1, 1.0, 1.0};
    Image img = flat(4, 4, 100, 100, 100);
    apply_effects(img, fx, 0);
    // +0,1 de brillo suma 25 a Y (rango limitado): ~29 en RGB.
    CHECK(at(img, 0, 0, 0) >= 127);
    CHECK(at(img, 0, 0, 0) <= 131);
    CHECK(at(img, 0, 0, 0) == at(img, 0, 0, 1));
    // Sin saturación, casi gris: el eq de FFmpeg deja U y V en 127, no en 128.
    fx.eq = gc::core::EqParams{0.0, 1.0, 0.0};
    Image color = flat(4, 4, 220, 40, 60);
    apply_effects(color, fx, 0);
    CHECK(std::abs(at(color, 1, 1, 0) - at(color, 1, 1, 1)) <= 3);
    CHECK(std::abs(at(color, 1, 1, 2) - at(color, 1, 1, 1)) <= 3);
}
