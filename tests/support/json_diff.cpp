#include "json_diff.hpp"

#include <algorithm>
#include <cmath>
#include <string_view>

namespace gc::test {

namespace {

using value_t = Json::value_t;

// Entero con o sin signo cuentan como la misma categoría (en Python ambos son int).
int category(const Json& j) {
    switch (j.type()) {
    case value_t::number_integer:
    case value_t::number_unsigned: return static_cast<int>(value_t::number_integer);
    default: return static_cast<int>(j.type());
    }
}

std::string brief(const Json& j) {
    std::string s = j.dump();
    return s.size() > 80 ? s.substr(0, 77) + "..." : s;
}

std::optional<std::string> diff(const Json& e, const Json& a, const std::string& path, bool check_order,
                                double tol) {
    if (category(e) != category(a)) {
        return path + ": tipo distinto, se esperaba " + brief(e) + " y llegó " + brief(a);
    }
    switch (e.type()) {
    case value_t::object: {
        if (e.size() != a.size()) {
            for (const auto& [key, value] : e.items()) {
                if (!a.contains(key)) {
                    return path + "." + key + ": falta en el resultado";
                }
            }
            for (const auto& [key, value] : a.items()) {
                if (!e.contains(key)) {
                    return path + "." + key + ": sobra en el resultado";
                }
            }
        }
        auto ia = a.begin();
        for (auto ie = e.begin(); ie != e.end(); ++ie, ++ia) {
            if (check_order && ia.key() != ie.key()) {
                return path + ": orden de claves distinto, se esperaba \"" + ie.key() + "\" y llegó \"" +
                       ia.key() + "\"";
            }
            const auto other = a.find(ie.key());
            if (other == a.end()) {
                return path + "." + ie.key() + ": falta en el resultado";
            }
            if (auto d = diff(*ie, *other, path + "." + ie.key(), check_order, tol)) {
                return d;
            }
        }
        return std::nullopt;
    }
    case value_t::array: {
        if (e.size() != a.size()) {
            return path + ": longitud distinta, " + std::to_string(e.size()) + " frente a " +
                   std::to_string(a.size());
        }
        for (std::size_t i = 0; i < e.size(); ++i) {
            if (auto d = diff(e[i], a[i], path + "[" + std::to_string(i) + "]", check_order, tol)) {
                return d;
            }
        }
        return std::nullopt;
    }
    case value_t::number_float: {
        const double x = e.get<double>();
        const double y = a.get<double>();
        // Bit a bit: 0.0 y -0.0 son distintos (Python los escribe distinto).
        if ((x == y && std::signbit(x) == std::signbit(y)) || (std::isnan(x) && std::isnan(y))) {
            return std::nullopt;
        }
        if (tol > 0 && std::abs(x - y) <= tol * std::max({1.0, std::abs(x), std::abs(y)})) {
            return std::nullopt;
        }
        return path + ": se esperaba " + brief(e) + " y llegó " + brief(a);
    }
    case value_t::number_integer:
    case value_t::number_unsigned:
        if (e.dump() == a.dump()) {
            return std::nullopt;
        }
        return path + ": se esperaba " + brief(e) + " y llegó " + brief(a);
    default:
        if (e == a) {
            return std::nullopt;
        }
        return path + ": se esperaba " + brief(e) + " y llegó " + brief(a);
    }
}

} // namespace

std::optional<std::string> json_diff(const Json& expected, const Json& actual, bool check_order, double tolerance) {
    return diff(expected, actual, "$", check_order, tolerance);
}

} // namespace gc::test
