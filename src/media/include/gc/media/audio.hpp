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
//
// Con la misma maquinaria, el audio mono que leen los beats y la envolvente de un sonido
// (`ffmpeg -i x -ac 1 -ar N -f f32le -`), con el -ss y el -t de entrada del CLI (seek, desplazamiento
// de las marcas y atrim) y los filtros creados en su orden, que decide dónde cae el aresample.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "gc/core/beat_detect.hpp"
#include "gc/core/clip_motion.hpp"
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

// El audio de un archivo en mono a `rate` Hz, como `ffmpeg <input> -ac 1 -ar <rate> [-t <max_seconds>]
// -f f32le -`. `input`: los argumentos de entrada (-ss y -t antes de -i incluidos, que el CLI
// aplica con un seek y un atrim); `max_seconds`: el -t de la salida.
[[nodiscard]] Result<std::vector<float>> decode_mono(const std::vector<std::string>& input, int rate,
                                                     std::optional<double> max_seconds = std::nullopt);

// beats.detect_beats sin la caché: los primeros 20 min del audio a 22 050 Hz (decode_mono) y
// core::detect_beats. Los tiempos son del archivo.
[[nodiscard]] Result<core::Beats> detect_beats(const std::filesystem::path& path);

// clip_motion.sample_rms_envelope: `duration` s del audio desde `src_start` (con -ss y -t antes
// de -i) a 400 Hz y core::rms_envelope.
[[nodiscard]] Result<core::Envelope> sample_rms_envelope(const std::filesystem::path& path, double src_start,
                                                         double duration, int rate = 20);

} // namespace gc::media
