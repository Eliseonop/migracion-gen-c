#pragma once

// Animaciones de clip: entrada, salida, bucle y combo
// (espejo de backend/app/fx_anims.py y de clip_anim.render_pose, que a su vez son
// espejo de frontend/src/fx/core/animEval.js).
//
//   clip.animations = {in: {id, dur}, out: {id, dur}, loop: {id, dur}, combo: {id}}
//
// Las curvas salen del catálogo (fx_catalog().anim(id)). El delta se aplica a la pose
// (x, y en fracción del cuadro; scale y opacity multiplican; rotation suma), nunca al
// recorte (cx/cy/zoom). La edición usa clip_pose; el dibujo, render_pose.

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

#include "gc/core/fx_catalog.hpp"
#include "gc/core/json.hpp"
#include "gc/core/model.hpp"
#include "gc/core/pose.hpp"

namespace gc::core {

// Curva de suavizado por nombre ("linear", "outQuad", "outBounce"…). Un nombre
// desconocido es lineal.
[[nodiscard]] double anim_ease(std::string_view name, double x);

// Valor de una pista [[u, v, curva], …] en `u` (0–1). nullopt si no hay pista.
[[nodiscard]] std::optional<double> eval_anim_track(const Json& keys, double u);

struct ClipAnimation {
    std::string_view slot;   // "in" | "out" | "loop" | "combo"
    const Json* anim = nullptr; // manifiesto del catálogo (vive mientras viva `catalog`)
    double dur = 0.0;        // segundos efectivos
};

struct ClipAnimations {
    std::shared_ptr<const FxCatalog> catalog;
    std::vector<ClipAnimation> items;
};

// Animaciones válidas del clip con sus duraciones efectivas: la que no existe en el
// catálogo o no es de su ranura se ignora; combo dura todo el clip; si entrada y salida
// no caben en el clip, se reparten.
[[nodiscard]] ClipAnimations clip_animations(const TimelineClip& clip, double clip_dur);
[[nodiscard]] bool has_animations(const TimelineClip& clip);

struct PoseDelta {
    double x = 0.0;
    double y = 0.0;
    double scale = 1.0;
    double rotation = 0.0;
    double opacity = 1.0;
};

// Delta de las animaciones en `t` (s locales). nullopt = sin animaciones (identidad).
[[nodiscard]] std::optional<PoseDelta> animation_delta(const TimelineClip& clip, double t, double clip_dur);
[[nodiscard]] Pose apply_delta(const Pose& pose, const PoseDelta& d);

// Pose con la que se DIBUJA el clip en `local_t`: clip_pose más las animaciones.
[[nodiscard]] Pose render_pose(const TimelineClip& clip, double local_t);
// Factor de opacidad de las animaciones en `local_t` (1 si no hay).
[[nodiscard]] double anim_opacity(const TimelineClip& clip, double local_t);

inline constexpr int kAnimSampleFps = 30;
inline constexpr std::int64_t kMaxAnimSamples = 10'000'000; // intervalos por animación

// Instantes (s locales, redondeados a 6 decimales, ordenados y sin repetir) en los que
// el export muestrea las animaciones: a 30 fps dentro de la ventana de cada una.
[[nodiscard]] std::vector<double> animation_sample_times(const TimelineClip& clip, double clip_dur);

} // namespace gc::core
