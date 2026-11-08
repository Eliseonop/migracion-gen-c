#pragma once

// El servidor MCP de verdad: gc::core::mcp::McpServer detrás de un servidor HTTP (cpp-httplib) en
// http://127.0.0.1:<puerto>/mcp, por defecto el 8000, donde Claude Desktop ya lo busca.

#include <memory>
#include <string>

#include "gc/core/error.hpp"
#include "gc/core/mcp_server.hpp"

namespace gc::agent {

class McpHttpServer {
public:
    explicit McpHttpServer(core::mcp::McpServer& server);
    ~McpHttpServer();
    McpHttpServer(const McpHttpServer&) = delete;
    McpHttpServer& operator=(const McpHttpServer&) = delete;

    // Abre el puerto (0: uno libre) y lo devuelve. Sin aceptar conexiones todavía.
    [[nodiscard]] Result<int> bind(const std::string& host = "127.0.0.1", int port = 8000);
    // Atiende peticiones hasta stop(). Se bloquea.
    void serve();
    // Desde otro hilo.
    void stop();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace gc::agent
