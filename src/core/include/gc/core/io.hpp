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

[[nodiscard]] Result<Json> read_json_file(const std::filesystem::path& path);

// Escribe `value` con sangría `indent` (como json.dumps(indent=2, ensure_ascii=False))
// en un temporal y lo pone en su sitio con un reemplazo atómico. En Windows reintenta
// si el destino está abierto por otro proceso (antivirus, indexador, IDE), igual que
// projects._replace_file.
[[nodiscard]] Result<void> write_json_file(const std::filesystem::path& path, const Json& value,
                                           int indent = 2);

} // namespace gc::core
