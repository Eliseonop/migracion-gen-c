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

// repr(x) de Python para un valor JSON: None, True, 3, 3.0, 'texto' (con las comillas y
// los escapes de Python), [1, 2] y {'a': 1}. Para saber qué caracteres no ASCII se
// escapan (\xa0, \u200b…) usa una tabla aproximada de los no imprimibles.
[[nodiscard]] std::string py_repr(const Json& j);

// str(Path(x)) de Python 3.14 en Windows: «/» → «\», sin partes vacías ni «.» y sin la
// barra final; «\\servidor\recurso» gana la raíz («\\servidor\recurso\») y «» es «.».
// La unidad es un carácter de UTF-16 (no uno de fuera del plano básico) + «:».
[[nodiscard]] std::string py_path_str(std::string_view text);
// Path(x).name de Python 3.14 en Windows: el último componente, sin la unidad («C:x» → «x»)
// ni el recurso UNC, y sin «.» (que no cuenta).
[[nodiscard]] std::string py_path_name(std::string_view text);
// Path(x).suffix de Python 3.14: «x.mp3» → «.mp3», «x.» → «.», «.mp3» y «..» → «».
[[nodiscard]] std::string py_path_suffix(std::string_view text);
// Path(x).stem: el nombre sin el sufijo («.hidden» y «..» se quedan como están).
[[nodiscard]] std::string py_path_stem(std::string_view text);

// ¿`c` es un carácter de palabra (\w) para las regex de Python (str.isalnum() o «_»)?
// Exacto en ASCII y Latin-1; fuera, aproximado por bloques de Unicode.
[[nodiscard]] bool py_word_char(char32_t c);

// Siguiente carácter de un texto UTF-8 a partir del byte `i` (que avanza).
[[nodiscard]] char32_t utf8_next(std::string_view utf8, std::size_t& i);

// urllib.parse.quote(s) de Python (safe="/"): los bytes UTF-8 fuera de letras y cifras
// ASCII, «_.-~» y «/» van como %XX.
[[nodiscard]] std::string py_quote(std::string_view text);

// str.lower() de Python sobre UTF-8: minúsculas de Unicode completas (también las que
// cambian de longitud, «İ» → «i̇») y la sigma final («ΟΔΟΣ» → «οδος»). Tablas de
// tools/gen/gen_py_lower.py; los bytes que no son UTF-8 válido se copian tal cual.
[[nodiscard]] std::string py_lower(std::string_view utf8);

// str.title() de Python sobre UTF-8: cada carácter pasa a título si el anterior no tiene caja
// y a minúsculas si la tiene (con la sigma final de lower()). Tablas de tools/gen/gen_py_text.py.
[[nodiscard]] std::string py_title(std::string_view utf8);

// str.isdecimal() de un carácter: el `\d` de las regex de texto de Python.
[[nodiscard]] bool py_isdecimal(char32_t c);

// El plegado de `_norm` y `_slug` de stick_library: NFKD, sin las marcas que combinan y en
// minúsculas, con cada carácter que no es [a-z0-9] cambiado por «_». (`_norm` quita los «_»;
// `_slug` junta cada racha en uno.)
[[nodiscard]] std::string py_nfkd_fold(std::string_view utf8);

// a == b de Python entre valores JSON: los números se comparan por su valor exacto también
// entre int, float y bool (True == 1 == 1.0; 2**53 + 1 != 2.0**53), y las listas y los
// dicts por contenido (los dicts sin mirar el orden).
[[nodiscard]] bool py_equal(const Json& a, const Json& b);

// x % y de Python con reales: el resultado lleva el signo del divisor
// ((-90.0) % 360 == 270.0), no el del dividendo como fmod.
[[nodiscard]] double py_mod(double x, double y);

// Letras y cifras ASCII sin depender del locale. std::isalnum y std::tolower sí dependen:
// con un locale de un byte (Qt fija el del sistema en Linux y macOS) cambiarían los bytes
// ≥ 0x80 de un texto UTF-8.
[[nodiscard]] constexpr bool ascii_alnum(char c) noexcept {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}
[[nodiscard]] constexpr char ascii_lower(char c) noexcept {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}
[[nodiscard]] constexpr char ascii_upper(char c) noexcept {
    return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
}
// Pasa a minúsculas solo las letras ASCII.
inline void ascii_lower_inplace(std::string& s) noexcept {
    for (char& c : s) {
        c = ascii_lower(c);
    }
}

} // namespace gc::core::detail
