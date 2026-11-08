#pragma once

// El servidor MCP sin sockets: lo que video-yt hace con el SDK de MCP (mcp.server.mcpserver y
// streamable_http) sobre backend/app/mcp_server/server.py. Tres capas, de dentro afuera:
//
//   validate_tool_args  los argumentos de una tool contra su esquema, como Pydantic en modo laxo: el
//                       JSON dentro de un texto, los números como texto, los valores por defecto y
//                       el texto de sus errores.
//   handle_message      un mensaje JSON-RPC: initialize, ping, tools/*, resources/*, prompts/*.
//   handle_http         una petición HTTP de Streamable HTTP: la protección contra el DNS rebinding
//                       (Host y Origin), el Accept y el Content-Type, las sesiones y la respuesta en
//                       SSE. El servidor de verdad, con cpp-httplib, está en gc_agent.
//
// Diferencias con video-yt, a propósito:
//   - GET (el canal de avisos del servidor) da 405, que el protocolo permite a quien no lo ofrece: no
//     hay nada que avisar. video-yt lo deja abierto sin mandar nada.
//   - Solo se listan y se pueden llamar las tools hechas en C++ (`tool_names`).
//   - Un número entero solo llega a 64 bits (Python no tiene tope).
//   - Sin el modo sin sesión del protocolo 2026-07-28: una versión que no es de las de initialize da
//     el mismo error 400 que video-yt da cuando no se dice bien.

#include <filesystem>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "gc/core/error.hpp"
#include "gc/core/json.hpp"
#include "gc/core/mcp_audit.hpp"
#include "gc/core/mcp_capabilities.hpp"
#include "gc/core/mcp_tools_edit.hpp"

namespace gc::core::mcp {

// DEFAULT_MAX_REQUEST_BODY_SIZE del SDK: más que esto, un 413 antes de leerlo (lo hace gc_agent).
inline constexpr std::size_t kMaxRequestBody = std::size_t{4} * 1024 * 1024;

// Cada tool de video-yt, en su orden, con su esquema, sus anotaciones y su `_meta`.
[[nodiscard]] const Json& all_tools();
// Las tools que tiene el servidor C++: las de lectura (mcp_tools_read.hpp) y las de edición.
[[nodiscard]] const std::vector<std::string>& tool_names();
// El texto de las instrucciones de server.py.
[[nodiscard]] const std::string& server_instructions();

// `tool` es su entrada de all_tools(). Devuelve todos sus parámetros, en orden, con los valores ya
// convertidos y los por defecto (model_dump_one_level) y sin lo que no declara. El error, con
// ErrorCode::InvalidArgument, lleva el str() del ValidationError de Pydantic.
[[nodiscard]] Result<Json> validate_tool_args(const Json& tool, const Json& args);

struct HttpRequest {
    std::string method;
    std::map<std::string, std::string> headers; // con el nombre en minúsculas
    std::string body;
};

struct HttpResponse {
    int status = 200;
    std::optional<std::string> content_type;
    std::optional<std::string> session_id; // la cabecera Mcp-Session-Id
    std::string body;
};

class McpServer {
public:
    // `edit` es lo que necesitan las tools de edición (el almacén, data/, los ids…). `audit` puede
    // ser nullptr: sin registro.
    McpServer(EditToolsEnv edit, RuntimeEnv runtime, Audit* audit = nullptr);

    // Una petición entera; se atienden de una en una.
    [[nodiscard]] HttpResponse handle_http(const HttpRequest& request);
    // Un mensaje JSON-RPC ya validado con id: la respuesta, con su `result` o su `error`.
    [[nodiscard]] Json handle_message(const Json& message);

private:
    [[nodiscard]] Json call_tool(const Json& params, const Json& id);
    [[nodiscard]] Result<Json> run_tool(std::string_view name, const Json& args);
    [[nodiscard]] Json read_resource_message(const Json& params, const Json& id);
    [[nodiscard]] HttpResponse handle_post(const HttpRequest& request, const std::optional<std::string>& session);

    EditToolsEnv edit_;
    RuntimeEnv runtime_;
    Audit* audit_;
    std::string saved_model_;
    std::map<std::string, bool> sessions_; // id → terminada
    std::mutex mutex_;
};

} // namespace gc::core::mcp
