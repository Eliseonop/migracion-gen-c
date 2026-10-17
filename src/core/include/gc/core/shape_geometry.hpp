#pragma once

// Geometría de las figuras (espejo de shape_geometry, path_points, trim_polyline, dash_runs,
// flip_geometry, shape_draw_at y rasterize_shape de backend/app/shapes.py): los anillos y líneas
// de cada tipo en una caja de 0–100 y, colocados en el cuadro, las operaciones de dibujo con las
// que el export rasteriza su PNG. El dibujo en sí es de gc_render.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "gc/core/clip_layout.hpp"
#include "gc/core/error.hpp"
#include "gc/core/json.hpp"
#include "gc/core/model.hpp"

namespace gc::core {

using Polyline = std::vector<Point>;

struct ShapeGeometry {
    std::vector<Polyline> fills;   // anillos rellenos (con su contorno si hay trazo)
    std::vector<Polyline> strokes; // líneas abiertas
};

// Pasos de Catmull-Rom entre dos anclas de un trazado suave (PATH_STEPS).
inline constexpr int kPathSteps = 12;

// Los anillos y líneas de un tipo de figura en su caja de 0–100 (shape_geometry). `opts` es la
// figura (normalizada o no): cornerRadius, sides, arrowHead, bar, points, closed y smooth.
[[nodiscard]] ShapeGeometry shape_geometry(std::string_view type, const Json& opts);

// Polilínea (o anillo si `closed`) de un trazado por sus anclas: Catmull-Rom si `smooth` y hay
// más de dos (path_points).
[[nodiscard]] Polyline path_points(const Json& points, bool closed, bool smooth);

// El primer tramo de la polilínea que cubre la fracción `frac` de su largo (trim_polyline).
[[nodiscard]] Polyline trim_polyline(const Polyline& pts, double frac);

// La polilínea troceada con el patrón (encendido, apagado), empezando por un tramo encendido como
// setLineDash; con encendido 0, cada tramo es un punto (dash_runs).
struct DashRuns {
    std::vector<Polyline> runs;
    std::vector<Point> dots;
};
[[nodiscard]] DashRuns dash_runs(const Polyline& pts, std::pair<double, double> pattern);

// Patrón del estilo de trazo en múltiplos del grosor `lw`: "dash" = (2lw, 2lw), "dot" = (0, 2lw)
// y "solid" (o cualquier otro), ninguno (dash_pattern).
[[nodiscard]] std::optional<std::pair<double, double>> dash_pattern(std::string_view dash, double lw);

// Reflejo en los ejes de la figura (flip_geometry).
[[nodiscard]] ShapeGeometry flip_geometry(ShapeGeometry geo, bool horizontal, bool vertical);

// Cuánto del trazo está dibujado (0–1) en el instante local `t`: el `draw` de los keyframes o el
// de la figura (shape_draw_at). Error si la figura no se puede normalizar.
[[nodiscard]] Result<double> shape_draw_at(const TimelineClip& clip, double t);

// Una operación del dibujo de una figura, en px del cuadro de salida con el píxel (i, j) en
// [i, i+1) × [j, j+1) (el convenio de Skia).
struct ShapeOp {
    enum class Kind { Fill, Line, Dot };
    Kind kind = Kind::Fill;
    Polyline points;     // el anillo (Fill) o la línea (Line)
    bool closed = false; // Line cerrada
    double alpha = 1.0;  // Fill: la fracción dibujada («dibujar trazo»)
    Point center;        // Dot
    double radius = 0.0; // Dot
};

struct ShapeRgb {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
};

// Lo que pinta rasterize_shape para una figura colocada: las operaciones en orden, el grosor del
// trazo, los colores y la opacidad de toda la figura. El export rasteriza con OpenCV al doble de
// resolución (vértices en enteros, half-even) y reduce con INTER_AREA: aquí los vértices salen
// redondeados igual y llevados al cuadro de salida (x₂ / 2 + 0,25), y el grosor es el entero del
// doble entre 2. Uniones y extremos redondos (los de las líneas gruesas de OpenCV).
struct ShapePlan {
    std::vector<ShapeOp> ops;
    double line_width = 0.0; // px de salida
    ShapeRgb fill;
    ShapeRgb stroke;
    double opacity = 1.0;
};

// La pose (x, y en 0–1 del cuadro; escala; giro en grados; opacidad) sustituye a la del estilo,
// como applyShapePose; `draw` es la fracción dibujada (shape_draw_at); `flip` el del clip. El
// cuadro es de `width` × `height` px.
struct ShapePose {
    double x = 0.5;
    double y = 0.5;
    double scale = 1.0;
    double rotation = 0.0;
    double opacity = 1.0;
};
[[nodiscard]] Result<ShapePlan> shape_plan(const Json& shape, const ShapePose& pose, double draw, Flip flip, int width,
                                           int height);

} // namespace gc::core
