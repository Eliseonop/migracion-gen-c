#pragma once

// Beats automáticos (espejo de backend/app/beats.py): dónde caen los golpes de la música, con el
// esquema de Ellis 2007 (el de librosa.beat.beat_track):
//   1. fuerza de ataque: flujo espectral positivo en 48 bandas logarítmicas, sin su media móvil;
//   2. tempo: autocorrelación de esa curva entre 60 y 200 BPM, con preferencia suave por 120;
//   3. beats: programación dinámica que premia caer en ataques y separarse un periodo.
// La entrada es el audio mono a 22 050 Hz (decodificarlo es cosa de gc_media). Los tiempos son
// del archivo, como in_point/out_point.

#include <cstdint>
#include <span>
#include <vector>

#include "gc/core/error.hpp"

namespace gc::core {

inline constexpr int kBeatSampleRate = 22050;
inline constexpr int kBeatHop = 512;
inline constexpr int kBeatFft = 2048;
inline constexpr double kBeatMaxSeconds = 20 * 60; // tope de análisis

// Fuerza de ataque por fotograma de kBeatHop muestras, normalizada a 0–1.
[[nodiscard]] std::vector<double> onset_envelope(std::span<const float> mono);
// Tempo (BPM) por autocorrelación de la curva de ataque.
[[nodiscard]] double estimate_bpm(std::span<const double> envelope);
// Fotogramas de los beats. Error donde Python falla (ningún máximo de la puntuación llega al umbral).
[[nodiscard]] Result<std::vector<std::int64_t>> track_beats(std::span<const double> envelope, double bpm);

struct Beats {
    std::vector<double> times; // s del archivo, a ms
    double bpm = 0;            // a 0,1
};
// detect_beats sin la caché ni la decodificación. Error si hay menos de kBeatFft muestras.
[[nodiscard]] Result<Beats> detect_beats(std::span<const float> mono);

} // namespace gc::core
