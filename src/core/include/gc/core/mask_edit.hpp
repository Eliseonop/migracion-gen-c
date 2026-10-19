#pragma once

// Máscaras desde el editor (defaultMask de clipMask.js; addMask, removeMask, duplicateMask,
// changeMask y commitMask de VideoEditor.jsx). Se edita la primera máscara del clip (masks[0]):
// su geometría (mx, my, mw, mh, msx, msy, mrot, mfeather) se anima con los keyframes de la pose.

#include <string_view>

#include "gc/core/ids.hpp"
#include "gc/core/json.hpp"
#include "gc/core/model.hpp"

namespace gc::core {

// Tamaño por defecto (~60 % del ancho de salida, en unidades de alto) de cada tipo.
[[nodiscard]] Json default_mask(std::string_view type, double out_aspect, IdSource& ids);
// Máscara nueva delante de las que hay; `target` = "clip" o "adjust".
void add_mask(TimelineClip& clip, std::string_view type, std::string_view target, double out_aspect, IdSource& ids);
void remove_mask(TimelineClip& clip);
// Una copia algo desplazada justo detrás de la primera.
void duplicate_mask(TimelineClip& clip, IdSource& ids);
// Campos estáticos de la primera máscara; cambiar de tipo siembra el tamaño del tipo nuevo y suelta
// los keyframes de tamaño.
void change_mask(TimelineClip& clip, const Json& patch, double out_aspect, IdSource& ids);
// Geometría animable (mx, my…): en su sitio y, si la pose ya está animada, en el keyframe de `local_t`.
void commit_mask(TimelineClip& clip, const Json& patch, double local_t, double fps, IdSource& ids);

} // namespace gc::core
