#include "py.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <limits>
#include <locale>
#include <sstream>
#include <system_error>

#include "json_reader.hpp"

namespace gc::core::detail {

namespace {

constexpr std::array<double, 16> kPow10{1e0, 1e1, 1e2,  1e3,  1e4,  1e5,  1e6,  1e7,
                                        1e8, 1e9, 1e10, 1e11, 1e12, 1e13, 1e14, 1e15};

// Decodifica un código UTF-8 en `s[i]` y avanza `i`. Bytes inválidos cuentan como uno.
char32_t next_code_point(std::string_view s, std::size_t& i) {
    const auto b0 = static_cast<unsigned char>(s[i]);
    std::size_t len = 1;
    char32_t cp = b0;
    if (b0 >= 0xF0) {
        len = 4;
        cp = b0 & 0x07U;
    } else if (b0 >= 0xE0) {
        len = 3;
        cp = b0 & 0x0FU;
    } else if (b0 >= 0xC0) {
        len = 2;
        cp = b0 & 0x1FU;
    }
    if (i + len > s.size()) {
        len = 1;
        cp = b0;
    } else {
        for (std::size_t k = 1; k < len; ++k) {
            cp = (cp << 6) | (static_cast<unsigned char>(s[i + k]) & 0x3FU);
        }
    }
    i += len;
    return cp;
}

// str.isspace() de Python: tipos bidireccionales WS/B/S y categoría Zs.
bool py_isspace(char32_t c) {
    return (c >= 0x09 && c <= 0x0D) || (c >= 0x1C && c <= 0x20) || c == 0x85 || c == 0xA0 || c == 0x1680 ||
           (c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F ||
           c == 0x3000;
}

std::optional<double> parse_decimal(const std::string& text) {
    std::istringstream in(text);
    in.imbue(std::locale::classic());
    double v = 0.0;
    in >> v;
    if (in.fail()) {
        return std::nullopt;
    }
    return v;
}

} // namespace

double py_round(double x, int ndigits) {
    if (!std::isfinite(x) || x == 0.0) {
        return x;
    }
    // to_chars con precisión fija da el decimal correctamente redondeado (empates
    // al par sobre el valor exacto), igual que el dtoa que usa Python.
    std::array<char, 400> buf{};
    const auto res = std::to_chars(buf.data(), buf.data() + buf.size(), x, std::chars_format::fixed, ndigits);
    if (res.ec != std::errc{}) {
        return x;
    }
    std::string_view s(buf.data(), static_cast<std::size_t>(res.ptr - buf.data()));
    const bool negative = s.front() == '-';
    if (negative) {
        s.remove_prefix(1);
    }
    std::string digits;
    digits.reserve(s.size());
    for (const char c : s) {
        if (c != '.') {
            digits.push_back(c);
        }
    }
    if (ndigits >= 0 && ndigits < static_cast<int>(kPow10.size()) && digits.size() <= 15) {
        // Entero exacto (< 2^53) entre una potencia de 10 exacta: la división IEEE
        // da el double más cercano al decimal, igual que strtod.
        std::uint64_t n = 0;
        for (const char c : digits) {
            n = n * 10 + static_cast<std::uint64_t>(c - '0');
        }
        const double v = static_cast<double>(n) / kPow10[static_cast<std::size_t>(ndigits)];
        return negative ? -v : v;
    }
    const auto v = parse_decimal(std::string(s));
    if (!v) {
        return x;
    }
    return negative ? -*v : *v;
}

std::string py_repr(double x) {
    if (std::isnan(x)) {
        return "nan";
    }
    if (std::isinf(x)) {
        return x > 0 ? "inf" : "-inf";
    }
    // Forma más corta que reproduce el valor, como repr(). Solo difiere de Python
    // entre 1e15 y 1e16 (Python no pasa a notación científica hasta 1e16).
    return Json(x).dump();
}

std::string py_strip(std::string_view utf8) {
    std::size_t i = 0;
    std::size_t first = utf8.size();
    std::size_t last = 0;
    while (i < utf8.size()) {
        const std::size_t at = i;
        const char32_t cp = next_code_point(utf8, i);
        if (!py_isspace(cp)) {
            if (first == utf8.size()) {
                first = at;
            }
            last = i;
        }
    }
    if (first == utf8.size()) {
        return {};
    }
    return std::string(utf8.substr(first, last - first));
}

std::string py_slice(std::string_view utf8, std::size_t n) {
    std::size_t i = 0;
    for (std::size_t count = 0; count < n && i < utf8.size(); ++count) {
        (void)next_code_point(utf8, i);
    }
    return std::string(utf8.substr(0, i));
}

std::optional<double> py_float(const Json& j) {
    if (j.is_string()) {
        return parse_py_float(j.get_ref<const std::string&>());
    }
    return py_number(j);
}

std::optional<std::int64_t> py_int(const Json& j) {
    switch (j.type()) {
    case Json::value_t::number_integer: return j.get<std::int64_t>();
    case Json::value_t::number_unsigned: {
        const auto v = j.get<std::uint64_t>();
        if (v > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            return std::nullopt;
        }
        return static_cast<std::int64_t>(v);
    }
    case Json::value_t::boolean: return j.get<bool>() ? 1 : 0;
    case Json::value_t::number_float: {
        const double d = std::trunc(j.get<double>());
        if (!std::isfinite(d) || d < -9.2233720368547758e18 || d >= 9.2233720368547758e18) {
            return std::nullopt;
        }
        return static_cast<std::int64_t>(d);
    }
    case Json::value_t::string: return parse_py_int(j.get_ref<const std::string&>(), false);
    default: return std::nullopt;
    }
}

double py_num(const Json& j, double fallback) {
    const auto v = py_float(j);
    return v && !std::isnan(*v) ? *v : fallback;
}

std::optional<std::vector<Json>> py_iter(const Json& j) {
    std::vector<Json> out;
    if (j.is_array()) {
        out.assign(j.begin(), j.end());
        return out;
    }
    if (j.is_object()) {
        for (const auto& [key, value] : j.items()) {
            out.emplace_back(key);
        }
        return out;
    }
    if (j.is_string()) {
        const std::string& s = j.get_ref<const std::string&>();
        std::size_t i = 0;
        while (i < s.size()) {
            const std::size_t at = i;
            (void)next_code_point(s, i);
            out.emplace_back(s.substr(at, i - at));
        }
        return out;
    }
    return std::nullopt;
}

std::string py_rstrip(std::string_view utf8) {
    std::size_t i = 0;
    std::size_t last = 0;
    while (i < utf8.size()) {
        const char32_t cp = next_code_point(utf8, i);
        if (!py_isspace(cp)) {
            last = i;
        }
    }
    return std::string(utf8.substr(0, last));
}

std::size_t py_len(std::string_view utf8) {
    std::size_t i = 0;
    std::size_t n = 0;
    while (i < utf8.size()) {
        (void)next_code_point(utf8, i);
        ++n;
    }
    return n;
}

std::vector<std::string> py_split(std::string_view utf8) {
    std::vector<std::string> out;
    std::size_t i = 0;
    std::size_t word_start = std::string_view::npos;
    while (i < utf8.size()) {
        const std::size_t at = i;
        const char32_t cp = next_code_point(utf8, i);
        if (py_isspace(cp)) {
            if (word_start != std::string_view::npos) {
                out.emplace_back(utf8.substr(word_start, at - word_start));
                word_start = std::string_view::npos;
            }
        } else if (word_start == std::string_view::npos) {
            word_start = at;
        }
    }
    if (word_start != std::string_view::npos) {
        out.emplace_back(utf8.substr(word_start));
    }
    return out;
}

double py_num_finite(const Json& j, double fallback) {
    const auto v = py_float(j);
    return v && std::isfinite(*v) ? *v : fallback;
}

Json py_clamp(const Json& v, const Json& lo, const Json& hi) {
    const double x = *py_number(v);
    // max(lo, v): gana lo salvo que v > lo
    const Json& m = x > *py_number(lo) ? v : lo;
    // min(hi, m): gana hi salvo que m < hi
    return *py_number(m) < *py_number(hi) ? m : hi;
}

std::optional<double> py_number(const Json& j) {
    switch (j.type()) {
    case Json::value_t::number_integer: return static_cast<double>(j.get<std::int64_t>());
    case Json::value_t::number_unsigned: return static_cast<double>(j.get<std::uint64_t>());
    case Json::value_t::number_float: return j.get<double>();
    case Json::value_t::boolean: return j.get<bool>() ? 1.0 : 0.0;
    default: return std::nullopt;
    }
}

double py_mod(double x, double y) {
    // float_rem de CPython
    double mod = std::fmod(x, y);
    if (mod != 0.0) {
        if ((y < 0) != (mod < 0)) {
            mod += y;
        }
    } else {
        mod = std::copysign(0.0, y);
    }
    return mod;
}

namespace {

// Aproximación de str.isprintable() de Python fuera de ASCII: lo que repr() escapa.
bool py_printable(char32_t c) {
    if (c < 0x20 || c == 0x7F || (c >= 0x80 && c <= 0xA0) || c == 0xAD) {
        return false;
    }
    if (c == 0x061C || c == 0x1680 || c == 0x180E || c == 0x3000 || c == 0xFEFF) {
        return false;
    }
    if ((c >= 0x2000 && c <= 0x200F) || (c >= 0x2028 && c <= 0x202F) || (c >= 0x205F && c <= 0x206F)) {
        return false;
    }
    if ((c >= 0xD800 && c <= 0xF8FF) || (c >= 0xFFF9 && c <= 0xFFFB) || c == 0xFFFE || c == 0xFFFF) {
        return false; // sustitutos, uso privado, anclas de anotación y no caracteres
    }
    if ((c >= 0xE0000 && c <= 0xE007F) || c >= 0xF0000) {
        return false; // etiquetas y uso privado de los planos 15 y 16
    }
    return true;
}

std::string hex_escape(char32_t c) {
    char buf[12];
    if (c <= 0xFF) {
        std::snprintf(buf, sizeof buf, "\\x%02x", static_cast<unsigned>(c));
    } else if (c <= 0xFFFF) {
        std::snprintf(buf, sizeof buf, "\\u%04x", static_cast<unsigned>(c));
    } else {
        std::snprintf(buf, sizeof buf, "\\U%08x", static_cast<unsigned>(c));
    }
    return buf;
}

std::string repr_text(std::string_view utf8) {
    const bool single = utf8.find('\'') != std::string_view::npos;
    const bool dbl = utf8.find('"') != std::string_view::npos;
    const char quote = single && !dbl ? '"' : '\'';
    std::string out(1, quote);
    std::size_t i = 0;
    while (i < utf8.size()) {
        const std::size_t from = i;
        const char32_t c = next_code_point(utf8, i);
        if (c == static_cast<char32_t>(quote) || c == U'\\') {
            out += '\\';
            out += static_cast<char>(c);
        } else if (c == U'\t') {
            out += "\\t";
        } else if (c == U'\n') {
            out += "\\n";
        } else if (c == U'\r') {
            out += "\\r";
        } else if (c < 0x7F && c >= 0x20) {
            out += static_cast<char>(c);
        } else if (py_printable(c)) {
            out.append(utf8.substr(from, i - from));
        } else {
            out += hex_escape(c);
        }
    }
    out += quote;
    return out;
}

} // namespace

std::string py_repr(const Json& j) {
    switch (j.type()) {
    case Json::value_t::null: return "None";
    case Json::value_t::boolean: return j.get<bool>() ? "True" : "False";
    case Json::value_t::number_integer: return std::to_string(j.get<std::int64_t>());
    case Json::value_t::number_unsigned: return std::to_string(j.get<std::uint64_t>());
    case Json::value_t::number_float: return py_repr(j.get<double>());
    case Json::value_t::string: return repr_text(j.get_ref<const std::string&>());
    case Json::value_t::array: {
        std::string out = "[";
        for (std::size_t k = 0; k < j.size(); ++k) {
            out += (k == 0 ? "" : ", ") + py_repr(j[k]);
        }
        return out + "]";
    }
    case Json::value_t::object: {
        std::string out = "{";
        bool first = true;
        for (const auto& [key, value] : j.items()) {
            out += (first ? "" : ", ") + repr_text(key) + ": " + py_repr(value);
            first = false;
        }
        return out + "}";
    }
    default: return j.dump();
    }
}

std::string py_path_stem(std::string_view text) {
    std::string p(text);
    std::ranges::replace(p, '/', '\\');
    std::string_view rest = p;
    if (rest.starts_with("\\\\")) { // UNC: \\servidor\recurso
        const auto server = rest.find('\\', 2);
        const auto share = server == std::string_view::npos ? server : rest.find('\\', server + 1);
        rest = share == std::string_view::npos ? std::string_view() : rest.substr(share);
    } else if (rest.size() >= 2 && rest[1] == ':') { // unidad: cualquier carácter + «:»
        rest.remove_prefix(2);
    }
    std::string_view name;
    std::size_t i = 0;
    while (i <= rest.size()) {
        const auto j = std::min(rest.find('\\', i), rest.size());
        const auto part = rest.substr(i, j - i);
        if (!part.empty() && part != ".") {
            name = part;
        }
        i = j + 1;
    }
    const auto dot = name.rfind('.');
    if (dot != std::string_view::npos) {
        const auto stem = name.substr(0, dot);
        if (stem.find_first_not_of('.') != std::string_view::npos) {
            return std::string(stem);
        }
    }
    return std::string(name);
}

char32_t utf8_next(std::string_view utf8, std::size_t& i) { return next_code_point(utf8, i); }

bool py_word_char(char32_t c) {
    if (c < 0x80) {
        return (c >= U'0' && c <= U'9') || (c >= U'a' && c <= U'z') || (c >= U'A' && c <= U'Z') || c == U'_';
    }
    if (c <= 0xFF) { // Latin-1: letras, ª º µ, ² ³ ¹ y ¼ ½ ¾
        return c == 0xAA || c == 0xB2 || c == 0xB3 || c == 0xB5 || c == 0xB9 || c == 0xBA ||
               (c >= 0xBC && c <= 0xBE) || (c >= 0xC0 && c != 0xD7 && c != 0xF7);
    }
    struct Range {
        char32_t lo;
        char32_t hi;
    };
    // Bloques sin letras ni números: modificadores-símbolo, marcas combinantes, signos
    // griegos y cirílicos, puntuación general, monedas, flechas, matemáticas, formas,
    // dingbats, puntuación CJK, uso privado, especiales y emoji.
    static constexpr Range kNonWord[] = {
        {0x2C2, 0x2C5},     {0x2D2, 0x2DF},   {0x2E5, 0x2EB},   {0x2ED, 0x2ED},   {0x2EF, 0x36F},
        {0x375, 0x375},     {0x37E, 0x37E},   {0x384, 0x385},   {0x387, 0x387},   {0x3F6, 0x3F6},
        {0x482, 0x489},     {0x2000, 0x206F}, {0x207A, 0x207E}, {0x208A, 0x208E}, {0x20A0, 0x214F},
        {0x2190, 0x245F},   {0x249C, 0x24E9}, {0x2500, 0x2775}, {0x2794, 0x2BFF}, {0x2E00, 0x2E7F},
        {0x3000, 0x3004},   {0x3008, 0x3020}, {0x3030, 0x3030}, {0xD800, 0xF8FF}, {0xFE00, 0xFE6F},
        {0xFF01, 0xFF0F},   {0xFF1A, 0xFF20}, {0xFF3B, 0xFF40}, {0xFF5B, 0xFF65}, {0xFFF0, 0xFFFF},
        {0x1F000, 0x1FAFF}, {0xE0000, 0x10FFFF}};
    return std::ranges::none_of(kNonWord, [c](const Range& r) { return c >= r.lo && c <= r.hi; });
}

} // namespace gc::core::detail
