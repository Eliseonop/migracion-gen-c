#pragma once

// Fotogramas sueltos de una timeline, como compose.render_frame de video-yt: lo mismo que dibuja
// el export (render.hpp) en ese fotograma, con la misma preparación (material, efectos de vídeo y
// matte de «Eliminar fondo»). Lo usan `genclip frame` y lo usará el preview del editor (F5): los
// lectores de cada clip se quedan abiertos de un fotograma al siguiente, así que pedirlos en orden
// no vuelve a decodificar desde el principio.

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "gc/core/error.hpp"
#include "gc/export/project.hpp"
#include "gc/render/canvas.hpp"

namespace gc::exporter {

struct FrameOptions {
    // Lo que el render aún no sabe dibujar se salta con un aviso en vez de ser un error.
    bool omit_unsupported = false;
    // Cada aviso distinto, una vez (material que falta, capas que se omiten).
    std::function<void(const std::string& message)> on_warning;
};

struct Frame {
    render::Image image; // RGBA, opaco, del tamaño de la salida
    std::int64_t tick = 0;
    double t = 0;           // tick / fps
    std::size_t layers = 0; // capas compuestas (sin los textos, que van encima)
};

class FrameRenderer {
public:
    // Prepara la timeline de `project` (se copia lo que hace falta: el proyecto no tiene que seguir
    // vivo). Error si la timeline está vacía (como compose.py) o si falla la preparación.
    [[nodiscard]] static Result<FrameRenderer> open(const LoadedProject& project, const FrameOptions& options = {});

    FrameRenderer(FrameRenderer&&) noexcept;
    FrameRenderer& operator=(FrameRenderer&&) noexcept;
    FrameRenderer(const FrameRenderer&) = delete;
    FrameRenderer& operator=(const FrameRenderer&) = delete;
    ~FrameRenderer();

    [[nodiscard]] int width() const noexcept;  // par, como el export
    [[nodiscard]] int height() const noexcept; // par
    [[nodiscard]] int fps() const noexcept;
    [[nodiscard]] double duration() const noexcept; // s, export_total_duration
    // Los fotogramas que hay: k < round(duración · fps), como el fondo de render_frame.
    [[nodiscard]] std::int64_t frames() const noexcept;

    // El fotograma k (k / fps s). Error fuera de [0, frames()), si se ve algo que el render aún no
    // sabe dibujar (salvo con omit_unsupported) o si falla el decode.
    [[nodiscard]] Result<Frame> tick(std::int64_t k);
    // El que da compose.render_frame(…, at): el más cercano de la rejilla de 1/fps.
    [[nodiscard]] Result<Frame> at(double seconds);

private:
    struct Impl;
    explicit FrameRenderer(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

} // namespace gc::exporter
