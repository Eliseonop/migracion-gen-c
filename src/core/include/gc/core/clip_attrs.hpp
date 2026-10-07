#pragma once

// Copiar y pegar ATRIBUTOS entre clips, eligiendo cuáles («Pegar atributos» de CapCut;
// espejo de backend/app/clip_attrs.py y frontend/src/lib/clipAttrs.js).
//
// Un atributo es un GRUPO de propiedades (posición, mezcla, animación, máscara,
// audio…) que se pega en uno o varios clips sin tocar su contenido, su id ni su sitio
// en la timeline. Como en Python, se trabaja sobre el model_dump() de los clips y el
// resultado se vuelve a validar como TimelineClip.

#include <array>
#include <string>
#include <string_view>
#include <vector>

#include "gc/core/error.hpp"
#include "gc/core/ids.hpp"
#include "gc/core/json.hpp"
#include "gc/core/model.hpp"

namespace gc::core {

// Grupos en el orden en que se aplican (y en que consumen ids).
inline constexpr std::array<std::string_view, 14> kAttrGroupIds{
    "transform", "flip",   "blend",  "animation", "transitions", "animations", "crop",
    "effects",   "vfx",    "mask",   "chroma",    "style",       "speed",      "audio"};

// ¿Se puede pegar el grupo `group` de `source` en `target`? (tipos de clip admitidos;
// el estilo solo entre clips del mismo tipo).
[[nodiscard]] bool group_applies(std::string_view group, const TimelineClip& source, const TimelineClip& target);

// model_dump() de `target` con los grupos `groups` de `source` pegados; los que no
// valen para ese par se ignoran. Los keyframes del destino se recalculan: las
// propiedades pegadas vienen del origen y el resto se conserva. Ids nuevos para los
// keyframes, las máscaras y los efectos animados copiados. Error si el origen tiene
// datos que Python no sabría leer (p. ej. un keyframe con un valor que no es número).
[[nodiscard]] Result<Json> paste_attributes(const TimelineClip& target, const TimelineClip& source,
                                            const std::vector<std::string>& groups, IdSource& ids);

} // namespace gc::core
