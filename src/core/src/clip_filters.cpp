#include "gc/core/clip_filters.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <initializer_list>

#include "gc/core/clip_adjust.hpp"
#include "json_reader.hpp"
#include "py.hpp"

namespace gc::core {

namespace {

using M = AffineColorMatrix;

// Las piezas de clip_filters.py. Python mezcla 0 enteros con reales; el resultado es el mismo
// en doble (0 · x da el mismo cero con signo que 0.0 · x).
M sat(double s) {
    return {{
        {0.213 + 0.787 * s, 0.715 - 0.715 * s, 0.072 - 0.072 * s, 0.0},
        {0.213 - 0.213 * s, 0.715 + 0.285 * s, 0.072 - 0.072 * s, 0.0},
        {0.213 - 0.213 * s, 0.715 - 0.715 * s, 0.072 + 0.928 * s, 0.0},
    }};
}

M sepia(double a) {
    const double b = 1 - a;
    return {{
        {0.393 + 0.607 * b, 0.769 - 0.769 * b, 0.189 - 0.189 * b, 0.0},
        {0.349 - 0.349 * b, 0.686 + 0.314 * b, 0.168 - 0.168 * b, 0.0},
        {0.272 - 0.272 * b, 0.534 - 0.534 * b, 0.131 + 0.869 * b, 0.0},
    }};
}

M contrast(double c) {
    return {{{c, 0.0, 0.0, 0.5 - 0.5 * c}, {0.0, c, 0.0, 0.5 - 0.5 * c}, {0.0, 0.0, c, 0.5 - 0.5 * c}}};
}

M bright(double b) { return {{{b, 0.0, 0.0, 0.0}, {0.0, b, 0.0, 0.0}, {0.0, 0.0, b, 0.0}}}; }

M tint(double r, double g, double b) { return {{{r, 0.0, 0.0, 0.0}, {0.0, g, 0.0, 0.0}, {0.0, 0.0, b, 0.0}}}; }

M lift(double o) {
    return {{{1 - o, 0.0, 0.0, o}, {0.0, 1 - o, 0.0, o}, {0.0, 0.0, 1 - o, o}}};
}

M mix(const ColorMatrix& m) {
    M out{};
    for (std::size_t r = 0; r < 3; ++r) {
        out[r] = {m[r][0], m[r][1], m[r][2], 0.0};
    }
    return out;
}

// _chain: la identidad y después cada pieza, en orden.
M chain(std::initializer_list<M> ops) {
    M m = kIdentityColor;
    for (const M& op : ops) {
        m = compose_color(op, m);
    }
    return m;
}

std::array<ColorFilterDef, 15> make_filters() {
    return {{
        {"bw", "Blanco y negro", "bn", chain({sat(0)})},
        {"noir", "Noir", "bn", chain({sat(0), contrast(1.5), bright(0.9)})},
        {"sepia", "Sepia", "retro", chain({sepia(1)})},
        {"vintage", "Vintage", "retro", chain({sepia(0.45), contrast(1.1), sat(0.8)})},
        {"faded", "Desvaído", "retro", chain({sat(0.8), contrast(0.9), lift(0.12)})},
        {"matte", "Mate", "retro", chain({contrast(0.95), lift(0.08)})},
        {"cinematic", "Cine", "cine", chain({contrast(1.15), sat(0.85), bright(0.92)})},
        {"teal_orange", "Naranja y turquesa", "cine",
         chain({mix({{{1.12, -0.08, -0.04}, {-0.04, 1.04, 0.0}, {-0.14, 0.06, 1.08}}}), contrast(1.08)})},
        {"night", "Noche", "cine", chain({tint(0.75, 0.85, 1.1), bright(0.85), sat(0.7)})},
        {"matrix", "Verde Matrix", "cine", chain({tint(0.85, 1.1, 0.85), contrast(1.1)})},
        {"contrast", "Alto contraste", "color", chain({contrast(1.35), sat(1.1)})},
        {"saturated", "Vivo", "color", chain({sat(1.55), contrast(1.08)})},
        {"warm", "Cálido", "color", chain({tint(1.08, 1, 0.88), sat(1.1)})},
        {"golden", "Hora dorada", "color", chain({tint(1.12, 1, 0.8), sat(1.2)})},
        {"cool", "Frío", "color", chain({tint(0.9, 1, 1.1), sat(0.9)})},
    }};
}

const ColorFilterDef* find_filter(std::string_view id) {
    for (const ColorFilterDef& f : color_filters()) {
        if (f.id == id) {
            return &f;
        }
    }
    return nullptr;
}

// min(1.0, max(0.0, n)) de Python: max(0.0, NaN) se queda con 0.0.
double clamp01(double n) {
    const double lo = n > 0.0 ? n : 0.0;
    return lo < 1.0 ? lo : 1.0;
}

// _clamp01: float(v) acotado; lo que no es un número, 1.
double clamp01(const Json& v) {
    const auto n = detail::py_float(v);
    return n ? clamp01(*n) : 1.0;
}

// Interpola la matriz con la identidad: I + a·(M − I).
M toward(const M& m, double a) {
    M out{};
    for (std::size_t r = 0; r < 3; ++r) {
        for (std::size_t c = 0; c < 4; ++c) {
            out[r][c] = kIdentityColor[r][c] + a * (m[r][c] - kIdentityColor[r][c]);
        }
    }
    return out;
}

// _fx de la capa de ajuste: float(e.get(k) or 0) acotado a su rango (lo que falla, 0). Como
// min(hi, max(lo, n)) de Python, NaN da lo.
double adjust_fx(const Json& effects, std::string_view key, double lo, double hi) {
    const auto it = effects.find(key);
    double n = 0.0;
    if (it != effects.end() && detail::py_truthy(*it)) {
        const auto v = detail::py_float(*it);
        if (!v) {
            return 0.0;
        }
        n = *v;
    }
    const double above = n > lo ? n : lo;
    return above < hi ? above : hi;
}

} // namespace

std::span<const ColorFilterDef> color_filters() {
    static const std::array<ColorFilterDef, 15> kFilters = make_filters();
    return kFilters;
}

std::vector<ClipFilter> clip_filters(const TimelineClip& clip) {
    std::vector<ClipFilter> out;
    if (clip.filters && clip.filters->is_array()) {
        for (const Json& f : *clip.filters) {
            if (!f.is_object()) {
                continue;
            }
            const std::string id =
                f.value("id", Json{}).is_string() ? f["id"].get<std::string>() : std::string{};
            if (id.empty() || find_filter(id) == nullptr) {
                continue;
            }
            out.push_back({id, f.contains("amount") ? clamp01(f["amount"]) : 1.0});
        }
        return out;
    }
    if (!clip.look.empty() && clip.look != "none" && find_filter(clip.look) != nullptr) {
        out.push_back({clip.look, 1.0});
    }
    return out;
}

AffineColorMatrix compose_color(const AffineColorMatrix& a, const AffineColorMatrix& b) {
    M out{};
    for (std::size_t r = 0; r < 3; ++r) {
        const auto& row = a[r];
        out[r] = {
            row[0] * b[0][0] + row[1] * b[1][0] + row[2] * b[2][0],
            row[0] * b[0][1] + row[1] * b[1][1] + row[2] * b[2][1],
            row[0] * b[0][2] + row[1] * b[1][2] + row[2] * b[2][2],
            row[0] * b[0][3] + row[1] * b[1][3] + row[2] * b[2][3] + row[3],
        };
    }
    return out;
}

AffineColorMatrix filter_matrix(std::string_view id, double k) {
    const ColorFilterDef* f = find_filter(id);
    return toward(f != nullptr ? f->matrix : kIdentityColor, clamp01(k));
}

std::optional<AffineColorMatrix> stack_matrix(const std::vector<ClipFilter>& filters) {
    std::optional<M> m;
    for (const ClipFilter& f : filters) {
        if (!(f.amount > 1e-4)) {
            continue;
        }
        const M fm = filter_matrix(f.id, f.amount);
        m = m ? compose_color(fm, *m) : fm;
    }
    return m;
}

std::optional<AffineColorMatrix> adjustment_matrix(const TimelineClip& clip) {
    static const Json kEmpty = Json::object();
    const Json& e = clip.effects && clip.effects->is_object() ? *clip.effects : kEmpty;
    M m = stack_matrix(clip_filters(clip)).value_or(kIdentityColor);
    const double b = adjust_fx(e, "brightness", -0.5, 0.5);
    const double c = adjust_fx(e, "contrast", -0.5, 0.5);
    const double s = adjust_fx(e, "saturation", -1.0, 1.0);
    if (b != 0) {
        m = compose_color(bright(1 + b), m);
    }
    if (c != 0) {
        m = compose_color(contrast(1 + c), m);
    }
    if (s != 0) {
        m = compose_color(sat(1 + s), m);
    }
    if (const auto adj = color_matrix(e)) {
        m = compose_color(mix(*adj), m);
    }
    const double k = clip.opacity ? clamp01(*clip.opacity) : 1.0;
    const M out = toward(m, k);
    bool identity = true;
    for (std::size_t r = 0; r < 3; ++r) {
        for (std::size_t col = 0; col < 4; ++col) {
            identity = identity && std::abs(out[r][col] - kIdentityColor[r][col]) < 1e-9;
        }
    }
    if (identity) {
        return std::nullopt;
    }
    return out;
}

std::string matrix_ffmpeg(const AffineColorMatrix& m) {
    static constexpr std::array<std::array<std::string_view, 4>, 3> kNames{{
        {"rr", "rg", "rb", "ra"},
        {"gr", "gg", "gb", "ga"},
        {"br", "bg", "bb", "ba"},
    }};
    const bool offs = std::ranges::any_of(m, [](const auto& row) { return std::abs(row[3]) > 1e-5; });
    std::string coefs;
    for (std::size_t r = 0; r < 3; ++r) {
        for (std::size_t c = 0; c < (offs ? 4U : 3U); ++c) {
            if (!coefs.empty()) {
                coefs += ':';
            }
            coefs += std::format("{}={:.5f}", kNames[r][c], m[r][c]);
        }
    }
    return std::string(offs ? "format=gbrap," : "") + "colorchannelmixer=" + coefs;
}

std::string filters_ffmpeg(const TimelineClip& clip) {
    const auto m = stack_matrix(clip_filters(clip));
    return m ? matrix_ffmpeg(*m) : std::string();
}

} // namespace gc::core
