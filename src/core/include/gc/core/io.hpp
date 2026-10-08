#pragma once

// Lectura y escritura de archivos JSON, y conversión de rutas UTF-8.

#include <filesystem>
#include <string>
#include <string_view>

#include "gc/core/error.hpp"
#include "gc/core/json.hpp"

namespace gc::core {

// Las rutas viajan por el motor como UTF-8. En Windows, construir un path desde
// `char*` usaría la página de códigos ANSI y rompería las rutas con tildes.
[[nodiscard]] std::filesystem::path path_from_utf8(std::string_view utf8);
[[nodiscard]] std::string path_to_utf8(const std::filesystem::path& path);

// Lee un JSON saltando el BOM de UTF-8 si lo hay (el CLI y las fixtures; video-yt no lo
// acepta en ningún archivo: ver read_json_file_py). Lee lo mismo que json.loads de Python:
// también NaN, Infinity, -Infinity y los números que se desbordan (1e999 es inf). Un NUL en
// el texto es un error. Divergencia: un entero de más de 64 bits pasa a real.
[[nodiscard]] Result<Json> read_json_file(const std::filesystem::path& path);
// json.loads(path.read_text(encoding="utf-8")) de Python, que es como lee video-yt sus
// archivos de data/ (ajustes, biblioteca, historial, SFX y projects.json): a diferencia de
// read_json_file, el BOM de UTF-8 es un error («Unexpected UTF-8 BOM») y quien lee cae a su
// valor por defecto.
[[nodiscard]] Result<Json> read_json_file_py(const std::filesystem::path& path);

// json.loads(text) de Python, como los lectores de archivo (sin el BOM). Divergencias: un
// entero de más de 64 bits pasa a real (y uno de más de 308 cifras, a infinito), y un sustituto
// suelto de UTF-16 (\ud800) pasa a U+FFFD (Python lo lee, aunque no podría volver a escribirlo).
[[nodiscard]] Result<Json> py_json_loads(std::string_view text);

// El texto exacto de json.dumps(value, ensure_ascii=False, indent=indent) de Python: los reales
// como repr() (y NaN, Infinity y -Infinity), «, » y «: » sin sangría (indent < 0) o «,» y «: »
// con ella, y solo se escapan las comillas, la barra y los caracteres de control. Error si un
// texto no es UTF-8 válido o si hay más de 1500 niveles (Python sí los escribe).
[[nodiscard]] Result<std::string> py_json_dumps(const Json& value, int indent = -1);

// Escribe py_json_dumps(value, indent) como path.write_text() de Python (en Windows, con los
// saltos de línea en CRLF) en un temporal y lo pone en su sitio con un reemplazo atómico. En
// Windows reintenta si el destino está abierto por otro proceso (antivirus, indexador, IDE),
// igual que projects._replace_file.
[[nodiscard]] Result<void> write_json_file(const std::filesystem::path& path, const Json& value,
                                           int indent = 2);

} // namespace gc::core
