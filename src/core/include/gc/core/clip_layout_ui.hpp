#pragma once

// Geometría de la vista previa del editor (lo de frontend/src/lib/clipLayout.js y selectionFrame.js
// que solo usa la interfaz): el recuadro Main dentro del lienzo, la posición como CapCut, los
// tiradores del modal Recortar y los del recuadro de transform, y freeFrameAt.

#include <array>
#include <optional>
#include <string>
#include <string_view>

#include "gc/core/clip_layout.hpp"
#include "gc/core/error.hpp"
#include "gc/core/ids.hpp"
#include "gc/core/model.hpp"
#include "gc/core/pose.hpp"

namespace gc::core {

constexpr double kHandlePad = 12; // HANDLE_PAD
constexpr double kRotateGap = 26; // ROT_GAP

// frameOf: top, bottom, left o right (huecos legados) o full.
[[nodiscard]] std::string frame_of(const TimelineClip& clip);
// slotAspectOf: el aspecto del hueco del clip dentro de la salida.
[[nodiscard]] double slot_aspect_of(const TimelineClip& clip, double out_aspect);

// toCapcutPos y fromCapcutPos: (0, 0) en el centro, el borde a ±ancho y ±alto y la Y hacia arriba.
[[nodiscard]] std::pair<double, double> to_capcut_pos(double x, double y, double w, double h);
[[nodiscard]] std::pair<double, double> from_capcut_pos(double X, double Y, double w, double h);

struct Rect {
    double x = 0;
    double y = 0;
    double w = 0;
    double h = 0;
};
// frameRectOf: el recuadro Main (aspecto de salida) centrado en el lienzo de cw × ch. Un aspecto
// que no sea > 0 toma el del lienzo.
[[nodiscard]] Rect frame_rect_of(double cw, double ch, double view_zoom = 1, double out_aspect = 0);

// destRect de clipLayout.js: en píxeles de salida, escala 1 = 1 px de fuente.
[[nodiscard]] DestRect dest_rect_ui(const Pose& transform, const SourceCrop& crop, double out_w, double out_h);
[[nodiscard]] DestRect dest_rect_on_canvas(const Pose& transform, const SourceCrop& crop, double out_w, double out_h,
                                           double canvas_w, double canvas_h);
[[nodiscard]] DestRect dest_rect_on_frame(const Pose& transform, const SourceCrop& crop, double out_w, double out_h,
                                          const Rect& frame);
[[nodiscard]] SourceCrop source_crop_px(const CropWindow& crop, double src_w, double src_h);

// --- Recortar -------------------------------------------------------------------------------

[[nodiscard]] CropWindow clamp_crop(double cx, double cy, double wf, double hf);
// Tamaño al arrastrar una esquina con el centro fijo.
[[nodiscard]] std::pair<double, double> crop_size_from_corner(double nx, double ny, double cx, double cy);
// Un tirador: hx, hy ∈ {-1, 0, 1} dicen qué borde arrastra.
struct CropHandle {
    int hx = 0;
    int hy = 0;
    double x = 0;
    double y = 0;
};
[[nodiscard]] std::array<CropHandle, 8> crop_handle_norms(const CropWindow& crop);
// El tirador bajo el puntero (coordenadas 0–1 sobre un rect de rect_w × rect_h px), con `px` de margen.
[[nodiscard]] std::optional<std::pair<int, int>> crop_handle_at(double nx, double ny, const CropWindow& crop,
                                                                double rect_w, double rect_h, double px = 12);
[[nodiscard]] bool inside_crop(double nx, double ny, const CropWindow& crop);
// Aspecto libre: el borde arrastrado sigue al puntero y el opuesto queda anclado.
[[nodiscard]] CropWindow resize_crop_free(const CropWindow& start, int hx, int hy, double nx, double ny,
                                          double min = 0.05);
// Aspecto bloqueado (r = wf / hf), anclando el borde opuesto.
[[nodiscard]] CropWindow resize_crop_locked(const CropWindow& start, int hx, int hy, double nx, double ny, double r,
                                            double min = 0.1);
// cropRatioNorm: wf / hf de un recorte con esa proporción (free, original, output, 16:9…); nada si es libre.
[[nodiscard]] std::optional<double> crop_ratio_norm(std::string_view ratio_id, double src_aspect, double out_aspect);
// El mayor recorte con relación `rn` que cabe, centrado en el actual.
[[nodiscard]] CropWindow fit_crop_ratio(const CropWindow& crop, double rn);
// cropCursor: nwse-resize, nesw-resize, ew-resize, ns-resize o move.
[[nodiscard]] std::string crop_cursor(int hx, int hy);

// --- Objeto libre ---------------------------------------------------------------------------

// freeFrameAt: el encuadre (cx, cy, zoom sobre la fuente) que equivale a lo que se ve del clip
// libre en el instante local `t`.
[[nodiscard]] Frame free_frame_at(const TimelineClip& clip, double t, double src_w, double src_h, double out_w,
                                  double out_h);
// hitTransformHandle: "rotate", "scale", "move" o nada para el puntero (px, py) sobre la caja `dest`.
[[nodiscard]] std::optional<std::string> hit_transform_handle(double px, double py, const DestRect& dest,
                                                              double pad = kHandlePad);

// --- Panel de Transformación (EdTransform.jsx) -----------------------------------------------

// Lo que enseña el panel en el instante local `t`. Con `capcut`, X e Y son la posición de CapCut
// (frame_w × frame_h); sin ella (un clip que llena el cuadro), el encuadre sobre la fuente en %.
// `height_scale` = alto de salida / alto de la fuente (1 si no se sabe).
struct TransformView {
    bool show = false;
    bool xy = false;
    bool scale = false;
    bool zoom_scale = false;
    bool rotation = false;
    bool capcut = false;
    double x = 0;
    double y = 0;
    double scale_pct = 100;
    double rotation_deg = 0;
};
[[nodiscard]] TransformView transform_view(const TimelineClip& clip, double t, double frame_w, double frame_h,
                                           double height_scale = 1);
// Parches para commitPose: mover a (X, Y), escalar a `pct` % y restablecer.
[[nodiscard]] Json transform_pos_patch(const TimelineClip& clip, double X, double Y, double frame_w, double frame_h);
[[nodiscard]] Json transform_scale_patch(const TimelineClip& clip, double pct, double height_scale = 1);
[[nodiscard]] Json transform_reset_patch(const TimelineClip& clip, double height_scale = 1);

// confirmCrop de VideoEditor.jsx: el recorte (fracciones de la fuente) y el giro del modal Recortar.
// En un clip animado (keyframes o varios del encuadre) el centro entra como keyframe en el cabezal
// (`local_t`, y `src_t` en la fuente); si no, el recorte vale para todo el clip.
[[nodiscard]] Result<TimelineClip> confirm_crop(const TimelineClip& clip, const CropWindow& crop, double rotation,
                                                double local_t, double src_t, double fps, IdSource& ids);

} // namespace gc::core
