#include "gc/core/shapes.hpp"

#include <algorithm>
#include <cmath>

#include "json_reader.hpp"
#include "py.hpp"

namespace gc::core {

using detail::py_clamp;
using detail::py_float;
using detail::py_round;
using detail::py_truthy;

namespace {

const Json& field(const Json& obj, std::string_view key) {
    static const Json kNull;
    if (!obj.is_object()) {
        return kNull;
    }
    const auto it = obj.find(key);
    return it == obj.end() ? kNull : *it;
}

// dict.get(key, default)
const Json& get_or(const Json& obj, std::string_view key, const Json& fallback) {
    if (!obj.is_object()) {
        return fallback;
    }
    const auto it = obj.find(key);
    return it == obj.end() ? fallback : *it;
}

// int(texto, 16) de Python sobre lo que va tras "#": signo opcional y "_" entre dígitos.
bool py_hex_ok(std::string_view s) {
    if (!s.empty() && (s.front() == '+' || s.front() == '-')) {
        s.remove_prefix(1);
    }
    if (s.empty()) {
        return false;
    }
    auto hex = [](char c) { return std::isxdigit(static_cast<unsigned char>(c)) != 0; };
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '_') {
            if (i == 0 || i + 1 >= s.size() || !hex(s[i - 1]) || !hex(s[i + 1])) {
                return false;
            }
        } else if (!hex(s[i])) {
            return false;
        }
    }
    return true;
}

// shapes._hex: "#rrggbb" (7 caracteres) o el defecto.
Json hex_or(const Json& v, const Json& fallback) {
    const std::string s = py_truthy(v) ? detail::py_strip(detail::py_str(v)) : std::string();
    if (detail::py_len(s) == 7 && s.starts_with("#") && py_hex_ok(std::string_view(s).substr(1))) {
        return s;
    }
    return fallback;
}

// float(x) de Python o error de validación con el nombre del campo.
Result<double> to_float(const Json& v, std::string_view key) {
    if (const auto f = py_float(v)) {
        return *f;
    }
    return fail(ErrorCode::InvalidArgument, "figura: '" + std::string(key) + "' no es un número");
}

} // namespace

Json default_shape(const Json& type) {
    const Json t = py_truthy(type) ? type : Json("rect");
    Json base = {{"type", t},          {"x", 0.5},       {"y", 0.42},           {"w", 0.38},
                 {"h", 0.16},          {"rotation", 0},  {"fill", "#e53935"},   {"stroke", "#ffffff"},
                 {"strokeWidth", 4},   {"opacity", 1},   {"cornerRadius", 0.12}, {"sides", t == "star" ? 5 : 6},
                 {"arrowHead", "filled"}};
    auto set = [&](std::initializer_list<std::pair<const char*, Json>> kv) {
        for (const auto& [k, v] : kv) {
            base[k] = v;
        }
    };
    if (t == "square" || t == "circle") {
        set({{"w", 0.28}, {"h", 0.16}});
    }
    if (t == "ellipse") {
        set({{"w", 0.4}, {"h", 0.18}});
    }
    if (t == "line") {
        set({{"w", 0.42}, {"h", 0.06}, {"fill", "none"}, {"strokeWidth", 6}});
    }
    if (t == "arrow" || t == "arrow_double") {
        set({{"w", 0.46}, {"h", 0.14}});
    }
    if (t == "arrow_curve") {
        set({{"w", 0.42}, {"h", 0.28}, {"y", 0.38}});
    }
    if (t == "triangle") {
        set({{"w", 0.3}, {"h", 0.18}});
    }
    if (t == "star" || t == "polygon") {
        set({{"w", 0.3}, {"h", 0.17}});
    }
    if (t == "speech") {
        set({{"w", 0.42}, {"h", 0.22}, {"y", 0.4}});
    }
    if (t == "check" || t == "x") {
        set({{"w", 0.2}, {"h", 0.12}, {"fill", "none"}, {"strokeWidth", 8}, {"stroke", "#22c55e"}});
    }
    if (t == "x") {
        set({{"stroke", "#ef4444"}});
    }
    if (t == "marker") {
        set({{"w", 0.18}, {"h", 0.2}, {"y", 0.4}});
    }
    if (t == "heart") {
        set({{"w", 0.26}, {"h", 0.16}, {"fill", "#e11d48"}});
    }
    if (t == "path") {
        set({{"w", 0.42},
             {"h", 0.2},
             {"fill", "none"},
             {"strokeWidth", 6},
             {"points", Json::array({Json::array({0, 100}), Json::array({50, 0}), Json::array({100, 100})})},
             {"closed", false},
             {"smooth", true}});
    }
    if (t == "letterbox") {
        set({{"x", 0.5},
             {"y", 0.5},
             {"w", 1},
             {"h", 1},
             {"fill", "#000000"},
             {"stroke", "#000000"},
             {"strokeWidth", 0},
             {"bar", 0.12}});
    }
    return base;
}

std::vector<std::pair<double, double>> path_anchors(const Json& points) {
    std::vector<std::pair<double, double>> out;
    if (!points.is_array()) {
        return out;
    }
    for (const Json& p : points) {
        // float(p[0]), float(p[1]): listas (y textos de 2+ caracteres numéricos)
        const auto items = detail::py_iter(p);
        if (!items || items->size() < 2 || p.is_object()) {
            continue;
        }
        const auto x = py_float((*items)[0]);
        const auto y = py_float((*items)[1]);
        if (!x || !y || !std::isfinite(*x) || !std::isfinite(*y)) {
            continue;
        }
        out.emplace_back(*x, *y);
    }
    return out;
}

Result<Json> normalize_shape(const Json& raw_in) {
    const Json raw = py_truthy(raw_in) ? raw_in : Json::object();
    if (!raw.is_object()) {
        return fail(ErrorCode::InvalidArgument, "la figura debe ser un objeto");
    }
    const Json t = py_truthy(field(raw, "type")) ? field(raw, "type") : Json("rect");
    const Json d = default_shape(t);
    const Json fill = field(raw, "fill") == "none" ? Json("none") : hex_or(field(raw, "fill"), d["fill"]);
    const Json& head_raw = field(raw, "arrowHead");
    const Json head = head_raw == "line" || head_raw == "none" || head_raw == "filled" ? head_raw : Json("filled");

    // {**d, **raw, ...}: los de la figura por defecto, luego los del usuario.
    Json out = d;
    for (const auto& [key, value] : raw.items()) {
        out[key] = value;
    }
    auto num = [&](const char* key) -> Result<double> { return to_float(get_or(raw, key, d[key]), key); };
#define GC_NUM(var, key)                                                                                              \
    auto var##_r = num(key);                                                                                          \
    if (!var##_r) {                                                                                                   \
        return std::unexpected(var##_r.error());                                                                      \
    }                                                                                                                 \
    const double var = *var##_r

    GC_NUM(x, "x");
    GC_NUM(y, "y");
    GC_NUM(w, "w");
    GC_NUM(h, "h");
    GC_NUM(stroke_width, "strokeWidth");
    GC_NUM(opacity, "opacity");
    GC_NUM(corner, "cornerRadius");
#undef GC_NUM
    const Json& rot_raw = get_or(raw, "rotation", d["rotation"]);
    auto rotation = to_float(py_truthy(rot_raw) ? rot_raw : Json(0), "rotation");
    if (!rotation) {
        return std::unexpected(rotation.error());
    }
    const Json& sides_raw = get_or(raw, "sides", d["sides"]);
    auto sides = to_float(py_truthy(sides_raw) ? sides_raw : d["sides"], "sides");
    if (!sides) {
        return std::unexpected(sides.error());
    }
    if (!std::isfinite(*sides)) {
        return fail(ErrorCode::InvalidArgument, "figura: 'sides' no es finito");
    }
    const Json& draw_raw = field(raw, "draw");
    auto draw = to_float(draw_raw.is_null() ? Json(1) : get_or(raw, "draw", Json(1)), "draw");
    if (!draw) {
        return std::unexpected(draw.error());
    }

    out["type"] = t;
    out["x"] = py_clamp(x, 0, 1);
    out["y"] = py_clamp(y, 0, 1);
    out["w"] = py_clamp(w, 0.04, 1);
    out["h"] = py_clamp(h, 0.03, 1);
    out["rotation"] = *rotation;
    out["fill"] = fill;
    out["stroke"] = hex_or(field(raw, "stroke"), d["stroke"]);
    out["strokeWidth"] = py_clamp(stroke_width, 0, 24);
    out["opacity"] = py_clamp(opacity, 0, 1);
    out["cornerRadius"] = py_clamp(corner, 0, 0.5);
    // int(_clamp(round(float(sides)), 3, 12))
    out["sides"] = static_cast<std::int64_t>(std::min(12.0, std::max(3.0, py_round(*sides, 0))));
    out["arrowHead"] = head;
    const Json& dash = field(raw, "dash");
    out["dash"] = dash == "solid" || dash == "dash" || dash == "dot" ? dash : Json("solid");
    out["draw"] = py_clamp(*draw, 0, 1);
    if (t == "letterbox") {
        const Json& bar_raw = get_or(raw, "bar", field(d, "bar"));
        auto bar = to_float(bar_raw, "bar");
        if (!bar) {
            return std::unexpected(bar.error());
        }
        // float(x) or 0: 0.0 se convierte en el entero 0
        out["bar"] = py_clamp(*bar != 0.0 ? Json(*bar) : Json(0), 0, 0.5);
    }
    if (t == "path") {
        Json points = Json::array();
        for (const auto& [px, py] : path_anchors(get_or(raw, "points", field(d, "points")))) {
            points.push_back(Json::array({px, py}));
        }
        out["points"] = std::move(points);
        out["closed"] = py_truthy(get_or(raw, "closed", field(d, "closed")));
        out["smooth"] = !(field(raw, "smooth").is_boolean() && !field(raw, "smooth").get<bool>());
    }
    return out;
}

Result<Json> path_shape(const Json& frame_points, const Json& opts) {
    const auto pts = path_anchors(frame_points);
    if (pts.size() < 2) {
        return fail(ErrorCode::InvalidArgument, "un trazado necesita al menos 2 puntos [x, y] en 0–1");
    }
    double x0 = pts[0].first;
    double x1 = x0;
    double y0 = pts[0].second;
    double y1 = y0;
    for (const auto& [px, py] : pts) {
        x0 = std::min(x0, px);
        x1 = std::max(x1, px);
        y0 = std::min(y0, py);
        y1 = std::max(y1, py);
    }
    const double w = std::min(1.0, std::max(0.04, x1 - x0));
    const double h = std::min(1.0, std::max(0.03, y1 - y0));
    const double cx = (x0 + x1) / 2;
    const double cy = (y0 + y1) / 2;
    Json out = default_shape("path");
    if (opts.is_object()) {
        for (const auto& [key, value] : opts.items()) {
            out[key] = value;
        }
    }
    out["type"] = "path";
    out["x"] = py_round(cx, 4);
    out["y"] = py_round(cy, 4);
    out["w"] = py_round(w, 4);
    out["h"] = py_round(h, 4);
    out["rotation"] = 0;
    Json points = Json::array();
    for (const auto& [px, py] : pts) {
        points.push_back(Json::array({py_round(50 + (px - cx) / w * 100, 3), py_round(50 + (py - cy) / h * 100, 3)}));
    }
    out["points"] = std::move(points);
    return out;
}

const Json* cinema_ratio(std::string_view name) {
    static const Json kRatios = {{"2.39", 2.39}, {"2", 2.0}, {"1.85", 1.85}, {"16:9", 16.0 / 9.0}};
    const auto it = kRatios.find(name);
    return it == kRatios.end() ? nullptr : &*it;
}

Json cinema_bar(double out_aspect, double ratio) {
    const Json clamped = py_clamp((1 - out_aspect / ratio) / 2, 0, kCinemaBarMax);
    // round(x, 4) de un int devuelve int
    return clamped.is_number_integer() ? clamped : Json(py_round(clamped.get<double>(), 4));
}

} // namespace gc::core
