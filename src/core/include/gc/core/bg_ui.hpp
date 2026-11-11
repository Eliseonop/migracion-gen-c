#pragma once

// «Eliminar fondo» en el editor (VideoEditor.jsx y EdBgRemove.jsx): lo que la Eliminación automática hace con
// `clip.bg_removal`. Todo devuelve el `bg_removal` NUEVO del clip, normalizado (normalizeBg de lib/clipBg.js,
// que es normalize_bg): el editor lo guarda en un paso de deshacer, como el setClips de patchBg.
//
// El matte lo calcula el backend de video-yt con el job POST /api/projects/{id}/bg-removal (bg_auto_start)
// hasta que llegue la IA local (F6); al acabar, bg_auto_finished escribe su clave de caché en el clip, y el
// render lo lee de data/bgcache (bg_matte.hpp).

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include "gc/core/frame.hpp"
#include "gc/core/json.hpp"
#include "gc/core/model.hpp"

namespace gc::core {

// Un motor de BG_PROVIDERS (clipBg.js), en su orden: id, nombre y pista de la UI. Los SAM son los de la
// Eliminación personalizada (interactive); RVM solo recorta personas (people).
struct BgProviderInfo {
    std::string_view id;
    std::string_view label;
    std::string_view hint;
    bool people = false;
    bool interactive = false;
};
[[nodiscard]] std::span<const BgProviderInfo> bg_provider_catalog();

// isInteractiveProvider: el id empieza por «sam» (SAM: el pincel es el prompt).
[[nodiscard]] bool is_interactive_bg_provider(const Json& provider);

// clipBg(clip) || defaultBg(): el bg_removal normalizado, o el de un clip que nunca lo tuvo.
[[nodiscard]] Json clip_bg_or_default(const TimelineClip& clip);

// patchBgAuto: `patch` sobre `auto` ({...bg.auto, ...patch}) y todo normalizado.
[[nodiscard]] Json bg_patch_auto(const TimelineClip& clip, const Json& patch);

// toggleBgAuto: enciende o apaga la Eliminación automática. Al encenderla por primera vez (sin calcular y con
// el motor por defecto) propone el del material (recommended_bg_provider) con su alto de matte; si venía de
// la personalizada (SAM), además tira las marcas y el matte calculado.
[[nodiscard]] Json bg_toggle_auto(const TimelineClip& clip, bool on);

// changeBgAuto: un cambio del panel. Cambiar de motor, de detalle (mask_height) o la estabilización obliga a
// recalcular (base_key vacío, status idle); un motor automático nuevo trae su alto de matte recomendado.
[[nodiscard]] Json bg_change_auto(const TimelineClip& clip, const Json& patch);

// patchBgChroma y patchBgOutline: `patch` sobre el croma o el contorno, y todo normalizado.
[[nodiscard]] Json bg_patch_chroma(const TimelineClip& clip, const Json& patch);
[[nodiscard]] Json bg_patch_outline(const TimelineClip& clip, const Json& patch);
// resetBgChroma: el croma a sus valores por defecto, encendido o apagado como estaba.
[[nodiscard]] Json bg_reset_chroma(const TimelineClip& clip);

// startBgAutoFor: el cuerpo del POST /bg-removal (bgClipDescriptor, el material y su tramo, más el `auto` de
// antes de lanzarlo) y el bg_removal mientras se calcula (status running). Nada si no es vídeo ni imagen.
struct BgAutoStart {
    Json body;
    Json bg_removal;
};
[[nodiscard]] std::optional<BgAutoStart> bg_auto_start(const TimelineClip& clip);

// preferredSamProvider: el modelo SAM al encender la personalizada: el actual si ya es SAM; si no, el primero
// ya descargado del catálogo (`catalog`, el de /api/bg/providers) y, sin ninguno, el de calidad.
[[nodiscard]] std::string preferred_sam_provider(const Json& current, const Json& catalog);
// toggleBgCustom: enciende o apaga la Eliminación personalizada. Al venir de la automática tira las marcas y
// el matte y vuelve al alto de siempre.
[[nodiscard]] Json bg_toggle_custom(const TimelineClip& clip, bool on, const Json& catalog);

// Los trazos del pincel (auto.edits), en el espacio normalizado de la FUENTE.
// startBgBrush: el trazo que empieza en `first`, con el fotograma (t) y la herramienta si es SAM.
[[nodiscard]] Json bg_new_stroke(const TimelineClip& clip, std::string_view op, std::string_view tool, double size,
                                 double head, Point first);
// El siguiente punto del arrastre, o nada si está a menos de 0,003 del anterior.
[[nodiscard]] std::optional<Json> bg_stroke_point(Point last, Point next);
[[nodiscard]] Json bg_add_stroke(const TimelineClip& clip, const Json& stroke);     // addBgStroke
[[nodiscard]] std::optional<Json> bg_extend_stroke(const TimelineClip& clip, const Json& point); // nada sin trazos
[[nodiscard]] Json bg_undo_edit(const TimelineClip& clip);                          // undoBgEdit
[[nodiscard]] Json bg_clear_edits(const TimelineClip& clip);                        // clearBgEdits

// Fotogramas del matte y marcas de SAM (lib/clipBg.js).
[[nodiscard]] std::int64_t bg_matte_frame_index(double src_t, const Json& mask_fps);
[[nodiscard]] double bg_matte_frame_time(std::int64_t index, const Json& mask_fps);
[[nodiscard]] Json bg_edits_at_frame(const Json& edits, std::int64_t index, const Json& mask_fps);
[[nodiscard]] Json bg_mark_frames(const Json& edits, const Json& mask_fps); // [{idx, t, count}], en orden
[[nodiscard]] bool bg_manual_mark(const Json& edit);                         // isManualMark

// canvasToSourceNorm: un punto del cuadro → la fuente normalizada, por la inversa del blit de la capa (la que
// dibuja el render); nada si cae fuera del material.
[[nodiscard]] std::optional<Point> bg_frame_to_source(const FrameLayer& layer, double x, double y, double src_w,
                                                      double src_h);
// bgBrushRadiusPx: el radio en px del cuadro de un pincel en fracción del alto de la fuente.
[[nodiscard]] double bg_brush_radius(const FrameLayer& layer, double size, double src_h);

// exportBgCutout: el cuerpo del POST /bg-cutout (el material, su tramo, la etiqueta y el bg_removal tal como
// está), o nada si no hay qué hornear (ni matte listo ni croma encendido).
[[nodiscard]] std::optional<Json> bg_cutout_body(const TimelineClip& clip);

// El final del job (el efecto de bgJob.status): `done` con `bg_removal` escribe la clave de caché, el motor,
// su versión, mask_fps y mask_height del resultado (status ready); `error`, su error («Error» si viene vacío).
// Nada mientras sigue (o si acaba sin resultado).
[[nodiscard]] std::optional<Json> bg_auto_finished(const TimelineClip& clip, const Json& job);

} // namespace gc::core
