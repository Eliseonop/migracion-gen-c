#include "gc/core/mcp_server.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <span>
#include <string>
#include <utility>

#include "gc/core/history_store.hpp"
#include "gc/core/io.hpp"
#include "gc/core/mcp_registry.hpp"
#include "gc/core/mcp_tools_media.hpp"
#include "gc/core/mcp_resources.hpp"
#include "gc/core/mcp_tools_read.hpp"

#include "json_reader.hpp"
#include "py.hpp"

namespace gc::core::mcp {

namespace {

#include "mcp_server_data.inc"

using detail::py_float;
using detail::py_repr;
using detail::py_strip;

constexpr std::string_view kPydanticErrors = "https://errors.pydantic.dev/2.13/v/";

template <std::size_t N>
std::string joined(const std::array<std::string_view, N>& pieces) {
    std::string out;
    for (const std::string_view p : pieces) {
        out += p;
    }
    return out;
}

// --- Validación de los argumentos --------------------------------------------------------------

const char* py_type_name(const Json& v) {
    switch (v.type()) {
    case Json::value_t::null: return "NoneType";
    case Json::value_t::boolean: return "bool";
    case Json::value_t::number_float: return "float";
    case Json::value_t::number_integer:
    case Json::value_t::number_unsigned: return "int";
    case Json::value_t::string: return "str";
    case Json::value_t::array: return "list";
    case Json::value_t::object: return "dict";
    default: return "object";
    }
}

// Pydantic corta lo que muestra del valor: más de 50 caracteres, los 25 primeros y los 24 últimos.
std::string shown_value(const Json& v) {
    std::string repr = py_repr(v);
    if (detail::py_len(repr) <= 50) {
        return repr;
    }
    const std::string head = detail::py_slice(repr, 25);
    std::size_t tail_start = repr.size();
    for (int kept = 0; kept < 24 && tail_start > 0; ++kept) {
        do {
            --tail_start;
        } while (tail_start > 0 && (static_cast<unsigned char>(repr[tail_start]) & 0xC0U) == 0x80U);
    }
    return head + "..." + repr.substr(tail_start);
}

struct ArgIssue {
    std::string loc;
    std::string message;
    std::string type;
    std::string input;
    std::string input_type;
};

ArgIssue issue(std::string loc, std::string message, std::string type, const Json& value) {
    return {std::move(loc), std::move(message), std::move(type), shown_value(value), py_type_name(value)};
}

std::string without_underscores(std::string text) {
    // «1_000» vale; «_1», «1_» y «1__0», no.
    const auto bad = text.starts_with('_') || text.ends_with('_') || text.contains("__");
    if (bad) {
        return "x";
    }
    std::erase(text, '_');
    return text;
}

// El texto de un entero: con signo, sin espacios alrededor y con los «.0» del final, que no cuentan.
std::optional<std::int64_t> text_to_int(const std::string& raw) {
    std::string text = without_underscores(py_strip(raw));
    if (const auto dot = text.find('.'); dot != std::string::npos) {
        const std::string_view frac = std::string_view(text).substr(dot + 1);
        if (!std::ranges::all_of(frac, [](char c) { return c == '0'; })) {
            return std::nullopt;
        }
        text.resize(dot);
    }
    const bool negative = text.starts_with('-');
    const std::string_view digits = std::string_view(text).substr(negative || text.starts_with('+') ? 1 : 0);
    if (digits.empty() || !std::ranges::all_of(digits, [](char c) { return c >= '0' && c <= '9'; })) {
        return std::nullopt;
    }
    constexpr std::size_t kMaxInt64Digits = 18; // lo que cabe seguro en 64 bits (ver mcp_server.hpp)
    if (digits.size() > kMaxInt64Digits) {
        return std::nullopt;
    }
    std::int64_t value = 0;
    for (const char c : digits) {
        value = value * 10 + (c - '0');
    }
    return negative ? -value : value;
}

std::optional<double> text_to_float(const std::string& raw) {
    const std::string stripped = py_strip(raw);
    if (stripped.empty()) {
        return std::nullopt;
    }
    return py_float(Json(without_underscores(stripped)));
}

std::optional<bool> text_to_bool(const std::string& raw) {
    const std::string t = detail::py_lower(raw);
    for (const std::string_view yes : {"true", "t", "yes", "y", "on", "1"}) {
        if (t == yes) {
            return true;
        }
    }
    for (const std::string_view no : {"false", "f", "no", "n", "off", "0"}) {
        if (t == no) {
            return false;
        }
    }
    return std::nullopt;
}

std::string_view schema_type(const Json& schema) {
    const auto it = schema.find("type");
    return it != schema.end() && it->is_string() ? std::string_view(it->get_ref<const std::string&>()) : std::string_view();
}

// Convierte `value` según `schema` (un `type`, o `anyOf` con null) o añade por qué no se puede.
Json coerce(const Json& schema, const Json& value, const std::string& loc, std::vector<ArgIssue>& issues);

Json coerce_number(const Json& value, const std::string& loc, std::vector<ArgIssue>& issues) {
    if (value.is_number()) {
        return value.get<double>();
    }
    if (value.is_boolean()) {
        return value.get<bool>() ? 1.0 : 0.0;
    }
    if (value.is_string()) {
        if (const auto x = text_to_float(value.get<std::string>())) {
            return *x;
        }
        issues.push_back(issue(loc, "Input should be a valid number, unable to parse string as a number", "float_parsing", value));
        return nullptr;
    }
    issues.push_back(issue(loc, "Input should be a valid number", "float_type", value));
    return nullptr;
}

Json coerce_integer(const Json& value, const std::string& loc, std::vector<ArgIssue>& issues) {
    if (value.is_number_integer()) {
        return value;
    }
    if (value.is_boolean()) {
        return value.get<bool>() ? 1 : 0;
    }
    if (value.is_number_float()) {
        const double d = value.get<double>();
        if (std::isfinite(d) && std::floor(d) == d) {
            return static_cast<std::int64_t>(d);
        }
        issues.push_back(issue(loc, "Input should be a valid integer, got a number with a fractional part", "int_from_float", value));
        return nullptr;
    }
    if (value.is_string()) {
        if (const auto x = text_to_int(value.get<std::string>())) {
            return *x;
        }
        issues.push_back(issue(loc, "Input should be a valid integer, unable to parse string as an integer", "int_parsing", value));
        return nullptr;
    }
    issues.push_back(issue(loc, "Input should be a valid integer", "int_type", value));
    return nullptr;
}

Json coerce_boolean(const Json& value, const std::string& loc, std::vector<ArgIssue>& issues) {
    if (value.is_boolean()) {
        return value;
    }
    if (value.is_number()) {
        const double d = value.get<double>();
        if (d == 0.0 || d == 1.0) {
            return d == 1.0;
        }
    } else if (value.is_string()) {
        if (const auto b = text_to_bool(value.get<std::string>())) {
            return *b;
        }
    }
    // Un entero o un texto que no dice nada es bool_parsing; un real o lo demás, bool_type.
    if (value.is_number_integer() || value.is_string()) {
        issues.push_back(issue(loc, "Input should be a valid boolean, unable to interpret input", "bool_parsing", value));
    } else {
        issues.push_back(issue(loc, "Input should be a valid boolean", "bool_type", value));
    }
    return nullptr;
}

Json coerce(const Json& schema, const Json& value, const std::string& loc, std::vector<ArgIssue>& issues) {
    if (schema.contains("anyOf")) {
        if (value.is_null()) {
            return nullptr;
        }
        for (const Json& option : schema["anyOf"]) {
            if (schema_type(option) != "null") {
                return coerce(option, value, loc, issues);
            }
        }
        return value;
    }
    const std::string_view type = schema_type(schema);
    if (type == "string") {
        if (!value.is_string()) {
            issues.push_back(issue(loc, "Input should be a valid string", "string_type", value));
        }
        return value;
    }
    if (type == "number") {
        return coerce_number(value, loc, issues);
    }
    if (type == "integer") {
        return coerce_integer(value, loc, issues);
    }
    if (type == "boolean") {
        return coerce_boolean(value, loc, issues);
    }
    if (type == "object") {
        if (!value.is_object()) {
            issues.push_back(issue(loc, "Input should be a valid dictionary", "dict_type", value));
        }
        return value;
    }
    if (type == "array") {
        if (!value.is_array()) {
            issues.push_back(issue(loc, "Input should be a valid list", "list_type", value));
            return value;
        }
        if (!schema.contains("items") || schema["items"].empty()) {
            return value;
        }
        Json out = Json::array();
        for (std::size_t i = 0; i < value.size(); ++i) {
            out.push_back(coerce(schema["items"], value[i], loc + "." + std::to_string(i), issues));
        }
        return out;
    }
    return value;
}

// pre_parse_json: un texto en un parámetro que no es solo `str` puede traer JSON (Claude Desktop lo
// hace). Los escalares se quedan como texto, y null, una lista o un objeto sustituyen al texto.
Json pre_parsed(const Json& schema, const Json& value) {
    if (!value.is_string() || schema_type(schema) == "string") {
        return value;
    }
    auto parsed = py_json_loads(value.get_ref<const std::string&>());
    if (parsed && (parsed->is_null() || parsed->is_array() || parsed->is_object())) {
        return std::move(*parsed);
    }
    return value;
}

std::string validation_text(const std::string& title, const std::vector<ArgIssue>& issues) {
    std::string out = std::to_string(issues.size()) + " validation error" + (issues.size() == 1 ? "" : "s") + " for " + title;
    for (const ArgIssue& i : issues) {
        out += "\n" + i.loc + "\n  " + i.message + " [type=" + i.type + ", input_value=" + i.input +
               ", input_type=" + i.input_type + "]\n    For further information visit " + std::string(kPydanticErrors) + i.type;
    }
    return out;
}

// --- HTTP ---------------------------------------------------------------------------------------

std::string header(const HttpRequest& request, const char* name) {
    const auto it = request.headers.find(name);
    return it != request.headers.end() ? it->second : std::string();
}

std::vector<std::string_view> split_list(std::string_view text) {
    std::vector<std::string_view> out;
    while (!text.empty()) {
        const auto comma = text.find(',');
        std::string_view item = text.substr(0, comma);
        item = item.substr(0, item.find(';'));
        while (item.starts_with(' ')) {
            item.remove_prefix(1);
        }
        while (item.ends_with(' ')) {
            item.remove_suffix(1);
        }
        out.push_back(item);
        text = comma == std::string_view::npos ? std::string_view() : text.substr(comma + 1);
    }
    return out;
}

// allowed_hosts/allowed_origins con «:*»: igual o con ese principio y un puerto cualquiera.
bool allowed(std::string_view value, std::span<const std::string_view> bases) {
    return std::ranges::any_of(bases, [&](std::string_view base) {
        return value.size() > base.size() && value.starts_with(base) && value[base.size()] == ':';
    });
}

HttpResponse plain(int status, std::string text) {
    HttpResponse r;
    r.status = status;
    r.body = std::move(text);
    return r;
}

HttpResponse jsonrpc_error(int status, const std::string& message, const std::optional<std::string>& session,
                           int code = -32600, const Json& id = nullptr) {
    Json error = Json::object();
    error["code"] = code;
    error["message"] = message;
    Json body = Json::object();
    body["jsonrpc"] = "2.0";
    body["id"] = id;
    body["error"] = std::move(error);
    HttpResponse r;
    r.status = status;
    r.content_type = "application/json";
    r.session_id = session;
    r.body = body.dump(-1, ' ', false, Json::error_handler_t::replace);
    return r;
}

Json rpc_result(const Json& id, Json result) {
    Json out = Json::object();
    out["jsonrpc"] = "2.0";
    out["id"] = id;
    out["result"] = std::move(result);
    return out;
}

Json rpc_error(const Json& id, int code, std::string message, const Json& data = Json::value_t::discarded) {
    Json error = Json::object();
    error["code"] = code;
    error["message"] = std::move(message);
    if (!data.is_discarded()) {
        error["data"] = data;
    }
    Json out = Json::object();
    out["jsonrpc"] = "2.0";
    out["id"] = id;
    out["error"] = std::move(error);
    return out;
}

Json invalid_params(const Json& id) {
    return rpc_error(id, -32602, "Invalid request parameters", "");
}

Json tool_text(const Json& id, std::string text, bool is_error) {
    Json item = Json::object();
    item["text"] = std::move(text);
    item["type"] = "text";
    Json result = Json::object();
    result["content"] = Json::array({std::move(item)});
    result["isError"] = is_error;
    return rpc_result(id, std::move(result));
}

const Json* find_tool(std::string_view name) {
    const Json& tools = all_tools();
    const auto it = std::ranges::find_if(tools, [&](const Json& t) { return t["name"].get_ref<const std::string&>() == name; });
    return it != tools.end() ? &*it : nullptr;
}

bool has_tool(std::string_view name) {
    return std::ranges::find(tool_names(), name) != tool_names().end();
}

const std::array<std::string_view, 8> kReadTools{"describe_capabilities", "list_projects",   "resolve_project",
                                                 "get_project_context",   "get_timeline",    "inspect_clip",
                                                 "list_media",            "search_transcript"};

} // namespace

const Json& all_tools() {
    static const Json tools = Json::parse(joined(kToolsJson));
    return tools;
}

const std::vector<std::string>& tool_names() {
    static const std::vector<std::string> names = [] {
        std::vector<std::string> out;
        for (const Json& t : all_tools()) {
            const auto name = t["name"].get<std::string>();
            const auto named = [&](const EditToolInfo& e) { return e.name == name; };
            const bool edit = std::ranges::any_of(edit_tools(), named) || std::ranges::any_of(media_tools(), named);
            if (edit || std::ranges::find(kReadTools, name) != kReadTools.end()) {
                out.push_back(name);
            }
        }
        return out;
    }();
    return names;
}

const std::string& server_instructions() {
    static const std::string text = joined(kInstructions);
    return text;
}

Result<Json> validate_tool_args(const Json& tool, const Json& args) {
    const Json& schema = tool["inputSchema"];
    const Json& properties = schema["properties"];
    const Json required = schema.contains("required") ? schema["required"] : Json::array();
    // Lo que se muestra de un argumento que falta es el diccionario ya con el JSON de los textos.
    Json parsed = Json::object();
    if (args.is_object()) {
        for (const auto& [key, value] : args.items()) {
            parsed[key] = properties.contains(key) ? pre_parsed(properties[key], value) : value;
        }
    }
    Json out = Json::object();
    std::vector<ArgIssue> issues;
    for (const auto& [name, field] : properties.items()) {
        if (parsed.contains(name)) {
            out[name] = coerce(field, parsed[name], name, issues);
        } else if (std::ranges::any_of(required, [&](const Json& r) { return r == name; })) {
            issues.push_back(issue(name, "Field required", "missing", parsed));
        } else {
            out[name] = field.value("default", Json());
        }
    }
    if (!issues.empty()) {
        return fail(ErrorCode::InvalidArgument, validation_text(schema["title"].get<std::string>(), issues));
    }
    return out;
}

McpServer::McpServer(EditToolsEnv edit, RuntimeEnv runtime, Audit* audit)
    : edit_(std::move(edit)), runtime_(std::move(runtime)), audit_(audit), saved_model_(runtime_.transcribe_model) {}

Result<Json> McpServer::run_tool(std::string_view name, const Json& args) {
    const auto text = [&](const char* key) { return args[key].get<std::string>(); };
    if (name == "describe_capabilities") {
        return describe_capabilities(args["domain"], runtime_);
    }
    if (name == "list_projects") {
        return list_projects(edit_.store);
    }
    if (name == "resolve_project") {
        return resolve_project(edit_.store, args["query"]);
    }
    if (name == "get_project_context") {
        return get_project_context(edit_.store, edit_.data_dir, text("project_id"));
    }
    if (name == "get_timeline") {
        return get_timeline(edit_.store, text("project_id"));
    }
    if (name == "inspect_clip") {
        return inspect_clip(edit_.store, text("project_id"), text("clip_id"));
    }
    if (name == "list_media") {
        return list_media(edit_.store, text("project_id"));
    }
    if (name == "search_transcript") {
        return search_transcript(edit_.store, text("project_id"), args["query"], args["limit"]);
    }
    if (std::ranges::any_of(media_tools(), [&](const EditToolInfo& e) { return e.name == name; })) {
        return call_media_tool(edit_, name, args);
    }
    return call_edit_tool(edit_, name, args);
}

Json McpServer::call_tool(const Json& params, const Json& id) {
    if (!params.is_object() || !params.contains("name") || !params["name"].is_string()) {
        return invalid_params(id);
    }
    const Json no_args = Json::object();
    const Json& raw_args = params.contains("arguments") ? params["arguments"] : no_args;
    if (!raw_args.is_object()) {
        return invalid_params(id);
    }
    const auto name = params["name"].get<std::string>();
    const std::string prefix = "Error executing tool " + name;
    if (!has_tool(name)) {
        return tool_text(id, "Unknown tool: " + name, true);
    }
    const Json& tool = *find_tool(name);
    auto args = validate_tool_args(tool, raw_args);
    if (!args) {
        return tool_text(id, prefix + ": " + args.error().message, true);
    }

    // registry._wrap: toda llamada que pasa la validación queda en la auditoría.
    const std::string access = tool["_meta"]["access"].get<std::string>();
    const Json pid = args->contains("project_id") && (*args)["project_id"].is_string() ? (*args)["project_id"] : Json();
    std::vector<std::string> param_keys;
    for (const auto& [key, value] : args->items()) {
        param_keys.push_back(key);
    }
    std::string token;
    if (audit_ != nullptr) {
        token = audit_->begin(name, access, pid, extract_meta(name, *args, saved_model_));
    }
    const auto started = std::chrono::steady_clock::now();
    auto result = run_tool(name, *args);
    const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();

    const auto finish = [&](std::string status, std::optional<std::string> error_text) {
        if (audit_ != nullptr) {
            FinishArgs finish_args;
            finish_args.status = std::move(status);
            finish_args.error = std::move(error_text);
            finish_args.ms = ms;
            finish_args.param_keys = param_keys;
            (void)audit_->finish(token, finish_args);
        }
    };
    if (result) {
        finish("ok", std::nullopt);
        return tool_text(id, result->dump(2, ' ', false, Json::error_handler_t::replace), false);
    }
    const Error& error = result.error();
    if (error.code == ErrorCode::Internal) {
        // Un crash: se apunta y el SDK no le da detalles al cliente.
        finish("error", error.message);
        return tool_text(id, prefix, true);
    }
    std::string payload;
    const bool structured = !error.path.empty() || !error.hint.empty() || error.code == ErrorCode::Unsupported;
    if (structured) {
        McpError mcp_error;
        mcp_error.message = error.message;
        if (error.code == ErrorCode::Unsupported) {
            mcp_error.code = "dependency_error";
        } else {
            const auto [code, retryable] = classify_value_error(error.message);
            mcp_error.code = std::string(code);
            mcp_error.retryable = retryable;
            if (!error.path.empty()) {
                mcp_error.param = error.path;
            }
        }
        if (!error.hint.empty()) {
            mcp_error.hint = error.hint;
        }
        payload = tool_error_text(mcp_error);
    } else {
        payload = tool_error_text(std::string_view(error.message));
    }
    finish("error", std::string(structured ? "MCPError: " : "ValueError: ") + error.message);
    return tool_text(id, prefix + ": " + payload, true);
}

Json McpServer::read_resource_message(const Json& params, const Json& id) {
    if (!params.is_object() || !params.contains("uri") || !params["uri"].is_string()) {
        return invalid_params(id);
    }
    const auto uri = params["uri"].get<std::string>();
    Json data = Json::object();
    data["uri"] = uri;
    const auto match = match_resource(uri);
    if (!match) {
        return rpc_error(id, -32602, "Unknown resource: " + uri, data);
    }
    // _load_history falla con un historial que no se puede leer como tal, y el resource con él.
    if (match->first == "project://{pid}" && !history_summary_file(history_path(edit_.data_dir, match->second["pid"].get<std::string>()))) {
        return rpc_error(id, -32603, "Error creating resource from template " + uri, data);
    }
    const ResourceContext ctx{
        .env = runtime_,
        .project = [&](std::string_view pid) -> std::optional<Project> {
            auto p = edit_.store.get_project(pid);
            return p ? std::move(*p) : std::nullopt;
        },
        .history = [&](std::string_view pid) { return load_history(history_path(edit_.data_dir, pid)); },
    };
    auto text = read_resource(match->first, match->second, ctx);
    if (!text) {
        return rpc_error(id, -32603, "Error creating resource from template " + uri, data);
    }
    const auto catalog = resource_catalog();
    const auto info = std::ranges::find_if(catalog, [&](const ResourceInfo& r) { return r.uri == match->first; });
    Json item = Json::object();
    item["mimeType"] = info != catalog.end() && info->mime_type ? std::string(*info->mime_type) : std::string("text/plain");
    item["text"] = std::move(*text);
    item["uri"] = uri;
    Json result = Json::object();
    result["contents"] = Json::array({std::move(item)});
    return rpc_result(id, std::move(result));
}

Json McpServer::handle_message(const Json& message) {
    const Json& id = message["id"];
    const auto method = message["method"].get<std::string>();
    const Json no_params = nullptr;
    const Json& params = message.contains("params") ? message["params"] : no_params;
    const auto list_result = [&](const char* key, Json items) {
        Json result = Json::object();
        result[key] = std::move(items);
        return rpc_result(id, std::move(result));
    };
    if (method == "ping") {
        return rpc_result(id, Json::object());
    }
    if (method == "initialize") {
        const bool valid = params.is_object() && params.contains("protocolVersion") && params["protocolVersion"].is_string() &&
                           params.contains("capabilities") && params["capabilities"].is_object() &&
                           params.contains("clientInfo") && params["clientInfo"].is_object();
        if (!valid) {
            return invalid_params(id);
        }
        const auto wanted = params["protocolVersion"].get<std::string>();
        const bool known = std::ranges::find(kProtocolVersions, wanted) != kProtocolVersions.end();
        const auto capabilities = Json::parse(
            R"({"experimental":{},"prompts":{"listChanged":false},"resources":{"listChanged":false,"subscribe":false},"tools":{"listChanged":false}})");
        Json info = Json::object();
        info["name"] = "video-yt";
        info["version"] = "";
        Json result = Json::object();
        result["capabilities"] = capabilities;
        result["instructions"] = server_instructions();
        result["protocolVersion"] = known ? wanted : std::string(kProtocolVersions.back());
        result["serverInfo"] = std::move(info);
        return rpc_result(id, std::move(result));
    }
    if (method == "tools/list") {
        Json tools = Json::array();
        for (const Json& t : all_tools()) {
            if (has_tool(t["name"].get<std::string>())) {
                tools.push_back(t);
            }
        }
        return list_result("tools", std::move(tools));
    }
    if (method == "tools/call") {
        return call_tool(params, id);
    }
    if (method == "resources/list") {
        return list_result("resources", Json::parse(joined(kResourcesJson)));
    }
    if (method == "resources/templates/list") {
        return list_result("resourceTemplates", Json::parse(joined(kTemplatesJson)));
    }
    if (method == "resources/read") {
        return read_resource_message(params, id);
    }
    if (method == "prompts/list") {
        return list_result("prompts", Json::array());
    }
    if (method == "prompts/get") {
        if (!params.is_object() || !params.contains("name") || !params["name"].is_string()) {
            return invalid_params(id);
        }
        return rpc_error(id, 0, "Unknown prompt: " + params["name"].get<std::string>());
    }
    return rpc_error(id, -32601, "Method not found", method);
}

HttpResponse McpServer::handle_post(const HttpRequest& request, const std::optional<std::string>& session) {
    const std::string accept = header(request, "accept");
    const auto accepts = split_list(accept);
    const auto any_of = [&](std::initializer_list<std::string_view> types) {
        return std::ranges::any_of(accepts, [&](std::string_view a) { return std::ranges::find(types, a) != types.end(); });
    };
    if (!any_of({"application/json", "application/*", "*/*"}) || !any_of({"text/event-stream", "text/*", "*/*"})) {
        return jsonrpc_error(406, "Not Acceptable: Client must accept both application/json and text/event-stream", session);
    }
    const std::string content_type = header(request, "content-type");
    const auto content_types = split_list(content_type);
    if (std::ranges::find(content_types, "application/json") == content_types.end()) {
        return jsonrpc_error(415, "Unsupported Media Type: Content-Type must be application/json", session);
    }
    const Json message = Json::parse(request.body, nullptr, false);
    if (message.is_discarded()) {
        return jsonrpc_error(400, "Parse error: el cuerpo no es JSON", session, -32700);
    }
    // El JSON-RPC 2.0: una petición o aviso (method) o una respuesta (id con result o error).
    const bool object = message.is_object() && message.value("jsonrpc", Json()) == "2.0";
    const bool has_method = object && message.contains("method") && message["method"].is_string();
    const bool is_response = object && message.contains("id") && (message.contains("result") || message.contains("error"));
    if (!has_method && !is_response) {
        return jsonrpc_error(400, "Validation error: no es un mensaje JSON-RPC 2.0", session, -32602);
    }
    const bool has_id = has_method && message.contains("id") && (message["id"].is_number_integer() || message["id"].is_string());
    const bool initialize = has_id && message["method"] == "initialize";

    std::optional<std::string> sid = session;
    if (!sid) {
        if (!initialize) {
            return jsonrpc_error(400, "Bad Request: Missing session ID", std::nullopt);
        }
        sid = edit_.ids.hex32();
        sessions_[*sid] = false;
    }
    HttpResponse out;
    out.session_id = std::move(sid);
    out.content_type = "application/json";
    if (!has_id) {
        out.status = 202; // un aviso o una respuesta: nada que contestar
        return out;
    }
    out.content_type = "text/event-stream";
    out.body = "event: message\r\ndata: " + handle_message(message).dump(-1, ' ', false, Json::error_handler_t::replace) + "\r\n\r\n";
    return out;
}

HttpResponse McpServer::handle_http(const HttpRequest& request) {
    const std::scoped_lock lock(mutex_);
    // TransportSecurityMiddleware: un sitio web no debe poder llegar al servidor local.
    static constexpr std::array<std::string_view, 3> kHosts{"127.0.0.1", "localhost", "[::1]"};
    static constexpr std::array<std::string_view, 3> kOrigins{"http://127.0.0.1", "http://localhost", "http://[::1]"};
    if (request.method == "POST") {
        const std::string content_type = detail::py_lower(header(request, "content-type"));
        if (!content_type.starts_with("application/json")) {
            return plain(400, "Invalid Content-Type header");
        }
    }
    if (!allowed(header(request, "host"), kHosts)) {
        return plain(421, "Invalid Host header");
    }
    if (const std::string origin = header(request, "origin"); !origin.empty() && !allowed(origin, kOrigins)) {
        return plain(403, "Invalid Origin header");
    }
    if (request.method == "POST") {
        const std::string version = header(request, "mcp-protocol-version");
        if (!version.empty() && std::ranges::find(kProtocolVersions, version) == kProtocolVersions.end()) {
            const Json message = Json::parse(request.body, nullptr, false);
            const Json id = message.is_object() && message.contains("id") ? message["id"] : Json();
            return jsonrpc_error(
                400,
                "params._meta must be an object carrying the required 'io.modelcontextprotocol/protocolVersion' and "
                "'io.modelcontextprotocol/clientCapabilities' envelope keys",
                std::nullopt, -32602, id);
        }
    }

    std::optional<std::string> session;
    if (const std::string given = header(request, "mcp-session-id"); !given.empty()) {
        const auto it = sessions_.find(given);
        if (it == sessions_.end()) {
            return jsonrpc_error(404, "Session not found", std::nullopt);
        }
        if (it->second) {
            return jsonrpc_error(404, "Not Found: Session has been terminated", given);
        }
        session = given;
    }
    if (request.method == "POST") {
        return handle_post(request, session);
    }
    if (request.method == "DELETE") {
        if (!session) {
            return jsonrpc_error(400, "Bad Request: Missing session ID", std::nullopt);
        }
        sessions_[*session] = true;
        HttpResponse out;
        out.content_type = "application/json";
        out.session_id = std::move(session);
        return out;
    }
    if (request.method == "GET") {
        const std::string accept = header(request, "accept");
        const auto accepts = split_list(accept);
        const bool sse = std::ranges::any_of(accepts, [](std::string_view a) {
            return a == "text/event-stream" || a == "text/*" || a == "*/*";
        });
        if (!sse) {
            return jsonrpc_error(406, "Not Acceptable: Client must accept text/event-stream", session);
        }
    }
    return jsonrpc_error(405, "Method Not Allowed", session);
}

} // namespace gc::core::mcp
