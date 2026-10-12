#pragma once

// El audio del export: las cadenas de FFmpeg que arma compose.py para cada clip que suena y su
// mezcla final (espejo de backend/app/audio_fx.py, clip_audio.py, la parte de audio de
// clip_speed.py y clip_keyframes.py, y el bloque de audio de compose.build_command).
//
// Cada clip: `atrim` de su tramo, `asetpts`, formato fltp/48 kHz/estéreo, velocidad (`areverse`,
// `atempo` o `asetrate`), los efectos de sonido en serie (cada uno mezclado seco/efecto según su
// intensidad, animable), el volumen (constante o una envolvente con keyframes) y `adelay` hasta su
// inicio. Después, `amix` sin normalizar, `apad` hasta el final y `loudnorm` al objetivo.
// Son textos: gc_media los ejecutará con libavfilter, igual que el CLI.

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "gc/core/error.hpp"
#include "gc/core/json.hpp"
#include "gc/core/model.hpp"

namespace gc::core {

// --- Envolventes con keyframes (clip_keyframes.py) ----------------------------------------------

// Expresión de FFmpeg (con las comas escapadas, en `t` local) de la propiedad animable `key`
// (volume o un efecto de audio), o nullopt si los keyframes están apagados, hay menos de dos o
// el valor no cambia (a 4 decimales). Las curvas sin fórmula cerrada (back-out, bézier) van en
// tramos rectos, uno por fotograma a 30 fps (2–120 por tramo).
[[nodiscard]] std::optional<std::string> ffmpeg_envelope(const TimelineClip& clip, std::string_view key,
                                                         double fallback);

// Filtro `volume` del clip: la envolvente con `eval=frame` o el volumen en 0 con 3 decimales.
[[nodiscard]] std::string volume_filter(const TimelineClip& clip);

// --- Efectos de sonido (audio_fx.py) ------------------------------------------------------------

// Un nodo de una etapa. Los números van como texto, tal como los escribe Python (3000, 0.7071, 1).
struct AudioNode {
    std::string_view type; // lowpass, highpass, peaking, lowshelf, highshelf, echo, compressor, drive
    std::vector<std::pair<std::string_view, std::string_view>> params;
};

struct AudioFxDef {
    std::string_view id;
    std::string_view label;
    std::string_view group; // effect | filter
    std::vector<std::vector<AudioNode>> stages;
};

// Los 12 efectos y filtros de AUDIO_FX, en su orden.
[[nodiscard]] std::span<const AudioFxDef> audio_fx_catalog();

// Intensidad 0–1: True = 1, False/None = 0, float() acotado; lo que no es número o NaN, 0.
[[nodiscard]] double fx_intensity(const Json& value);

// El nodo como filtro de FFmpeg (biquads del Cookbook, como Web Audio).
[[nodiscard]] std::string ffmpeg_node(const AudioNode& node);

// Peso de cada etapa (la 0 es la señal seca) para la intensidad k: con p = k·N, la etapa j pesa
// max(0, 1 − |p − j|).
[[nodiscard]] std::vector<double> stage_weights(double k, int n_stages);

struct AudioFxGraph {
    std::vector<std::string> steps; // «[in]…[out]»
    std::string out;                // la etiqueta de salida (la de entrada si no hay efectos)
};

// Los efectos del clip en serie desde `in_label`, con etiquetas que empiezan por `tag`.
[[nodiscard]] AudioFxGraph audio_fx_graph(const TimelineClip& clip, std::string_view in_label, std::string_view tag);

// --- Velocidad (clip_speed.py) ------------------------------------------------------------------

// atempo encadenado (cada uno entre 0,5 y 2).
[[nodiscard]] std::string atempo_chain(double speed);
// `areverse` y la velocidad: atempo si conserva el tono, si no `asetrate=48000*sp`. "" si nada.
[[nodiscard]] std::string audio_speed_filters(const TimelineClip& clip);

// --- El clip en la mezcla (clip_audio.py y compose.build_command) -------------------------------

// ¿Aporta audio? No si la pista o el clip están silenciados.
[[nodiscard]] bool clip_mixes_audio(const TimelineClip& clip, const TimelineTrack& track);

// Los pasos del grafo del clip `m` (entrada `input` de FFmpeg) hasta su etiqueta «a{m}».
[[nodiscard]] std::vector<std::string> export_audio_steps(const TimelineClip& clip, int input, int m);

// La mezcla final de `count` clips (a0…) hasta «[aout]»: amix (si hay más de uno), apad hasta
// `total` s y loudnorm al objetivo (acotado a −40…−5 LUFS). "" si no hay ninguno.
[[nodiscard]] std::string export_audio_mix(int count, double total, double target_db);

// --- La timeline entera (compose.build_command) ------------------------------------------------

using ClipPredicate = std::function<bool(const TimelineClip&)>;

struct ExportInputs {
    // Las entradas de FFmpeg en orden: primero los clips que se dibujan y después los que solo
    // suenan, una por id (un id repetido comparte la del primero).
    std::vector<const TimelineClip*> files;
    // Los clips que suenan, en orden, con el índice de su entrada.
    std::vector<std::pair<const TimelineClip*, int>> audio;
};

// Qué clips entran en el export y cuáles suenan, como build_command: se saltan los que no tienen
// pista (si un id de pista se repite, manda la última), los desactivados, las capas de ajuste,
// los de 0,02 s o menos y aquellos cuyo material no existe (`media_exists`). En una pista de
// vídeo se dibujan si la pista no está oculta y suenan si no son fijos, no están silenciados y
// su material tiene audio (`has_audio`); en una de audio, solo suenan. Error con un tipo de
// clip desconocido en una pista de vídeo (el ValueError de Python).
[[nodiscard]] Result<ExportInputs> export_inputs(const Timeline& timeline, const ClipPredicate& media_exists,
                                                 const ClipPredicate& has_audio);

// El grafo de audio entero de la timeline (los pasos de cada clip que suena y la mezcla), vacío
// si no suena nada.
[[nodiscard]] Result<std::vector<std::string>> export_audio_graph(const Timeline& timeline,
                                                                  const ClipPredicate& media_exists,
                                                                  const ClipPredicate& has_audio);

} // namespace gc::core
