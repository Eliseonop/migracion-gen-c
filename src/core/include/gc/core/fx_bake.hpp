#pragma once

// Lo que decide fx_bake.py de video-yt antes de hornear los efectos de vídeo (`clip.vfx`) de un
// clip, sin hornear: qué efectos aplica, si su margen puede verse y cómo es el clip que el export
// compone después con el horneado como material (`baked_clip`).
//
// El horneado en sí (el tramo del clip con los efectos, fotograma a fotograma) lo hace gc_render
// (vfx.hpp) con lo que el compositor decodifica.

#include <optional>
#include <string_view>

#include "gc/core/error.hpp"
#include "gc/core/json.hpp"
#include "gc/core/model.hpp"

namespace gc::core {

// active_vfx: las entradas aplicables (objetos activos de un efecto del catálogo que no es un
// generador), normalizadas como Python: {uid, effect, v, enabled, params, from, to}. Error si la
// versión (`v`) no se pasa a entero, como el int() de Python.
[[nodiscard]] Result<Json> active_vfx(const TimelineClip& clip);

// has_vfx: un vídeo, una imagen o un motion con algún efecto aplicable.
[[nodiscard]] Result<bool> has_vfx(const TimelineClip& clip);

// pad_frac: el margen de la pila (la suma de los `pad` de sus efectos, como mucho 0,5).
[[nodiscard]] double vfx_pad_fraction(const Json& entries);

// pad_allowed: el margen solo se ve en un objeto libre (overlay) que enseña la imagen ENTERA al
// principio, a la mitad y al final del clip. `iw`×`ih`: el material; `W`×`H`: la salida.
[[nodiscard]] bool vfx_pad_allowed(const TimelineClip& clip, double iw, double ih, double W, double H);

// _remap_reframe: los keyframes del encuadre (en tiempo de FUENTE) pasados a tiempo local del clip,
// como quedan en el horneado (entra en 0, a velocidad 1 y sin revés).
[[nodiscard]] std::optional<Reframe> remap_reframe_to_local(const TimelineClip& clip);

// baked_clip: la copia del clip que compone el export con el horneado `baked_filename` como
// material: entra en 0 y dura lo mismo, a velocidad 1, sin revés, sin «Eliminar fondo» ni efectos,
// con el encuadre en tiempo local; una imagen pasa a ser un vídeo.
[[nodiscard]] TimelineClip baked_clip(const TimelineClip& clip, std::string_view baked_filename);

} // namespace gc::core
