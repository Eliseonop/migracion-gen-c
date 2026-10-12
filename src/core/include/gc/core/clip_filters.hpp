#pragma once

// Filtros de color con intensidad, apilables (#18, los «Filtros» de CapCut). Espejo de
// backend/app/clip_filters.py y frontend/src/lib/clipFilters.js.
//
// Cada filtro es una matriz de color AFÍN 3×4 sobre RGB en gamma (0–1): out = M·rgb + o, con
// el desplazamiento o en la 4.ª columna. La intensidad interpola con la identidad y la pila se
// compone en UNA matriz (`colorchannelmixer` en el export, `feColorMatrix` en el preview).
// Los `look` antiguos (un solo filtro sin intensidad) cuentan como una pila de uno.
//
// La capa de ajuste (#19) usa la misma matriz con brillo, contraste, saturación, los ajustes
// de color de clip_adjust y su intensidad (`adjustment_matrix`).

#include <array>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "gc/core/model.hpp"

namespace gc::core {

// Filas R, G y B; columnas r, g, b y el desplazamiento.
using AffineColorMatrix = std::array<std::array<double, 4>, 3>;

inline constexpr AffineColorMatrix kIdentityColor{{
    {1.0, 0.0, 0.0, 0.0},
    {0.0, 1.0, 0.0, 0.0},
    {0.0, 0.0, 1.0, 0.0},
}};

struct ColorFilterDef {
    std::string_view id;
    std::string_view label;
    std::string_view group;
    AffineColorMatrix matrix;
};

// Los 15 filtros de FILTERS, en su orden, con las matrices compuestas como en Python.
[[nodiscard]] std::span<const ColorFilterDef> color_filters();

struct ClipFilter {
    std::string id;
    double amount = 1.0; // 0–1
};

// Pila de filtros del clip: los de `filters` con un id conocido y su intensidad acotada a 0–1
// (lo que no es un número, 1; NaN, 0); si `filters` no es una lista, el `look` antiguo con
// intensidad 1. Una lista, aunque esté vacía, manda sobre el `look`.
// (Python lanza TypeError con un id que no se puede usar como clave, una lista o un dict;
// aquí ese filtro se salta.)
[[nodiscard]] std::vector<ClipFilter> clip_filters(const TimelineClip& clip);

// A∘B: primero B, después A.
[[nodiscard]] AffineColorMatrix compose_color(const AffineColorMatrix& a, const AffineColorMatrix& b);

// Matriz de un filtro a intensidad `k` (acotada a 0–1; NaN, 0). Un id desconocido, la identidad.
[[nodiscard]] AffineColorMatrix filter_matrix(std::string_view id, double k = 1.0);

// La pila en una matriz, o nullopt si ningún filtro tiene intensidad (> 1e-4).
[[nodiscard]] std::optional<AffineColorMatrix> stack_matrix(const std::vector<ClipFilter>& filters);

// Matriz de una capa de ajuste, o nullopt si no hace nada (a 1e-9 de la identidad): la pila de
// filtros, brillo, contraste y saturación (como los filtros CSS del preview), los ajustes de
// color (`color_matrix`) y la intensidad (`opacity`).
[[nodiscard]] std::optional<AffineColorMatrix> adjustment_matrix(const TimelineClip& clip);

// `colorchannelmixer` de FFmpeg para la matriz (con el desplazamiento en la columna del alfa y
// `format=gbrap,` delante si lo hay), con los coeficientes a 5 decimales.
[[nodiscard]] std::string matrix_ffmpeg(const AffineColorMatrix& m);
// El de la pila del clip ("" si no hay filtros).
[[nodiscard]] std::string filters_ffmpeg(const TimelineClip& clip);

} // namespace gc::core
