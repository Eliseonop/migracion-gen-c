#include "gc/core/mcp_help.hpp"

#include <algorithm>
#include <array>

namespace gc::core::mcp {

namespace {

#include "mcp_help.inc"

} // namespace

std::span<const ToolDomain> tool_domains() {
    return kToolDomains;
}

std::string_view tool_domain(std::string_view tool) {
    for (const ToolDomain& t : kToolDomains) {
        if (t.tool == tool) {
            return t.domain;
        }
    }
    return "other";
}

std::span<const DomainGuide> help_guides() {
    return kHelp;
}

std::vector<std::string> help_domains() {
    std::vector<std::string> out;
    out.reserve(kToolDomains.size());
    for (const ToolDomain& t : kToolDomains) {
        out.emplace_back(t.domain);
    }
    std::ranges::sort(out); // por bytes de UTF-8: el mismo orden que los puntos de código de Python
    const auto [first, last] = std::ranges::unique(out);
    out.erase(first, last);
    return out;
}

std::optional<std::string_view> guide(std::string_view domain) {
    for (const DomainGuide& g : kHelp) {
        if (g.domain == domain) {
            return g.guide;
        }
    }
    return std::nullopt;
}

} // namespace gc::core::mcp
