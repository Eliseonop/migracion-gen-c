#pragma once

// El registro de tools del servidor MCP y sus errores estructurados (espejo de
// backend/app/mcp_server/registry.py de video-yt): los niveles de acceso, los 8 códigos de error,
// el payload `{"error": {...}}` que llega al agente como resultado `is_error`, la clasificación de
// los ValueError «sueltos» de las ops y lo que se registra de cada tool (su dominio, su acceso y
// las pistas de solo lectura y destructiva). La auditoría que envuelve cada llamada está en
// mcp_audit.hpp.

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "gc/core/error.hpp"
#include "gc/core/json.hpp"

namespace gc::core::mcp {

inline constexpr std::array<std::string_view, 3> kAccessLevels{"read", "write", "destructive"};

// Cerrado y corto: el agente distingue «corregir y reintentar» de «replantear» y de «avisar al humano».
inline constexpr std::array<std::string_view, 8> kErrorCodes{
    "invalid_parameter",     // valor o forma mal: corregir el parámetro (retryable)
    "resource_not_found",    // clip, pista, material o job inexistente: releer el estado
    "operation_not_allowed", // p. ej. velocidad en un texto: usar otra tool
    "configuration_error",   // falta una API key o un modelo: avisar al humano
    "dependency_error",      // binario o modelo ausente (ffmpeg, whisper, piper): avisar
    "processing_error",      // fallo interno del job o de ffmpeg
    "temporary_error",       // red o timeout: reintentar con espera
    "job_running",           // acción en curso: wait_for_job y reintentar
};

// MCPError: un error de tool con su código.
struct McpError {
    std::string code;
    std::string message;
    std::optional<std::string> hint;
    bool retryable = false;
    std::optional<std::string> param;

    // {"error": {code, message, retryable[, hint][, param]}}: hint y param solo si no están vacíos.
    [[nodiscard]] Json payload() const;
};

// _classify_value_error: el código y si se puede reintentar de un ValueError suelto, por su
// texto en minúsculas («no encontrad…» → resource_not_found; «inválid…», «falta»… → invalid_parameter).
[[nodiscard]] std::pair<std::string_view, bool> classify_value_error(std::string_view message);

// El texto del ToolError de _to_tool_error: json.dumps(payload, ensure_ascii=False). Para un
// ValueError suelto, su código sale de classify_value_error. (Un fallo que no es ValueError no
// llega al agente: el servidor lo oculta.)
[[nodiscard]] std::string tool_error_text(const McpError& error);
[[nodiscard]] std::string tool_error_text(std::string_view value_error_message);

// _is_job_dto: un dict con `id` de texto y las claves status, progress y message.
[[nodiscard]] bool is_job_dto(const Json& result);

// Lo que se registra de una tool en el servidor: su meta ({domain, access}, con el dominio de
// TOOL_DOMAINS o «other») y sus anotaciones ({readOnlyHint, destructiveHint}).
struct ToolInfo {
    std::string name;
    std::string access;
    Json meta;
    Json annotations;
};

class ToolRegistry {
public:
    // registry.tool(mcp, access=access, name=name): error si el acceso no es uno de kAccessLevels
    // («access inválido: 'x' (usa ('read', 'write', 'destructive'))») o si ya hay una tool con
    // ese nombre («tool duplicada: x»), en ese orden.
    [[nodiscard]] Result<ToolInfo> add(std::string_view name, std::string_view access);
    [[nodiscard]] const std::vector<ToolInfo>& tools() const noexcept { return tools_; }
    [[nodiscard]] const ToolInfo* find(std::string_view name) const;
    void reset() noexcept { tools_.clear(); }

private:
    std::vector<ToolInfo> tools_;
};

} // namespace gc::core::mcp
