#pragma once

// La máscara editada sobre la vista previa: lo de frontend/src/lib/clipMask.js que solo usa la interfaz
// (su geometría en el lienzo, los tiradores, qué arrastra un punto y los contornos de la estrella y el
// corazón) y los arrastres de startMaskDrag y startMaskBrush de features/editor/interactions.js.
// La máscara es la normalizada (normalize_mask); `frame`, el recuadro de salida en el lienzo, en sus píxeles.

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "gc/core/clip_layout.hpp"
#include "gc/core/clip_layout_ui.hpp"
#include "gc/core/json.hpp"

namespace gc::core {

// maskGeometry: centro, semiejes (≥ 0,5), giro en radianes, la unidad (el alto del recuadro) y la pluma.
struct MaskGeometry {
    double cx = 0;
    double cy = 0;
    double hw = 0;
    double hh = 0;
    double rot = 0;
    double ref = 0;
    double feather = 0;
};
[[nodiscard]] MaskGeometry mask_geometry(const Json& mask, const Rect& frame);

// toMaskLocal: un punto del lienzo en el espacio de la máscara (unidades de alto, sin su giro).
[[nodiscard]] Point to_mask_local(double px, double py, const Json& mask, const Rect& frame);

// maskHandleBox: la caja de los tiradores. La división y el rollo de película son infinitos a lo ancho:
// su caja es fija (y la división, también de alto).
[[nodiscard]] MaskGeometry mask_handle_box(const Json& mask, const Rect& frame);

// maskHandles: mover (c), ancho (r), alto (b), esquina (br), girar (rot) y pluma (fea), y su caja.
struct MaskHandles {
    Point c;
    Point r;
    Point b;
    Point br;
    Point rot;
    Point fea;
    MaskGeometry box;
};
[[nodiscard]] MaskHandles mask_handles(const Json& mask, const Rect& frame);

// maskHitMode: el arrastre del tirador a menos de `pad` (rotate, feather, corner, width, height, en
// ese orden) o move dentro de la caja; nada fuera.
[[nodiscard]] std::optional<std::string> mask_hit_mode(double px, double py, const Json& mask, const Rect& frame,
                                                       double pad = 13);

// startMaskDrag: el parche de commitMask (mx y my, mrot, mfeather, o mw y mh) de llevar el puntero de p0
// a p con el arrastre `mode` sobre la máscara tal como estaba al empezar. Vacío si `mode` no es ninguno.
[[nodiscard]] Json mask_drag_patch(std::string_view mode, const Json& mask, const Rect& frame, Point p0, Point p);

// startMaskBrush: el punto del pincel en p, en el espacio de la máscara sin su escala y a 4 decimales; el
// primero de un trazo lleva m = 1. Nada si no es el primero y está a menos de 0,004 de `last`.
[[nodiscard]] std::optional<Json> mask_brush_point(const Json& mask, const Rect& frame, Point p, bool first,
                                                   const Json* last);

// resizeCursor de lib/selectionFrame.js: la flecha (ew, nwse, ns o nesw-resize) más cercana a un eje de `deg`°.
[[nodiscard]] std::string resize_cursor(double deg);
// maskCursor de interactions.js: crosshair con el pincel encendido en una máscara de pincel; si no, move,
// rotate o la flecha del tirador, girada con la máscara; vacío fuera de todo.
[[nodiscard]] std::string mask_cursor(double px, double py, const Json& mask, const Rect& frame, bool drawing);

// starPoints y heartPoints: los contornos de la estrella y del corazón, centrados, con semiejes hw y hh.
[[nodiscard]] std::vector<Point> star_points(double hw, double hh, int spikes = 5, double inner = 0.42);
[[nodiscard]] std::vector<Point> heart_points(double hw, double hh, int n = 96);

} // namespace gc::core
