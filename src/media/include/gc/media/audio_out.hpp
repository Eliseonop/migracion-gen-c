#pragma once

// La reproducción de audio (miniaudio): un PCM estéreo en float que suena desde un instante. Lo
// que ya se oye es el reloj del reproductor del editor (el «reloj de audio maestro» del plan): la
// imagen sigue al sonido, no al revés. Lo que se oye va por detrás de lo que ha pedido la tarjeta
// lo que tarda en salir su búfer (la latencia).

#include <cstdint>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <vector>

#include "gc/core/error.hpp"
#include "gc/media/audio.hpp"

namespace gc::media {

using Pcm = std::shared_ptr<const std::vector<float>>; // estéreo entrelazado (L, R, L, R…)

// Lo que suena y por dónde va: la tarjeta pide bloques (fill) desde su hilo y la interfaz cambia
// el PCM o la posición desde el suyo. Sin PCM, o pasado su final, silencio: el reloj sigue igual.
class PcmCursor {
public:
    explicit PcmCursor(int rate = kExportSampleRate) : rate_(rate) {}

    // Cambia lo que suena sin moverse de donde va (otra mezcla de la misma timeline).
    void set_pcm(Pcm pcm);
    // A `seconds` (a la muestra más cercana; antes de 0 o NaN, 0). Da la posición que queda, en s.
    double seek(double seconds);
    // Llena `out` (muestras estéreo entrelazadas: out.size() / 2 por canal) desde la posición y la
    // avanza otro tanto.
    void fill(std::span<float> out);
    // En s: lo que ya se ha llenado.
    [[nodiscard]] double position() const;
    [[nodiscard]] int rate() const noexcept { return rate_; }

private:
    mutable std::mutex mutex_;
    Pcm pcm_;
    std::int64_t frame_ = 0; // muestras por canal
    int rate_;
};

// Lo que se oye de un PCM que suena desde `from` y del que la tarjeta ya ha pedido hasta `filled`,
// con `latency` s esperando en su búfer: filled − latency, sin bajar de `from` (lo primero que se
// mandó aún no ha salido).
[[nodiscard]] constexpr double heard_position(double filled, double from, double latency) noexcept {
    return filled - latency > from ? filled - latency : from;
}

class AudioOut {
public:
    // La salida por defecto del sistema, estéreo en float a `rate` Hz. Con `null_device` o con la
    // variable de entorno GC_AUDIO_DEVICE=null, el backend nulo de miniaudio: sin sonido y con el
    // mismo reloj (los tests, que no deben sonar por los altavoces). Error si no hay salida.
    [[nodiscard]] static Result<AudioOut> open(bool null_device = false, int rate = kExportSampleRate);

    AudioOut(AudioOut&&) noexcept;
    AudioOut& operator=(AudioOut&&) noexcept;
    AudioOut(const AudioOut&) = delete;
    AudioOut& operator=(const AudioOut&) = delete;
    ~AudioOut();

    void set_pcm(Pcm pcm);
    // Empieza a sonar en `seconds` (o salta ahí si ya sonaba).
    [[nodiscard]] Result<void> play_from(double seconds);
    void pause();
    [[nodiscard]] bool playing() const noexcept;
    // El volumen de escucha (0 – 1, el maestro de la salida): solo lo que se oye, no el PCM.
    void set_volume(double volume);
    // El reloj, en s: lo que ya se oye (heard_position de filled() y latency()).
    [[nodiscard]] double position() const;
    // Lo que ya ha pedido la tarjeta, en s (lo que se oirá dentro de latency()).
    [[nodiscard]] double filled() const;
    // En s: lo que cabe en el búfer de la tarjeta (sus periodos), lo que tarda en oírse lo que se le
    // manda. 0 si no se sabe.
    [[nodiscard]] double latency() const;
    // «WASAPI», «Core Audio», «PulseAudio», «Null»…
    [[nodiscard]] std::string backend() const;

private:
    struct Impl;
    explicit AudioOut(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

} // namespace gc::media
