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
#include <vector>

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
// JSON marcador (con un NUL delante y detrás) y, ya leído, al real. Si algún texto de verdad
// coincide con un marcador (salen más de los puestos), se repite con otros. (Si a la vez una
// clave repetida tapara un marcador, las cuentas cuadrarían: hace falta un texto con NUL igual
// al marcador, que no aparece en datos de verdad.)
struct Marks {
    std::string nan;
    std::string inf;
    std::string neg_inf;

    explicit Marks(int nonce) : nan(make(nonce, "NaN")), inf(make(nonce, "+inf")), neg_inf(make(nonce, "-inf")) {}

    static std::string make(int nonce, std::string_view what) {
        std::string out(1, '\0');
        out += "gc" + std::to_string(nonce) + ":" + std::string(what);
        out += '\0';
        return out;
    }
};

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

// El valor de un \uXXXX que empieza en `at` (la barra), o -1.
int unicode_escape(std::string_view text, std::size_t at) {
    if (at + 6 > text.size() || text[at] != '\\' || text[at + 1] != 'u') {
        return -1;
    }
    int value = 0;
    for (std::size_t k = at + 2; k < at + 6; ++k) {
        const char c = text[k];
        const int digit = c >= '0' && c <= '9'   ? c - '0'
                          : c >= 'a' && c <= 'f' ? c - 'a' + 10
                          : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                                 : -1;
        if (digit < 0) {
            return -1;
        }
        value = value * 16 + digit;
    }
    return value;
}

struct Marked {
    std::string text;
    std::size_t marks = 0; // cuántos NaN e infinitos pasaron a marcador
};

// El texto con los marcadores, o nada si no hay que cambiar nada. También cambia los sustitutos
// sueltos de UTF-16 (\ud800 sin su pareja) por U+FFFD: Python los lee (aunque luego no podría
// escribirlos en UTF-8) y nlohmann no, y el archivo entero se tomaría por ilegible.
std::optional<Marked> with_marks(std::string_view text, const Marks& m) {
    Marked out;
    std::size_t copied = 0; // hasta dónde está copiado `text` en `out`
    bool changed = false;
    const auto put = [&](std::size_t at, std::size_t len, std::string_view replacement) {
        out.text.append(text.substr(copied, at - copied));
        out.text += replacement;
        copied = at + len;
        changed = true;
    };
    // ¿Lo que acaba en `at` es la clave de un objeto (le siguen espacios y «:»)? Python no acepta
    // NaN ni números como clave: se deja tal cual y nlohmann también falla.
    const auto is_key = [&](std::size_t at) {
        const auto k = text.find_first_not_of(" \t\r\n", at);
        return k != std::string_view::npos && text[k] == ':';
    };
    const auto mark = [&](std::size_t at, std::size_t len, const std::string& which) {
        if (!is_key(at + len)) {
            put(at, len, quoted_mark(which));
            ++out.marks;
        }
    };
    bool in_string = false;
    for (std::size_t i = 0; i < text.size();) {
        const char c = text[i];
        if (in_string) {
            if (c == '\\') {
                const int v = unicode_escape(text, i);
                if (v >= 0xD800 && v <= 0xDBFF) {
                    const int low = unicode_escape(text, i + 6);
                    if (low >= 0xDC00 && low <= 0xDFFF) {
                        i += 12; // pareja: un carácter de fuera del plano básico
                    } else {
                        put(i, 6, "\\ufffd");
                        i += 6;
                    }
                } else if (v >= 0xDC00 && v <= 0xDFFF) {
                    put(i, 6, "\\ufffd");
                    i += 6;
                } else {
                    i += v >= 0 ? 6 : 2;
                }
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
            mark(i, 3, m.nan);
            i += 3;
        } else if (rest.starts_with("Infinity")) {
            mark(i, 8, m.inf);
            i += 8;
        } else if (rest.starts_with("-Infinity")) {
            mark(i, 9, m.neg_inf);
            i += 9;
        } else if (c == '-' || (c >= '0' && c <= '9')) {
            std::size_t j = i + 1;
            while (j < text.size() && ((text[j] >= '0' && text[j] <= '9') || text[j] == '.' || text[j] == 'e' ||
                                       text[j] == 'E' || text[j] == '+' || text[j] == '-')) {
                ++j;
            }
            const std::string_view token = text.substr(i, j - i);
            if (overflows(token)) {
                mark(i, j - i, token.starts_with('-') ? m.neg_inf : m.inf);
            }
            i = j;
        } else {
            ++i;
        }
    }
    if (!changed) {
        return std::nullopt;
    }
    out.text.append(text.substr(copied));
    return out;
}

// Los marcadores pasan a NaN e infinitos. Devuelve cuántos cambió (sin recursión: un JSON muy
// anidado no agota la pila).
std::size_t restore_marks(Json& root, const Marks& m) {
    std::size_t n = 0;
    std::vector<Json*> pending{&root};
    while (!pending.empty()) {
        Json* j = pending.back();
        pending.pop_back();
        if (j->is_string()) {
            const auto& s = j->get_ref<const std::string&>();
            if (s == m.nan) {
                *j = std::numeric_limits<double>::quiet_NaN();
                ++n;
            } else if (s == m.inf) {
                *j = std::numeric_limits<double>::infinity();
                ++n;
            } else if (s == m.neg_inf) {
                *j = -std::numeric_limits<double>::infinity();
                ++n;
            }
        } else if (j->is_structured()) {
            for (Json& v : *j) {
                pending.push_back(&v);
            }
        }
    }
    return n;
}

Result<Json> parse_text(std::string_view text, const std::string& where) {
    // nlohmann toma un NUL por el final del texto («{"a":1}\0basura» cargaría); para json.loads
    // de Python es un error, dentro o fuera de un texto. Pasa en archivos a medio escribir.
    if (text.contains('\0')) {
        return fail(ErrorCode::Parse, "carácter NUL en el JSON", where);
    }
    try {
        for (int nonce = 0; nonce < 8; ++nonce) {
            const Marks marks(nonce);
            auto marked = with_marks(text, marks);
            if (!marked) {
                return Json::parse(text);
            }
            Json j = Json::parse(marked->text);
            // Menos de los puestos: una clave repetida se quedó con otro valor, como en Python.
            if (restore_marks(j, marks) <= marked->marks) {
                return j;
            }
            // Más: algún texto de verdad era el marcador. Otra vez, con otro.
        }
        return fail(ErrorCode::Parse, "los marcadores de NaN e Infinity chocan con los datos", where);
    } catch (const Json::exception& e) {
        return fail(ErrorCode::Parse, e.what(), where);
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
    return parse_text(*text, path_to_utf8(path));
}

Result<Json> read_json_file_py(const std::filesystem::path& path) {
    auto text = read_bytes(path);
    if (!text) {
        return std::unexpected(text.error());
    }
    if (text->starts_with(kUtf8Bom)) {
        return fail(ErrorCode::Parse, "Unexpected UTF-8 BOM", path_to_utf8(path));
    }
    return parse_text(*text, path_to_utf8(path));
}

Result<Json> py_json_loads(std::string_view text) { return parse_text(text, ""); }

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

// Más niveles que esto no se escriben (Python sí). Se escribe sin recursión (con 1 MB de pila,
// la de MSVC, 1500 llamadas anidadas no caben en Debug), pero el límite se queda: es el
// documentado y lo que prueban los tests.
constexpr std::size_t kMaxDumpDepth = 1500;

// Un objeto o una lista abiertos: el siguiente elemento por escribir.
struct DumpFrame {
    const Json* value;
    Json::const_iterator next;
    bool object;
};

class Dumper {
public:
    Dumper(std::string& out, int indent, std::string& error) : out_(out), indent_(indent), error_(error) {}

    bool run(const Json& root) {
        if (!emit(root)) {
            return false;
        }
        while (!stack_.empty()) {
            DumpFrame& f = stack_.back();
            if (f.next == f.value->end()) {
                const bool object = f.object;
                stack_.pop_back();
                if (indent_ >= 0) {
                    newline(stack_.size());
                }
                out_ += object ? '}' : ']';
                continue;
            }
            if (f.next != f.value->begin()) {
                out_ += indent_ < 0 ? ", " : ",";
            }
            if (indent_ >= 0) {
                newline(stack_.size());
            }
            if (f.object) {
                if (!dump_string(out_, f.next.key())) {
                    error_ = "una clave del JSON no es UTF-8 válido";
                    return false;
                }
                out_ += ": ";
            }
            const Json& child = *f.next;
            ++f.next; // antes de emit: puede crecer la pila y mover `f`
            if (!emit(child)) {
                return false;
            }
        }
        return true;
    }

private:
    void newline(std::size_t level) {
        out_ += '\n';
        out_.append(static_cast<std::size_t>(indent_) * level, ' ');
    }

    // Escribe un valor simple o un contenedor vacío, o abre un contenedor (queda en la pila).
    bool emit(const Json& v) {
        if (stack_.size() > kMaxDumpDepth) {
            error_ = "el JSON tiene más de " + std::to_string(kMaxDumpDepth) + " niveles";
            return false;
        }
        switch (v.type()) {
        case Json::value_t::null: out_ += "null"; return true;
        case Json::value_t::boolean: out_ += v.get<bool>() ? "true" : "false"; return true;
        case Json::value_t::number_integer: out_ += std::to_string(v.get<std::int64_t>()); return true;
        case Json::value_t::number_unsigned: out_ += std::to_string(v.get<std::uint64_t>()); return true;
        case Json::value_t::number_float: {
            const double d = v.get<double>();
            out_ += std::isnan(d) ? "NaN" : std::isinf(d) ? (d > 0 ? "Infinity" : "-Infinity") : detail::py_repr(d);
            return true;
        }
        case Json::value_t::string:
            if (!dump_string(out_, v.get_ref<const std::string&>())) {
                error_ = "un texto del JSON no es UTF-8 válido";
                return false;
            }
            return true;
        case Json::value_t::array:
        case Json::value_t::object: {
            const bool object = v.is_object();
            if (v.empty()) {
                out_ += object ? "{}" : "[]";
                return true;
            }
            out_ += object ? '{' : '[';
            stack_.push_back(DumpFrame{&v, v.cbegin(), object});
            return true;
        }
        default: error_ = "un valor binario no es JSON"; return false; // no sale de un JSON leído
        }
    }

    std::string& out_;
    int indent_;
    std::string& error_;
    std::vector<DumpFrame> stack_;
};

} // namespace

Result<std::string> py_json_dumps(const Json& value, int indent) {
    std::string out;
    std::string error;
    if (!Dumper(out, indent, error).run(value)) {
        return fail(ErrorCode::InvalidArgument, error);
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
