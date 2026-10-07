#pragma once

#include <optional>
#include <string>

#include "gc/core/json.hpp"

namespace gc::test {

// Primera diferencia entre `expected` y `actual`, con su ruta ("$.timeline.clips[3].start").
// Comparación estricta, la que exige la paridad con video-yt:
//   - mismo tipo JSON (un entero no es igual a un real: 1 ≠ 1.0),
//   - reales idénticos bit a bit, salvo `tolerance` > 0 (relativa, mínimo absoluto
//     igual a la tolerancia): solo para cálculos con pow()/sin(), cuyo último bit
//     depende de la libm de cada plataforma,
//   - mismas claves y, con check_order, en el mismo orden.
// nullopt = iguales.
[[nodiscard]] std::optional<std::string> json_diff(const Json& expected, const Json& actual,
                                                   bool check_order = true, double tolerance = 0.0);

} // namespace gc::test
