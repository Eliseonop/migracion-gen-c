#pragma once

// La ayuda del servidor MCP (espejo de backend/app/mcp_server/help_content.py de video-yt): el
// dominio de cada tool y la guía larga de cada dominio, la que leen `help://<domain>` y
// `describe_capabilities`. Las tablas salen del Python real con tools/gen/gen_mcp_help.py.

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace gc::core::mcp {

struct ToolDomain {
    std::string_view tool;
    std::string_view domain;
};

struct DomainGuide {
    std::string_view domain;
    std::string_view guide;
};

// TOOL_DOMAINS: cada tool del servidor con su dominio, en su orden.
[[nodiscard]] std::span<const ToolDomain> tool_domains();
// TOOL_DOMAINS.get(tool, "other").
[[nodiscard]] std::string_view tool_domain(std::string_view tool);
// HELP: la guía de cada dominio, en su orden.
[[nodiscard]] std::span<const DomainGuide> help_guides();
// domains(): sorted(set(TOOL_DOMAINS.values())).
[[nodiscard]] std::vector<std::string> help_domains();
// guide(domain): HELP.get(domain), con el nombre tal cual (sin quitar espacios ni mayúsculas).
[[nodiscard]] std::optional<std::string_view> guide(std::string_view domain);

} // namespace gc::core::mcp
