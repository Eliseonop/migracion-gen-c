#pragma once

// Cambiar la pose de un clip desde el inspector o el lienzo (commitPose de VideoEditor.jsx con
// upsertKeyframeAt, shouldKeyframe y poseKeyframed de clipKeyframes.js). Regla «mover ≠ animar»:
// el valor se escribe en su sitio estático (estilo del texto, figura, transform u opacidad del
// clip) y solo se anima si el clip ya tiene keyframes de pose.
//
// Es la versión del editor: los keyframes caen en fotogramas enteros (`fps`) y dos a menos de
// medio fotograma son el mismo. La de las ops (keyframes.hpp, como Python) no ajusta al
// fotograma y junta los que están a menos de 0,06 s.

#include <optional>
#include <string>
#include <vector>

#include "gc/core/ids.hpp"
#include "gc/core/json.hpp"
#include "gc/core/model.hpp"

namespace gc::core {

// snapToFrame: el fotograma entero más cercano (a los fps de la timeline).
[[nodiscard]] double snap_to_frame(double t, double fps);
// normalizeItems: id (nuevo si falta), t numérica, curva y bézier normalizadas y props; ordenados.
[[nodiscard]] std::vector<Json> normalize_kf_items(const Json& items, IdSource& ids);
// ¿Tiene keyframes de pose (no solo de estilo del texto)?
[[nodiscard]] bool pose_keyframed(const TimelineClip& clip);
// ¿El cambio `patch` ({prop: valor}) cae en un keyframe?
[[nodiscard]] bool should_keyframe(const TimelineClip& clip, const Json& patch);
// upsertKeyframeAt: el snapshot en `local_t` con `patch` encima (actualiza el de ese fotograma).
void upsert_keyframe_ui(TimelineClip& clip, double local_t, const Json& patch,
                        const std::optional<std::string>& interpolation, double fps, IdSource& ids);
// applyStaticPose de VideoEditor.jsx.
void apply_static_pose(TimelineClip& clip, const Json& patch);
// commitPose de VideoEditor.jsx para un clip: `local_t` es el cabezal menos su inicio.
void commit_pose(TimelineClip& clip, const Json& patch, double local_t, double fps, IdSource& ids);

} // namespace gc::core
