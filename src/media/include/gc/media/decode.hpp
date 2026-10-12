#pragma once

// Decode de un material como lo hace el export de video-yt: la entrada que piden sus
// argumentos de ffmpeg (core::ffmpeg_input_args) y la cadena de filtros que el export le
// aplica antes del encuadre (`[k:v]{color_pre}{trim},setpts=PTS-STARTPTS`, compose.py), a
// fotogramas RGBA. El grafo de filtros es el de libavfilter, así que los fotogramas son los
// mismos que da `ffmpeg <args> -vf "<cadena>,format=rgba" -f rawvideo`.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "gc/core/error.hpp"
#include "gc/core/model.hpp"

namespace gc::media {

struct RgbaFrame {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels; // width · height · 4, sin relleno entre filas
};

struct DecodeSpec {
    std::vector<std::string> input_args; // como ffmpeg_input_args: opciones, "-i" y la ruta (UTF-8)
    std::string chain;                   // filtros de libavfilter tras el decode (sin el format=rgba)
};

// `ASSUME_BT709_FILTER` de compose.py: un vídeo sin etiqueta de color se lee como BT.709.
inline constexpr const char* kAssumeBt709Filter =
    "setparams=colorspace=bt709:color_primaries=bt709:color_trc=bt709:range=tv";

// La entrada y la cadena del export para un clip (sin el encuadre): `color_pre` si no es un
// still y el material no declara matriz (color_untagged), y luego ffmpeg_trim_filter y
// `setpts=PTS-STARTPTS`.
[[nodiscard]] DecodeSpec export_decode_spec(const core::TimelineClip& clip, const std::filesystem::path& path,
                                            int fps);

// Decodifica y llama a `on_frame` con cada fotograma que sale de la cadena; si devuelve false,
// para. Devuelve cuántos fotogramas salieron. Opciones de entrada que entiende: -c:v
// (decodificador), -t (duración de la entrada) y las del demuxer (-loop, -framerate,
// -start_number, -ignore_loop).
[[nodiscard]] Result<std::size_t> decode_frames(const DecodeSpec& spec,
                                                      const std::function<bool(const RgbaFrame&)>& on_frame);

// El fotograma del instante `t` como frame_grab.extract_frame (sin su escalado ni el JPEG):
// `-ss {max(0, t):.3f}` antes de `-i` y `-frames:v 1`, es decir, el primer fotograma con
// pts ≥ t (redondeado a ms). Nada (nullopt) si `t` queda después del último.
[[nodiscard]] Result<std::optional<RgbaFrame>> frame_at(const std::filesystem::path& path, double t);

// El fotograma compuesto que da `compose.render_frame(…, at)`: `-ss {max(0, at):.3f}` a la salida
// del grafo toma el más cercano de la rejilla de 1/fps (un empate, el siguiente). Devuelve su
// índice k (el instante es k/fps).
[[nodiscard]] std::int64_t export_frame_tick(double at, int fps);

// El fotograma de un clip que el export compone en el fotograma k de la timeline (k/fps s),
// como compose.py: la cadena de export_decode_spec más el `fps=F` que va tras el encuadre,
// `reverse` y `setpts=PTS/{velocidad:.6f}` (nada de eso en un still) y
// `setpts=PTS-STARTPTS+{max(0, start):.3f}/TB`, con el truncado a ticks enteros de las setpts;
// el overlay toma el primer fotograma con pts = k o, si no hay, el último anterior, y pasado el
// final el último (`eof_action=repeat`). Nada (nullopt) si la cadena no da ningún fotograma.
[[nodiscard]] Result<std::optional<RgbaFrame>> export_frame_at(const core::TimelineClip& clip,
                                                               const std::filesystem::path& path, int fps,
                                                               std::int64_t tick);

// Los fotogramas de un clip que compone el export, en orden de ticks: los mismos que
// export_frame_at, pero sin volver a decodificar desde el principio en cada fotograma (el
// decodificador avanza con los ticks). Pedir un tick anterior al último empieza de nuevo. Al revés
// (`reverse`), de momento, cada tick es un export_frame_at.
class ClipFrames {
public:
    [[nodiscard]] static Result<ClipFrames> open(const core::TimelineClip& clip, const std::filesystem::path& path,
                                                 int fps);

    ClipFrames(ClipFrames&&) noexcept;
    ClipFrames& operator=(ClipFrames&&) noexcept;
    ClipFrames(const ClipFrames&) = delete;
    ClipFrames& operator=(const ClipFrames&) = delete;
    ~ClipFrames();

    // El fotograma del tick (nullptr si la cadena no da ninguno). Vale hasta la siguiente llamada.
    [[nodiscard]] Result<const RgbaFrame*> at(std::int64_t tick);

private:
    struct Impl;
    explicit ClipFrames(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

// MD5 en hexadecimal (av_md5): para comparar fotogramas con las fixtures.
[[nodiscard]] std::string md5_hex(std::span<const std::uint8_t> data);

} // namespace gc::media
