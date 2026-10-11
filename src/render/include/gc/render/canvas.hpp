#pragma once

// Lienzo del render: un fotograma RGBA en memoria sobre el que se dibujan las capas. Por dentro
// es Skia por CPU (raster); Skia no sale de aquí, así que quien lo use no necesita sus cabeceras.
//
// Primera pieza de gc_render (F3): crear, limpiar, rellenar un rectángulo y leer los píxeles.
// Lo que dibuja cada capa (blits de frame.hpp, máscaras, fusión, texto) llega encima.

#include <cstdint>
#include <memory>
#include <vector>

#include "gc/core/error.hpp"

namespace gc::render {

// Color sin premultiplicar, 0–255 por canal.
struct Rgba8 {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;
};

// Fotograma leído del lienzo: RGBA sin premultiplicar, width · height · 4, sin relleno entre
// filas (el mismo formato que gc::media::RgbaFrame y que `-pix_fmt rgba` de ffmpeg).
struct Image {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels;
};

class Canvas {
public:
    // Error si las dimensiones no son mayores que 0 o Skia no puede reservar el lienzo.
    [[nodiscard]] static Result<Canvas> make(int width, int height);

    Canvas(Canvas&&) noexcept;
    Canvas& operator=(Canvas&&) noexcept;
    Canvas(const Canvas&) = delete;
    Canvas& operator=(const Canvas&) = delete;
    ~Canvas();

    [[nodiscard]] int width() const noexcept;
    [[nodiscard]] int height() const noexcept;

    // Sustituye todo el lienzo por `color` (también el alfa: transparente si a = 0).
    void clear(Rgba8 color);
    // Rellena el rectángulo (en píxeles, origen arriba a la izquierda) con `color`, fundido
    // con lo que haya debajo (src-over) y sin antialias.
    void fill_rect(double x, double y, double w, double h, Rgba8 color);

    [[nodiscard]] Image snapshot() const;

private:
    struct Impl;
    explicit Canvas(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

} // namespace gc::render
