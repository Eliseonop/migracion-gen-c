#pragma once

#include <expected>
#include <string>
#include <utility>

namespace gc {

enum class ErrorCode {
    InvalidArgument, // argumento fuera de rango o mal formado
    NotFound,        // proyecto, clip, archivo… inexistente
    Parse,           // texto que no es JSON válido
    Validation,      // JSON válido que no cumple el modelo (el ValidationError de Pydantic)
    Io,              // lectura/escritura en disco
    Unsupported,     // función no disponible en esta plataforma o build
    InUse,           // el recurso se está usando (el 409 «in_use» de la API)
    Internal,        // fallo inesperado (bug)
};

[[nodiscard]] const char* to_string(ErrorCode code) noexcept;

struct Error {
    ErrorCode code = ErrorCode::Internal;
    std::string message;
    // Campo donde está el problema, con la sintaxis de Python/JS:
    // "timeline.clips[3].reframe.zoom". Vacío si no aplica.
    std::string path;

    [[nodiscard]] std::string to_string() const;
};

// Resultado de cualquier operación que puede fallar entre módulos del motor.
template <class T>
using Result = std::expected<T, Error>;

[[nodiscard]] inline std::unexpected<Error> fail(ErrorCode code, std::string message, std::string path = {}) {
    return std::unexpected(Error{code, std::move(message), std::move(path)});
}

} // namespace gc
