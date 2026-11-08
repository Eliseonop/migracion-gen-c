#pragma once

#include <span>
#include <string>

namespace genclip {

// genclip mcp --data <carpeta> --clips <carpeta> [--fx <catalog.json>] [--sfx <carpeta>] [--port 8000]
// El servidor MCP sobre los proyectos de esa carpeta data/, hasta que se cierre.
int cmd_mcp(std::span<const std::string> args);

} // namespace genclip
