#include "gc/core/mcp_registry.hpp"

#include <algorithm>

#include "gc/core/io.hpp"
#include "gc/core/mcp_help.hpp"

#include "py.hpp"

namespace gc::core::mcp {

namespace {

constexpr std::array<std::string_view, 8> kNotFound{"no encontrad", "not found", "inexistente", "no existe",
                                                    "no se encuentra", "ningún", "ninguna", "no se encontr"};
constexpr std::array<std::string_view, 9> kInvalid{"inválid", "invalid", "no válid", "debe ser", "vacío",
                                                   "vacía", "falta", "desconocid", "no hay"};

template <std::size_t N>
bool contains_any(std::string_view text, const std::array<std::string_view, N>& keys) {
    // En UTF-8 válido, buscar los bytes es buscar los caracteres (`k in m` de Python).
    return std::ranges::any_of(keys, [&](std::string_view k) { return text.contains(k); });
}

std::string dumps(const Json& payload) {
    if (auto text = py_json_dumps(payload)) {
        return std::move(*text);
    }
    // Un texto que no es UTF-8 válido (Python no tiene ninguno): los bytes malos como U+FFFD.
    return payload.dump(-1, ' ', false, Json::error_handler_t::replace);
}

} // namespace

Json McpError::payload() const {
    Json err = {{"code", code}, {"message", message}, {"retryable", retryable}};
    if (hint && !hint->empty()) {
        err["hint"] = *hint;
    }
    if (param && !param->empty()) {
        err["param"] = *param;
    }
    return Json{{"error", std::move(err)}};
}

std::pair<std::string_view, bool> classify_value_error(std::string_view message) {
    const std::string m = detail::py_lower(message);
    if (contains_any(m, kNotFound)) {
        return {"resource_not_found", false};
    }
    if (contains_any(m, kInvalid)) {
        return {"invalid_parameter", true};
    }
    return {"processing_error", false};
}

std::string tool_error_text(const McpError& error) {
    return dumps(error.payload());
}

std::string tool_error_text(std::string_view value_error_message) {
    const auto [code, retryable] = classify_value_error(value_error_message);
    return dumps(Json{{"error", {{"code", code}, {"message", value_error_message}, {"retryable", retryable}}}});
}

bool is_job_dto(const Json& result) {
    if (!result.is_object()) {
        return false;
    }
    const auto id = result.find("id");
    return id != result.end() && id->is_string() && result.contains("status") && result.contains("progress") &&
           result.contains("message");
}

Result<ToolInfo> ToolRegistry::add(std::string_view name, std::string_view access) {
    if (std::ranges::find(kAccessLevels, access) == kAccessLevels.end()) {
        return fail(ErrorCode::InvalidArgument, "access inválido: " + detail::py_repr(Json(access)) +
                                                    " (usa ('read', 'write', 'destructive'))");
    }
    if (find(name) != nullptr) {
        return fail(ErrorCode::InvalidArgument, "tool duplicada: " + std::string(name));
    }
    ToolInfo info{std::string(name), std::string(access),
                  Json{{"domain", tool_domain(name)}, {"access", access}},
                  Json{{"readOnlyHint", access == "read"}, {"destructiveHint", access == "destructive"}}};
    tools_.push_back(info);
    return info;
}

const ToolInfo* ToolRegistry::find(std::string_view name) const {
    const auto it = std::ranges::find(tools_, name, &ToolInfo::name);
    return it == tools_.end() ? nullptr : &*it;
}

} // namespace gc::core::mcp
