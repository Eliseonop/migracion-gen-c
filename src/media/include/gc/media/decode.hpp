#pragma once

// Decode de un material como lo hace el export de video-yt: la entrada que piden sus
// argumentos de ffmpeg (core::ffmpeg_input_args) y la cadena de filtros que el export le
// aplica antes del encuadre (`[k:v]{color_pre}{trim},setpts=PTS-STARTPTS`, compose.py), a
// fotogramas RGBA. El grafo de filtros es el de libavfilter, así que los fotogramas son los
// mismos que da `ffmpeg <args> -vf "<cadena>,format=rgba" -f rawvideo`.

#include <cstdint>
#include <filesystem>
#include <functional>
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

// El fotograma de un clip que el export compone en el instante `t` de la timeline: el n-ésimo
// de su cadena (export_decode_spec) con n = ⌊(t − max(0, start))·fps·velocidad⌋, contado desde
// el final si va al revés, y el último si la cadena se acaba antes (`eof_action=repeat` del
// overlay). La velocidad y el revés no cuentan en un still, como en compose.py. Nada (nullopt)
// si `t` es anterior al inicio o la cadena no da ningún fotograma.
[[nodiscard]] Result<std::optional<RgbaFrame>> export_frame_at(const core::TimelineClip& clip,
                                                               const std::filesystem::path& path, int fps, double t);

// MD5 en hexadecimal (av_md5): para comparar fotogramas con las fixtures.
[[nodiscard]] std::string md5_hex(std::span<const std::uint8_t> data);

} // namespace gc::media
