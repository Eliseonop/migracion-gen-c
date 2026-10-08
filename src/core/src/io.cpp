#include "gc/core/io.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <format>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <system_error>
#include <thread>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "py.hpp"

namespace gc::core {

namespace fs = std::filesystem;

std::filesystem::path path_from_utf8(std::string_view utf8) {
    std::u8string u8(utf8.size(), u8'\0');
    std::ranges::transform(utf8, u8.begin(), [](char c) { return static_cast<char8_t>(c); });
    return fs::path(u8);
}

std::string path_to_utf8(const std::filesystem::path& path) {
    const std::u8string u8 = path.u8string();
    std::string out(u8.size(), '\0');
    std::ranges::transform(u8, out.begin(), [](char8_t c) { return static_cast<char>(c); });
    return out;
}

namespace {

Result<std::string> read_bytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        return fail(ErrorCode::NotFound, "no se puede abrir el archivo", path_to_utf8(path));
    }
    // Leer de una vez con el tamaño conocido (más rápido que istreambuf_iterator).
    const std::streamoff size = in.tellg();
    std::string text(size > 0 ? static_cast<std::size_t>(size) : 0U, '\0');
    in.seekg(0);
    if (size < 0 || !in.read(text.data(), size)) {
        return fail(ErrorCode::Io, "error al leer el archivo", path_to_utf8(path));
    }
    return text;
}

// NaN e infinitos de Python mientras lee nlohmann, que no los acepta: fuera de los textos,
// `NaN`, `Infinity`, `-Infinity` y los números que se desbordan (1e999) pasan a un texto de
// JSON marcador (con NUL, que no aparece en datos de verdad) y, ya leído, al real.
const std::string kNanMark("\0gc:NaN\0", 8);
const std::string kInfMark("\0gc:+inf\0", 9);
const std::string kNegInfMark("\0gc:-inf\0", 9);

// El marcador como texto de JSON (el NUL, escapado).
std::string quoted_mark(const std::string& mark) {
    std::string out = "\"";
    for (const char c : mark) {
        out += c == '\0' ? std::string("\\u0000") : std::string(1, c);
    }
    return out + "\"";
}

// ¿El número de JSON `token` se sale de double por arriba? Por abajo (1e-999) da 0.0, como en
// Python, y nlohmann ya lo lee así.
bool overflows(std::string_view token) {
    double value = 0.0;
    if (std::from_chars(token.data(), token.data() + token.size(), value).ec != std::errc::result_out_of_range) {
        return false;
    }
    if (token.starts_with('-')) {
        token.remove_prefix(1);
    }
    // El exponente decimal de la primera cifra no nula: positivo = demasiado grande.
    const auto e = token.find_first_of("eE");
    const std::string_view mantissa = token.substr(0, e);
    long long exp10 = 0;
    if (e != std::string_view::npos) {
        std::string_view digits = token.substr(e + 1);
        const bool negative = digits.starts_with('-');
        if (digits.starts_with('-') || digits.starts_with('+')) {
            digits.remove_prefix(1);
        }
        for (const char c : digits) {
            exp10 = std::min(exp10 * 10 + (c - '0'), 1'000'000'000LL); // saturado: basta con el signo
        }
        exp10 = negative ? -exp10 : exp10;
    }
    const auto dot = mantissa.find('.');
    const std::string_view int_part = mantissa.substr(0, dot);
    if (const auto first = int_part.find_first_not_of('0'); first != std::string_view::npos) {
        return static_cast<long long>(int_part.size() - first) - 1 + exp10 > 0;
    }
    if (dot == std::string_view::npos) {
        return false;
    }
    const auto nonzero = mantissa.substr(dot + 1).find_first_not_of('0');
    return nonzero != std::string_view::npos && exp10 - static_cast<long long>(nonzero) - 1 > 0;
}

// El texto con los marcadores, o nada si no hay nada que marcar.
std::optional<std::string> with_marks(std::string_view text) {
    std::string out;
    std::size_t copied = 0; // hasta dónde está copiado `text` en `out`
    bool marked = false;
    const auto put = [&](std::size_t at, std::size_t len, const std::string& mark) {
        out.append(text.substr(copied, at - copied));
        out += quoted_mark(mark);
        copied = at + len;
        marked = true;
    };
    bool in_string = false;
    for (std::size_t i = 0; i < text.size();) {
        const char c = text[i];
        if (in_string) {
            if (c == '\\') {
                i += 2;
                continue;
            }
            in_string = c != '"';
            ++i;
            continue;
        }
        const std::string_view rest = text.substr(i);
        if (c == '"') {
            in_string = true;
            ++i;
        } else if (rest.starts_with("NaN")) {
            put(i, 3, kNanMark);
            i += 3;
        } else if (rest.starts_with("Infinity")) {
            put(i, 8, kInfMark);
            i += 8;
        } else if (rest.starts_with("-Infinity")) {
            put(i, 9, kNegInfMark);
            i += 9;
        } else if (c == '-' || (c >= '0' && c <= '9')) {
            std::size_t j = i + 1;
            while (j < text.size() && ((text[j] >= '0' && text[j] <= '9') || text[j] == '.' || text[j] == 'e' ||
                                       text[j] == 'E' || text[j] == '+' || text[j] == '-')) {
                ++j;
            }
            const std::string_view token = text.substr(i, j - i);
            if (overflows(token)) {
                put(i, j - i, token.starts_with('-') ? kNegInfMark : kInfMark);
            }
            i = j;
        } else {
            ++i;
        }
    }
    if (!marked) {
        return std::nullopt;
    }
    out.append(text.substr(copied));
    return out;
}

void restore_marks(Json& j) {
    if (j.is_string()) {
        const auto& s = j.get_ref<const std::string&>();
        if (s == kNanMark) {
            j = std::numeric_limits<double>::quiet_NaN();
        } else if (s == kInfMark) {
            j = std::numeric_limits<double>::infinity();
        } else if (s == kNegInfMark) {
            j = -std::numeric_limits<double>::infinity();
        }
    } else if (j.is_structured()) {
        for (Json& v : j) {
            restore_marks(v);
        }
    }
}

Result<Json> parse_text(const std::string& text, const std::filesystem::path& path) {
    // nlohmann toma un NUL por el final del texto («{"a":1}\0basura» cargaría); para json.loads
    // de Python es un error, dentro o fuera de un texto. Pasa en archivos a medio escribir.
    if (text.contains('\0')) {
        return fail(ErrorCode::Parse, "carácter NUL en el JSON", path_to_utf8(path));
    }
    try {
        if (auto marked = with_marks(text)) {
            Json j = Json::parse(*marked);
            restore_marks(j);
            return j;
        }
        return Json::parse(text);
    } catch (const Json::exception& e) {
        return fail(ErrorCode::Parse, e.what(), path_to_utf8(path));
    }
}

constexpr std::string_view kUtf8Bom = "\xEF\xBB\xBF";

} // namespace

Result<Json> read_json_file(const std::filesystem::path& path) {
    auto text = read_bytes(path);
    if (!text) {
        return std::unexpected(text.error());
    }
    // BOM de UTF-8 (lo dejan algunos editores de Windows).
    if (text->starts_with(kUtf8Bom)) {
        text->erase(0, kUtf8Bom.size());
    }
    return parse_text(*text, path);
}

Result<Json> read_json_file_py(const std::filesystem::path& path) {
    auto text = read_bytes(path);
    if (!text) {
        return std::unexpected(text.error());
    }
    if (text->starts_with(kUtf8Bom)) {
        return fail(ErrorCode::Parse, "Unexpected UTF-8 BOM", path_to_utf8(path));
    }
    return parse_text(*text, path);
}

namespace {

#ifdef _WIN32
std::wstring wide(const fs::path& p) { return p.wstring(); }

// os.replace de Windows falla con WinError 5/32 si otro proceso tiene el destino
// abierto (antivirus, indexador, IDE): se reintenta con espera creciente y, como
// último recurso, se sobrescribe en el sitio (igual que projects._replace_file).
bool replace_file(const fs::path& src, const fs::path& dest, std::string& error) {
    const std::wstring wsrc = wide(src);
    const std::wstring wdest = wide(dest);
    auto delay = std::chrono::milliseconds(40);
    DWORD last = 0;
    for (int attempt = 0; attempt < 12; ++attempt) {
        const DWORD attrs = GetFileAttributesW(wdest.c_str());
        if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_READONLY) != 0) {
            SetFileAttributesW(wdest.c_str(), attrs & ~static_cast<DWORD>(FILE_ATTRIBUTE_READONLY));
        }
        if (MoveFileExW(wsrc.c_str(), wdest.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0) {
            return true;
        }
        last = GetLastError();
        if (last != ERROR_ACCESS_DENIED && last != ERROR_SHARING_VIOLATION) {
            break;
        }
        std::this_thread::sleep_for(delay);
        delay = std::min(std::chrono::milliseconds(250), delay * 3 / 2);
    }
    // Sobrescribir en el sitio.
    std::error_code ec;
    {
        std::ifstream in(src, std::ios::binary);
        std::ofstream out(dest, std::ios::binary | std::ios::trunc);
        if (in && out && (out << in.rdbuf()) && out.flush()) {
            in.close();
            fs::remove(src, ec);
            return true;
        }
    }
    error = "no se pudo reemplazar el archivo (error de Windows " + std::to_string(last) + ")";
    return false;
}
#else
bool replace_file(const fs::path& src, const fs::path& dest, std::string& error) {
    std::error_code ec;
    fs::rename(src, dest, ec); // rename() de POSIX ya es atómico y reemplaza
    if (ec) {
        error = ec.message();
        return false;
    }
    return true;
}
#endif

} // namespace

namespace {

bool valid_utf8(std::string_view s) {
    for (std::size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i]);
        std::size_t len = 0;
        char32_t cp = 0;
        if (c < 0x80) {
            ++i;
            continue;
        }
        if ((c & 0xE0U) == 0xC0U) {
            len = 2;
            cp = c & 0x1FU;
        } else if ((c & 0xF0U) == 0xE0U) {
            len = 3;
            cp = c & 0x0FU;
        } else if ((c & 0xF8U) == 0xF0U) {
            len = 4;
            cp = c & 0x07U;
        } else {
            return false;
        }
        if (i + len > s.size()) {
            return false;
        }
        for (std::size_t k = 1; k < len; ++k) {
            const auto b = static_cast<unsigned char>(s[i + k]);
            if ((b & 0xC0U) != 0x80U) {
                return false;
            }
            cp = (cp << 6U) | (b & 0x3FU);
        }
        // Sin formas largas, sin sustitutos y dentro de Unicode.
        const char32_t min = len == 2 ? 0x80 : len == 3 ? 0x800 : 0x10000;
        if (cp < min || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
            return false;
        }
        i += len;
    }
    return true;
}

// Un texto como lo escribe json.dumps(ensure_ascii=False): solo se escapan «"», «\» y los
// caracteres de control (\b, \f, \n, \r, \t y \u00XX en minúsculas).
bool dump_string(std::string& out, std::string_view s) {
    if (!valid_utf8(s)) {
        return false;
    }
    out += '"';
    for (const char ch : s) {
        switch (ch) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        default:
            if (static_cast<unsigned char>(ch) < 0x20) {
                out += std::format("\\u{:04x}", static_cast<unsigned>(static_cast<unsigned char>(ch)));
            } else {
                out += ch;
            }
        }
    }
    out += '"';
    return true;
}

bool dump_value(std::string& out, const Json& v, int indent, int level) {
    const auto newline = [&](int lvl) {
        out += '\n';
        out.append(static_cast<std::size_t>(indent) * static_cast<std::size_t>(lvl), ' ');
    };
    switch (v.type()) {
    case Json::value_t::null: out += "null"; return true;
    case Json::value_t::boolean: out += v.get<bool>() ? "true" : "false"; return true;
    case Json::value_t::number_integer: out += std::to_string(v.get<std::int64_t>()); return true;
    case Json::value_t::number_unsigned: out += std::to_string(v.get<std::uint64_t>()); return true;
    case Json::value_t::number_float: {
        const double d = v.get<double>();
        out += std::isnan(d) ? "NaN" : std::isinf(d) ? (d > 0 ? "Infinity" : "-Infinity") : detail::py_repr(d);
        return true;
    }
    case Json::value_t::string: return dump_string(out, v.get_ref<const std::string&>());
    case Json::value_t::array:
    case Json::value_t::object: {
        const bool object = v.is_object();
        if (v.empty()) {
            out += object ? "{}" : "[]";
            return true;
        }
        out += object ? '{' : '[';
        bool first = true;
        for (auto it = v.begin(); it != v.end(); ++it) {
            if (!first) {
                out += indent < 0 ? ", " : ",";
            }
            first = false;
            if (indent >= 0) {
                newline(level + 1);
            }
            if (object) {
                if (!dump_string(out, it.key())) {
                    return false;
                }
                out += ": ";
            }
            if (!dump_value(out, *it, indent, level + 1)) {
                return false;
            }
        }
        if (indent >= 0) {
            newline(level);
        }
        out += object ? '}' : ']';
        return true;
    }
    default: return false; // binario: no sale de un JSON leído
    }
}

} // namespace

Result<std::string> py_json_dumps(const Json& value, int indent) {
    std::string out;
    if (!dump_value(out, value, indent, 0)) {
        return fail(ErrorCode::InvalidArgument, "un texto del JSON no es UTF-8 válido");
    }
    return out;
}

Result<void> write_json_file(const std::filesystem::path& path, const Json& value, int indent) {
    auto dumped = py_json_dumps(value, indent);
    if (!dumped) {
        return fail(ErrorCode::InvalidArgument, dumped.error().message, path_to_utf8(path));
    }
    // Sin salto de línea final: video-yt escribe json.dumps(...) tal cual.
    std::string text = std::move(*dumped);
#ifdef _WIN32
    // path.write_text() en modo texto: cada «\n» sale como «\r\n» (dentro de los textos van
    // escapados, así que solo cambian los de la sangría).
    std::string crlf;
    crlf.reserve(text.size() + text.size() / 16);
    for (const char c : text) {
        if (c == '\n') {
            crlf += '\r';
        }
        crlf += c;
    }
    text = std::move(crlf);
#endif

    std::error_code ec;
    if (path.has_parent_path()) {
        fs::create_directories(path.parent_path(), ec);
    }
    fs::path tmp = path;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out || !out.write(text.data(), static_cast<std::streamsize>(text.size())) || !out.flush()) {
            return fail(ErrorCode::Io, "no se pudo escribir el temporal", path_to_utf8(tmp));
        }
    }
    std::string error;
    if (!replace_file(tmp, path, error)) {
        fs::remove(tmp, ec);
        return fail(ErrorCode::Io, error, path_to_utf8(path));
    }
    return {};
}

} // namespace gc::core
