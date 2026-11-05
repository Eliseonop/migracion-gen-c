// La reproducción de audio (gc/media/audio_out.hpp): lo que llena el cursor (exacto, sin tarjeta)
// y el reloj de AudioOut con el backend nulo de miniaudio (en tiempo real, sin sonar).

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cmath>
#include <limits>
#include <memory>
#include <thread>
#include <vector>

#include "gc/media/audio_out.hpp"

using gc::media::AudioOut;
using gc::media::PcmCursor;

namespace {

// 1 s a 10 Hz (para leerlo a mano): la muestra k del canal izquierdo vale k y la del derecho, −k.
gc::media::Pcm ramp() {
    auto pcm = std::make_shared<std::vector<float>>();
    for (int k = 0; k < 10; ++k) {
        pcm->push_back(static_cast<float>(k));
        pcm->push_back(static_cast<float>(-k));
    }
    return pcm;
}

} // namespace

TEST_CASE("audio: el cursor llena desde su posición y sigue en silencio pasado el final", "[media][audio_out]") {
    PcmCursor cursor(10);
    std::vector<float> out(6, 99.0F);
    cursor.fill(out); // sin PCM: silencio, y el reloj corre
    CHECK(out == std::vector<float>(6, 0.0F));
    CHECK(cursor.position() == 0.3);

    cursor.set_pcm(ramp()); // sin moverse
    cursor.fill(out);
    CHECK(out == std::vector<float>{3, -3, 4, -4, 5, -5});
    CHECK(cursor.position() == 0.6);

    cursor.seek(0.8);
    std::vector<float> tail(8, 99.0F);
    cursor.fill(tail); // dos muestras de verdad y dos de silencio
    CHECK(tail == std::vector<float>{8, -8, 9, -9, 0, 0, 0, 0});
    CHECK(cursor.position() == 1.2);

    cursor.seek(-1.0); // antes del principio, el principio
    CHECK(cursor.position() == 0.0);
    cursor.seek(std::numeric_limits<double>::quiet_NaN());
    CHECK(cursor.position() == 0.0);
    cursor.seek(0.25); // al más cercano: 2,5 muestras → 3 (llround)
    CHECK(cursor.position() == 0.3);

    cursor.set_pcm(nullptr);
    cursor.fill(out);
    CHECK(out == std::vector<float>(6, 0.0F));
}

TEST_CASE("audio: el reloj de la salida corre al sonar, se para y salta", "[media][audio_out]") {
    auto out = AudioOut::open(true); // el backend nulo: sin altavoces
    REQUIRE(out);
    CHECK(out->backend() == "Null");
    CHECK_FALSE(out->playing());
    out->set_pcm(ramp());
    REQUIRE(out->play_from(2.0));
    CHECK(out->playing());
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    const double ran = out->position();
    CHECK(ran > 2.1); // ha avanzado (en tiempo real, con margen para un PC cargado)
    CHECK(ran < 4.0);

    out->pause();
    CHECK_FALSE(out->playing());
    const double stopped = out->position();
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    CHECK(out->position() == stopped);

    REQUIRE(out->play_from(10.0));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    CHECK(out->position() >= 10.0);
    CHECK(out->position() < 12.0);
    REQUIRE(out->play_from(1.0)); // sonando, solo salta
    CHECK(out->position() < 1.5);
    out->pause();
}
