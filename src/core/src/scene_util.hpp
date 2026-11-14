#pragma once

// Lo que comparten scene_direction.py y motion/segment_context.py de video-yt: el `_r` (round a 3
// decimales), `_clean` y `_clip_text`, el min()/max() de Python con dos reales, el formato «.Nf» de
// las f-strings y el orden con NaN.

#include <cmath>
#include <cstddef>
#include <format>
#include <string>
#include <string_view>

#include "py.hpp"

namespace gc::core::detail {

// _r: round(float(x), 3).
[[nodiscard]] inline double r3(double x) { return py_round(x, 3); }

// min(a, b) y max(a, b) de Python con reales: el primero, salvo que el segundo sea estrictamente
// menor (o mayor). Con un NaN no es simétrico: min(1.0, nan) es 1.0 y min(nan, 1.0), nan.
[[nodiscard]] inline double py_min(double a, double b) { return b < a ? b : a; }
[[nodiscard]] inline double py_max(double a, double b) { return b > a ? b : a; }

// max(0.0, x): 0.0 salvo que x sea mayor (un NaN da 0.0).
[[nodiscard]] inline double max0(double x) { return py_max(0.0, x); }

// float(x or 0.0) de un real: el -0.0 es falso, así que sale 0.0.
[[nodiscard]] inline double or0(double x) { return x == 0.0 ? 0.0 : x; }

// a < b para ordenar con std::stable_sort: con un NaN, `<` deja de ser un orden débil estricto (y en
// C++ eso es indefinido). Los NaN van al final, como iguales entre sí. Divergencia: el sort de Python
// con un NaN no falla, pero el orden que da depende de las comparaciones que haga timsort.
[[nodiscard]] inline bool less_nan_last(double a, double b) {
    if (std::isnan(a)) {
        return false;
    }
    return std::isnan(b) || a < b;
}

// f"{x:.Nf}" de Python: el decimal correctamente redondeado, como std::format, salvo el NaN, que en
// Python es «nan» también con el signo puesto (std::format escribiría «-nan»).
[[nodiscard]] inline std::string py_fixed(double x, int precision) {
    if (std::isnan(x)) {
        return "nan";
    }
    return std::format("{:.{}f}", x, precision);
}

// `_clean(text, limit)` de scene_direction.py y `_clip_text` de segment_context.py sobre un texto ya
// convertido: re.sub(r"\s+", " ", t).strip() (los espacios Unicode juntados en uno) y, si pasa de
// `limit` caracteres, t[:limit - 1].rstrip() + «…».
[[nodiscard]] inline std::string clean_text(std::string_view text, std::size_t limit) {
    std::string t;
    for (const std::string& part : py_split(text)) {
        if (!t.empty()) {
            t += ' ';
        }
        t += part;
    }
    if (py_len(t) <= limit) {
        return t;
    }
    return py_rstrip(py_slice(t, limit - 1)) + "\xE2\x80\xA6";
}

} // namespace gc::core::detail
