#pragma once

// El audio del export ejecutado con libavfilter: el grafo de core::export_audio_graph (los pasos de
// cada clip que suena y la mezcla hasta «[aout]») con las entradas de FFmpeg de la timeline, como
// lo ejecuta el CLI en compose.build_command.
//
// Como el CLI: cada «[k:a]» del grafo es el mejor stream de audio de la entrada k, decodificado y
// con las marcas de tiempo pasadas a 1/frecuencia (sin huecos de redondeo, av_rescale_delta) y
// desplazadas con el start_time del contenedor. A la salida, lo que pide el codificador AAC del
// export (`-c:a aac -ar 48000`: float planar, 48 kHz, estéreo) y el `-t` de la salida (un atrim con
// la duración del vídeo).

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <vector>

#include "gc/core/error.hpp"

namespace gc::media {

inline constexpr int kExportSampleRate = 48000;
inline constexpr int kExportChannels = 2;

// Un bloque de muestras de la mezcla: estéreo entrelazado (L, R, L, R…), en float.
using AudioSink = std::function<bool(std::span<const float> interleaved)>;

// Ejecuta `graph` (pasos separados, como los da core::export_audio_graph; el último termina en
// «[aout]») con las entradas `inputs` (los argumentos de entrada de FFmpeg de cada una, como
// core::ffmpeg_input_args) y pasa la salida a `sink` hasta el final o hasta que devuelva false.
// `total`: la duración del export en s (el `-t` de la salida). Devuelve cuántas muestras por canal
// salieron.
[[nodiscard]] Result<std::int64_t> mix_audio(const std::vector<std::vector<std::string>>& inputs,
                                             const std::vector<std::string>& graph, double total,
                                             const AudioSink& sink);

// Toda la mezcla en memoria (estéreo entrelazado).
[[nodiscard]] Result<std::vector<float>> mix_audio(const std::vector<std::vector<std::string>>& inputs,
                                                   const std::vector<std::string>& graph, double total);

} // namespace gc::media
