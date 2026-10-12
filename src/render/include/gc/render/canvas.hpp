#pragma once

// Lienzo del render: un fotograma RGBA en memoria sobre el que se dibujan las capas. Por dentro
// es Skia por CPU (raster); Skia no sale de aquí, así que quien lo use no necesita sus cabeceras.
//
// Primera pieza de gc_render (F3): crear, limpiar, rellenar un rectángulo y leer los píxeles.
// Lo que dibuja cada capa (blits de frame.hpp, máscaras, fusión, texto) llega encima.

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include "gc/core/clip_filters.hpp"
#include "gc/core/clip_layout.hpp"
#include "gc/core/error.hpp"
#include "gc/core/frame.hpp"

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

    // Un blit de core::evaluate: el rectángulo `src` de `image` (px de la fuente) llevado al
    // cuadro por `m` (px de la fuente → px del cuadro), con opacidad `alpha` (0–1; NaN = 1, como
    // `globalAlpha` del contexto 2D) y src-over.
    // Si hay `clip` (la cortinilla, en px del cuadro), solo se dibuja dentro de ese
    // cuadrilátero. Con `color` (la matriz de los filtros de color, clip_filters.hpp), cada
    // píxel ya muestreado pasa por ella sin premultiplicar y se acota a 0–1, como el
    // `feColorMatrix` del preview y el `colorchannelmixer` del export (que la aplica tras escalar).
    // Muestreo bicúbico B = 0, C = 0,6, el `flags=bicubic` de swscale que usa el
    // export; como B = 0 interpola, sin escala ni giro copia los píxeles tal cual. Error si el
    // recorte no cabe en la imagen o está vacío.
    [[nodiscard]] Result<void> draw_image(const Image& image, const core::SourceCrop& src, const core::Affine& m,
                                          double alpha = 1.0,
                                          const std::optional<std::array<core::Point, 4>>& clip = std::nullopt,
                                          const std::optional<core::AffineColorMatrix>& color = std::nullopt);

    [[nodiscard]] Image snapshot() const;

private:
    struct Impl;
    friend struct CanvasAccess; // el texto (text.cpp) dibuja en el mismo lienzo
    explicit Canvas(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

} // namespace gc::render
