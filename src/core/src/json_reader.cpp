#include "json_reader.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <locale>
#include <optional>
#include <sstream>
#include <utility>
#include <vector>

#include "py.hpp"

namespace gc::core::detail {

namespace {

using value_t = Json::value_t;

constexpr std::string_view kSpaces = " \t\n\r\f\v";

std::string_view trim(std::string_view s) {
    const auto first = s.find_first_not_of(kSpaces);
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = s.find_last_not_of(kSpaces);
    return s.substr(first, last - first + 1);
}

std::string lower(std::string_view s) {
    std::string out(s);
    ascii_lower_inplace(out);
    return out;
}

bool is_digit(char c) { return c >= '0' && c <= '9'; }

// Python acepta "_" solo ENTRE dígitos ("1_000"); se quitan si están bien puestos.
std::optional<std::string> strip_underscores(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '_') {
            out.push_back(s[i]);
            continue;
        }
        if (i == 0 || i + 1 >= s.size() || !is_digit(s[i - 1]) || !is_digit(s[i + 1])) {
            return std::nullopt;
        }
    }
    return out;
}

} // namespace

std::optional<double> parse_py_float(std::string_view text) {
    std::string_view s = trim(text);
    if (s.empty()) {
        return std::nullopt;
    }
    bool negative = false;
    std::string_view body = s;
    if (body.front() == '+' || body.front() == '-') {
        negative = body.front() == '-';
        body.remove_prefix(1);
    }
    const std::string word = lower(body);
    if (word == "inf" || word == "infinity") {
        return negative ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::infinity();
    }
    if (word == "nan") {
        return std::numeric_limits<double>::quiet_NaN();
    }
    const auto clean = strip_underscores(s);
    if (!clean || clean->empty()) {
        return std::nullopt;
    }
    // Solo dígitos, signo, punto y exponente: así no se cuelan formas que Python
    // rechaza y el flujo de C++ aceptaría (hexadecimales, etc.).
    const bool plain = std::ranges::all_of(*clean, [](char c) {
        return is_digit(c) || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-';
    });
    if (!plain || !std::ranges::any_of(*clean, is_digit)) {
        return std::nullopt;
    }
    std::istringstream in(*clean);
    in.imbue(std::locale::classic());
    double value = 0.0;
    in >> value;
    if (in.fail()) {
        return std::nullopt;
    }
    in.peek();
    if (!in.eof()) {
        return std::nullopt;
    }
    return value;
}

std::optional<std::int64_t> parse_py_int(std::string_view text, bool accept_real_text) {
    std::string_view s = trim(text);
    if (s.empty()) {
        return std::nullopt;
    }
    const auto clean = strip_underscores(s);
    if (!clean) {
        return std::nullopt;
    }
    std::string_view digits = *clean;
    bool negative = false;
    if (!digits.empty() && (digits.front() == '+' || digits.front() == '-')) {
        negative = digits.front() == '-';
        digits.remove_prefix(1);
    }
    if (!digits.empty() && std::ranges::all_of(digits, is_digit)) {
        // Acumular en negativo para admitir INT64_MIN sin desbordar.
        std::int64_t acc = 0;
        for (const char c : digits) {
            const int d = c - '0';
            if (acc < (std::numeric_limits<std::int64_t>::min() + d) / 10) {
                return std::nullopt;
            }
            acc = acc * 10 - d;
        }
        if (!negative) {
            if (acc == std::numeric_limits<std::int64_t>::min()) {
                return std::nullopt;
            }
            acc = -acc;
        }
        return acc;
    }
    if (!accept_real_text) {
        return std::nullopt;
    }
    const auto real = parse_py_float(s);
    if (real && std::isfinite(*real) && std::trunc(*real) == *real &&
        *real >= -9.2233720368547758e18 && *real < 9.2233720368547758e18) {
        return static_cast<std::int64_t>(*real);
    }
    return std::nullopt;
}

namespace {

std::string quoted(const Json& j) {
    std::string s = j.dump();
    if (s.size() > 60) {
        s = s.substr(0, 57) + "...";
    }
    return s;
}

} // namespace

std::string PathNode::str() const {
    std::vector<const PathNode*> chain;
    for (const PathNode* n = this; n != nullptr; n = n->parent) {
        chain.push_back(n);
    }
    std::string out;
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        const PathNode& n = **it;
        if (n.is_index) {
            out += '[';
            out += std::to_string(n.index);
            out += ']';
        } else if (!n.key.empty()) {
            if (!out.empty()) {
                out += '.';
            }
            out += n.key;
        }
    }
    return out;
}

void fail_at(const PathNode& path, std::string message) {
    throw ValidationFailure{path.str(), std::move(message)};
}

const char* json_type_name(const Json& j) {
    switch (j.type()) {
    case value_t::null: return "null";
    case value_t::boolean: return "bool";
    case value_t::number_integer:
    case value_t::number_unsigned: return "int";
    case value_t::number_float: return "float";
    case value_t::string: return "str";
    case value_t::array: return "list";
    case value_t::object: return "dict";
    case value_t::binary: return "binary";
    case value_t::discarded: return "discarded";
    }
    return "?";
}

double to_float(const Json& j, const PathNode& path) {
    switch (j.type()) {
    case value_t::number_float: return j.get<double>();
    case value_t::number_integer: return static_cast<double>(j.get<std::int64_t>());
    case value_t::number_unsigned: return static_cast<double>(j.get<std::uint64_t>());
    case value_t::boolean: return j.get<bool>() ? 1.0 : 0.0;
    case value_t::string:
        if (const auto v = parse_py_float(j.get_ref<const std::string&>())) {
            return *v;
        }
        fail_at(path, "se esperaba un número y llegó el texto " + quoted(j));
    default: fail_at(path, std::string("se esperaba un número y llegó ") + json_type_name(j));
    }
}

std::int64_t to_int(const Json& j, const PathNode& path) {
    switch (j.type()) {
    case value_t::number_integer: return j.get<std::int64_t>();
    case value_t::number_unsigned: {
        const auto v = j.get<std::uint64_t>();
        if (v > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            fail_at(path, "entero demasiado grande: " + quoted(j));
        }
        return static_cast<std::int64_t>(v);
    }
    case value_t::number_float: {
        const double d = j.get<double>();
        if (std::isfinite(d) && std::trunc(d) == d && d >= -9.2233720368547758e18 && d < 9.2233720368547758e18) {
            return static_cast<std::int64_t>(d);
        }
        fail_at(path, "se esperaba un entero y llegó " + quoted(j));
    }
    case value_t::boolean: return j.get<bool>() ? 1 : 0;
    case value_t::string:
        if (const auto v = parse_py_int(j.get_ref<const std::string&>(), true)) {
            return *v;
        }
        fail_at(path, "se esperaba un entero y llegó el texto " + quoted(j));
    default: fail_at(path, std::string("se esperaba un entero y llegó ") + json_type_name(j));
    }
}

bool to_bool(const Json& j, const PathNode& path) {
    switch (j.type()) {
    case value_t::boolean: return j.get<bool>();
    case value_t::number_integer:
    case value_t::number_unsigned: {
        const auto v = j.get<std::int64_t>();
        if (v == 0 || v == 1) {
            return v == 1;
        }
        break;
    }
    case value_t::number_float: {
        const double d = j.get<double>();
        if (d == 0.0 || d == 1.0) {
            return d == 1.0;
        }
        break;
    }
    case value_t::string: {
        const std::string s = lower(j.get_ref<const std::string&>());
        if (s == "1" || s == "on" || s == "t" || s == "true" || s == "y" || s == "yes") {
            return true;
        }
        if (s == "0" || s == "off" || s == "f" || s == "false" || s == "n" || s == "no") {
            return false;
        }
        break;
    }
    default: break;
    }
    fail_at(path, "se esperaba un booleano y llegó " + quoted(j));
}

std::string to_str(const Json& j, const PathNode& path) {
    if (!j.is_string()) {
        fail_at(path, std::string("se esperaba un texto y llegó ") + json_type_name(j));
    }
    return j.get<std::string>();
}

bool py_truthy(const Json& j) {
    switch (j.type()) {
    case value_t::null: return false;
    case value_t::boolean: return j.get<bool>();
    case value_t::number_integer: return j.get<std::int64_t>() != 0;
    case value_t::number_unsigned: return j.get<std::uint64_t>() != 0;
    case value_t::number_float: return j.get<double>() != 0.0;
    case value_t::string: return !j.get_ref<const std::string&>().empty();
    case value_t::array:
    case value_t::object: return !j.empty();
    case value_t::binary: return !j.get_binary().empty();
    case value_t::discarded: return false;
    }
    return false;
}

std::string py_str(const Json& j) {
    switch (j.type()) {
    case value_t::null: return "None";
    case value_t::boolean: return j.get<bool>() ? "True" : "False";
    case value_t::number_integer: return std::to_string(j.get<std::int64_t>());
    case value_t::number_unsigned: return std::to_string(j.get<std::uint64_t>());
    case value_t::string: return j.get<std::string>();
    default: return py_repr(j); // reales, listas y dicts: como repr() de Python
    }
}

} // namespace gc::core::detail
