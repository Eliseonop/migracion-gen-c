#pragma once

// Semántica exacta de algunas operaciones de Python que la lógica de edición
// necesita reproducir para dar el mismo resultado que video-yt.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "gc/core/json.hpp"

namespace gc::core::detail {

// round(x, ndigits) de Python: redondeo correcto sobre el valor decimal EXACTO del
// double, con empates al par (round(2.675, 2) == 2.67, round(0.0625, 3) == 0.062).
// `std::round(x * 1000) / 1000` NO es equivalente.
[[nodiscard]] double py_round(double x, int ndigits);

// repr(x) / str(x) de un float de Python ("1.0", "0.1", "1e-05", "inf").
[[nodiscard]] std::string py_repr(double x);

// str.strip() de Python (espacios Unicode) y s[:n] (n caracteres, no bytes) sobre UTF-8.
[[nodiscard]] std::string py_strip(std::string_view utf8);
[[nodiscard]] std::string py_slice(std::string_view utf8, std::size_t n);

// float(x) de Python: números, bool y texto numérico. nullopt = TypeError/ValueError.
[[nodiscard]] std::optional<double> py_float(const Json& j);
// int(x) de Python: enteros, bool, reales (truncando) y texto ENTERO. nullopt = error.
[[nodiscard]] std::optional<std::int64_t> py_int(const Json& j);
// Operando numérico de una comparación (`x < 0`): int, float o bool; el texto no vale.
[[nodiscard]] std::optional<double> py_number(const Json& j);

// `_num(v, default)` de video-yt: float(v) o el defecto si falla o es NaN.
[[nodiscard]] double py_num(const Json& j, double fallback);

// Lo que recorre `for x in v` en Python: lista → elementos, dict → claves,
// texto → caracteres. nullopt = TypeError (números, bool, null).
[[nodiscard]] std::optional<std::vector<Json>> py_iter(const Json& j);

// str.rstrip() y len() de Python sobre UTF-8 (caracteres, no bytes).
[[nodiscard]] std::string py_rstrip(std::string_view utf8);
[[nodiscard]] std::size_t py_len(std::string_view utf8);

// str.split() sin argumentos: trozos separados por cualquier espacio Unicode.
[[nodiscard]] std::vector<std::string> py_split(std::string_view utf8);

// `_num` de clip_bg: como py_num pero también descarta ±inf.
[[nodiscard]] double py_num_finite(const Json& j, double fallback);

// min(hi, max(lo, v)) de Python conservando el TIPO del ganador: con límites
// enteros, un valor acotado sale entero (max(0, -0.5) es el int 0). Importa
// porque el JSON distingue 0 de 0.0.
[[nodiscard]] Json py_clamp(const Json& v, const Json& lo, const Json& hi);

} // namespace gc::core::detail
