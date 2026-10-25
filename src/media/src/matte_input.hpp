#pragma once

// La segunda entrada del grafo de un clip con «Eliminar fondo» ([in1] en la cadena): la secuencia PNG
// del matte derivado que el export le pasa a FFmpeg con `-framerate`, `-start_number` y, en un GIF,
// `-stream_loop -1` (compose._bg_input_args), hecha aquí sin escribir el nivel 2 de la caché.
//
// - Qué fotogramas: los que leería el demuxer image2 del derivado (find_image_range): el primero que
//   exista de los 5 primeros (start_number_range) y, desde ahí, hasta el primero que falte.
// - Cada uno: su PNG crudo decodificado por el decodificador PNG de FFmpeg (gris de 8 bits, con las
//   mismas etiquetas que el derivado, que también es un PNG en gris) y pasado por la LUT.
// - Las marcas: 0, 1, 2… en 1/mask_fps, como image2; con -stream_loop, al dar la vuelta siguen
//   (el CLI suma la duración de la entrada, un fotograma por cada uno).

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "gc/core/bg_matte.hpp"
#include "gc/core/error.hpp"

extern "C" {
struct AVFrame;
}

namespace gc::media::detail {

class MatteFeed {
public:
    // Error si image2 no encontraría ningún fotograma (el export falla al abrir la secuencia) o si un
    // PNG crudo no es un gris de 8 bits.
    [[nodiscard]] static Result<MatteFeed> open(const core::MatteSpec& spec);

    MatteFeed(MatteFeed&&) noexcept;
    MatteFeed& operator=(MatteFeed&&) noexcept;
    MatteFeed(const MatteFeed&) = delete;
    MatteFeed& operator=(const MatteFeed&) = delete;
    ~MatteFeed();

    [[nodiscard]] std::int64_t fps() const noexcept;
    // El primer fotograma (para los parámetros de la entrada del grafo).
    [[nodiscard]] const AVFrame& first() const noexcept;
    // El siguiente fotograma, una referencia nueva con su pts que pasa a quien la recibe; nullptr al
    // final (sin -stream_loop).
    [[nodiscard]] Result<AVFrame*> next();

private:
    struct Impl;
    explicit MatteFeed(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

} // namespace gc::media::detail
