#pragma once

// Piezas internas de la lectura del JSON del proyecto: ruta del campo para los
// errores, coerciones con las reglas de Pydantic v2 (modo lax) y la «verdad» de
// Python (`if x:`) que usan las migraciones.

#include <cstddef>
#include <cstdint>
#include <exception>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "gc/core/json.hpp"

namespace gc::core::detail {

// Ruta del campo que se está leyendo, encadenada por la pila de llamadas: no
// cuesta nada mientras todo va bien y solo se convierte a texto si hay un error.
struct PathNode {
    const PathNode* parent = nullptr;
    std::string_view key; // clave de objeto (vacía si es índice o la raíz)
    std::size_t index = 0;
    bool is_index = false;

    [[nodiscard]] PathNode child(std::string_view k) const { return {this, k, 0, false}; }
    [[nodiscard]] PathNode at(std::size_t i) const { return {this, {}, i, true}; }
    // "timeline.clips[3].reframe.zoom"
    [[nodiscard]] std::string str() const;
};

// Se lanza al validar y se convierte en gc::Error en la frontera de serialize.cpp.
struct ValidationFailure : std::exception {
    ValidationFailure(std::string p, std::string m) : path(std::move(p)), message(std::move(m)) {}
    [[nodiscard]] const char* what() const noexcept override { return message.c_str(); }
    std::string path;
    std::string message;
};

[[noreturn]] void fail_at(const PathNode& path, std::string message);

// float(texto) de Python: espacios alrededor, signo, exponente, "_" entre dígitos,
// inf/infinity/nan. Independiente del idioma del sistema (punto decimal siempre ".").
[[nodiscard]] std::optional<double> parse_py_float(std::string_view text);

// int(texto) de Python: "7", " +7 ", "1_000". Con accept_real_text (el «lax» de
// Pydantic) acepta también textos de real sin decimales ("7.0"); int() de Python no.
[[nodiscard]] std::optional<std::int64_t> parse_py_int(std::string_view text, bool accept_real_text);

[[nodiscard]] double to_float(const Json& j, const PathNode& path);
[[nodiscard]] std::int64_t to_int(const Json& j, const PathNode& path);
[[nodiscard]] bool to_bool(const Json& j, const PathNode& path);
[[nodiscard]] std::string to_str(const Json& j, const PathNode& path);

// bool(x) de Python sobre un valor JSON: null, false, 0, "", [] y {} son falsos.
[[nodiscard]] bool py_truthy(const Json& j);

// str(x) de Python para un escalar JSON (str(3) == "3", str(3.0) == "3.0", str(True) == "True").
[[nodiscard]] std::string py_str(const Json& j);

// Nombre del tipo JSON para los mensajes de error.
[[nodiscard]] const char* json_type_name(const Json& j);

} // namespace gc::core::detail
