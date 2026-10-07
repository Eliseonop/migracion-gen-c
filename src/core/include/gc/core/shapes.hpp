#pragma once

// Figuras vectoriales: modelo normalizado (espejo de la parte de datos de
// backend/app/shapes.py y frontend/src/lib/shapes.js). El dibujo es cosa del
// render (gc_render, F3).

#include <utility>
#include <vector>

#include "gc/core/error.hpp"
#include "gc/core/json.hpp"

namespace gc::core {

inline constexpr double kShapeDefaultDuration = 5.0;
inline constexpr double kCinemaBarMax = 0.45;

// Valores por defecto de un tipo de figura (rect, circle, arrow, path, letterbox…).
[[nodiscard]] Json default_shape(const Json& type);

// Figura con todos sus campos, acotados. Error si un valor numérico no es un
// número (float() de Python falla).
[[nodiscard]] Result<Json> normalize_shape(const Json& raw);

// Anclas [x, y] válidas de un trazado (las que no son números finitos se ignoran).
[[nodiscard]] std::vector<std::pair<double, double>> path_anchors(const Json& points);

// Figura "path" desde puntos en 0–1 del cuadro (la caja es la de los puntos).
// `opts` = otros campos de la figura (relleno, trazo…).
[[nodiscard]] Result<Json> path_shape(const Json& frame_points, const Json& opts);

// Barras de cine: proporciones con nombre ("2.39", "2", "1.85", "16:9").
[[nodiscard]] const Json* cinema_ratio(std::string_view name);
// Alto de cada barra para que lo visible tenga la proporción `ratio` (0 = no hay barras).
[[nodiscard]] Json cinema_bar(double out_aspect, double ratio);

} // namespace gc::core
