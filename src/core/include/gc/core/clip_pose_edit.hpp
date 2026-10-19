#pragma once

// Cambiar la pose de un clip desde el inspector o el lienzo (commitPose de VideoEditor.jsx con
// upsertKeyframeAt, shouldKeyframe y poseKeyframed de clipKeyframes.js). Regla «mover ≠ animar»:
// el valor se escribe en su sitio estático (estilo del texto, figura, transform u opacidad del
// clip) y solo se anima si el clip ya tiene keyframes de pose.
//
// Es la versión del editor: los keyframes caen en fotogramas enteros (`fps`) y dos a menos de
// medio fotograma son el mismo. La de las ops (keyframes.hpp, como Python) no ajusta al
// fotograma y junta los que están a menos de 0,06 s.

#include <array>
#include <optional>
#include <string>
#include <vector>

#include "gc/core/ids.hpp"
#include "gc/core/json.hpp"
#include "gc/core/keyframes.hpp"
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
// flattenPatch(patch, 'text' | 'shape'): la pose numérica (x, y, escala, giro, opacidad y encuadre).
[[nodiscard]] Json flatten_pose_patch(const Json& patch);
// changeShape de VideoEditor.jsx: la figura con `patch` encima y, si la pose ya está animada, su
// keyframe en el cabezal.
void change_shape(TimelineClip& clip, const Json& patch, double local_t, double fps, IdSource& ids);
// commitPose de VideoEditor.jsx para un clip: `local_t` es el cabezal menos su inicio.
void commit_pose(TimelineClip& clip, const Json& patch, double local_t, double fps, IdSource& ids);

// --- El rombo de keyframe (toggleKeyframeAtPlayhead de VideoEditor.jsx) ---------------------

// kfState: "on" (keyframe de pose en este fotograma), "empty" (animado, sin keyframe aquí) u "off".
[[nodiscard]] std::string kf_state(const TimelineClip& clip, double local_t, double fps);
// keyframeIdAt: el id del keyframe de pose o de encuadre en este fotograma.
[[nodiscard]] std::optional<std::string> keyframe_id_at(const TimelineClip& clip, double local_t, double fps);
// textStyleKfKeys: las propiedades de estilo del texto que tienen keyframes.
[[nodiscard]] std::vector<std::string> text_style_kf_keys(const TimelineClip& clip);
// Pone o quita el keyframe de pose en `local_t` (el rombo). En un texto con estilo animado, quitarlo
// le deja el estilo a ese keyframe.
void toggle_keyframe_at(TimelineClip& clip, double local_t, double fps, IdSource& ids);
// --- Copiar, pegar y duplicar keyframes (Alt+C / Alt+V / Alt+D) -----------------------------

// snapshotProps y clipPropsAt de clipKeyframes.js: lo estático con el encuadre del reframe en el
// instante (en la fuente, in_point + t) y, si hay keyframes, interpolado.
[[nodiscard]] KfProps snapshot_props(const TimelineClip& clip, double local_t);
[[nodiscard]] KfProps ui_props_at(const TimelineClip& clip, double local_t);
// El portapapeles de un keyframe: todos sus valores (pose y estilo), su curva y el tipo del clip.
struct KeyframeBoard {
    Json props = Json::object();
    std::string interpolation = "linear";
    Json bezier;
    std::string src_kind;
};
// copyKeyframeAt: el keyframe del fotograma de `local_t`; nada si no hay.
[[nodiscard]] std::optional<KeyframeBoard> copy_keyframe_at(const TimelineClip& clip, double local_t, double fps);
// pasteKeyframeAt: los valores de los grupos `groups` (KF_GROUPS; nada = todos) en `local_t`.
void paste_keyframe_at(TimelineClip& clip, double local_t, const KeyframeBoard& board,
                       const std::optional<std::vector<std::string>>& groups, double fps, IdSource& ids);
// duplicateKeyframeAt: el keyframe `id` copiado en `target_t` con todos sus valores.
void duplicate_keyframe_at(TimelineClip& clip, std::string_view id, double target_t, double fps, IdSource& ids);

// patchKeyframe: el keyframe `id` en otro instante (al fotograma), con otra curva o con valores.
struct KeyframePatch {
    std::optional<double> t;
    std::optional<std::string> interpolation;
    Json bezier;
    Json props;
};
void patch_keyframe(TimelineClip& clip, std::string_view id, const KeyframePatch& patch, double fps, IdSource& ids);
// moveKeyframe de VideoEditor.jsx: con keyframes, patchKeyframe({t}); si no, el del encuadre (t de fuente).
void move_keyframe(TimelineClip& clip, std::string_view id, double new_t, double fps, IdSource& ids);

// targetInterpItem: el keyframe cuya curva describe cómo se llega al cabezal (el siguiente; si no
// hay, el último). Nada sin keyframes.
[[nodiscard]] std::optional<Json> target_interp_item(const TimelineClip& clip, double local_t, double fps);
// interpBezier: los puntos de control de la curva del keyframe (su bézier o el preset aproximado).
[[nodiscard]] std::array<double, 4> interp_bezier(const Json& item);

// applyVolumeFade de clipKeyframes.js: fundido de volumen de entrada ("in") o salida ("out").
void apply_volume_fade_ui(TimelineClip& clip, double duration, const std::string& side, IdSource& ids,
                          double fade = 0.5);

} // namespace gc::core
