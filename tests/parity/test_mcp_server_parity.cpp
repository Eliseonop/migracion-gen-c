// Paridad del servidor MCP por HTTP (gc/core/mcp_server.hpp) con el servidor real de video-yt.
//
// tests/fixtures/mcp/wire.json (export_fixtures.py --solo mcp, con los casos de mcp_cases.py): las
// tools de tools/list, la validación de los argumentos y lo que responde el SDK de MCP a cada
// petición, por HTTP y sin red, sobre un data/ temporal.

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <map>
#include <algorithm>
#include <cctype>
#include <regex>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "gc/core/history_store.hpp"
#include "gc/core/ids.hpp"
#include "gc/core/io.hpp"
#include "gc/core/mcp_audit.hpp"
#include "gc/core/mcp_capabilities.hpp"
#include "gc/core/mcp_server.hpp"
#include "gc/core/projects_store.hpp"

#include "json_diff.hpp"

using gc::Json;
namespace mcp = gc::core::mcp;

namespace {

Json load_wire() {
    const auto path = gc::core::path_from_utf8(GC_FIXTURES_DIR) / "mcp" / "wire.json";
    REQUIRE(std::filesystem::exists(path)); // faltan: export_fixtures.py --solo mcp
    auto file = gc::core::read_json_file(path);
    REQUIRE(file);
    return std::move(*file);
}

void same(const Json& expected, const Json& actual, const std::string& what) {
    const auto diff = gc::test::json_diff(expected, actual);
    INFO(what);
    INFO((diff ? *diff : std::string()));
    CHECK_FALSE(diff);
}

// Los ids de pistas y marcadores son aleatorios en los dos lados: cada uno pasa a ser el orden en
// que aparece.
std::string normal_ids(const std::string& text) {
    static const std::regex kId("\"[A-Za-z][0-9a-f]{8}\"");
    std::map<std::string, std::string> seen;
    std::string out;
    auto last = std::sregex_iterator(text.begin(), text.end(), kId);
    std::size_t pos = 0;
    for (; last != std::sregex_iterator(); ++last) {
        out += text.substr(pos, static_cast<std::size_t>(last->position()) - pos);
        auto [it, added] = seen.try_emplace(last->str(), "\"id" + std::to_string(seen.size()) + "\"");
        out += it->second;
        pos = static_cast<std::size_t>(last->position() + last->length());
    }
    return out + text.substr(pos);
}

// El texto de una tool: si es JSON se comparan los valores (el orden de los decimales no importa)
// y, si no, el texto entero.
Json normal_message(Json message) {
    if (message.is_object() && message.contains("result") && message["result"].contains("contents")) {
        for (Json& item : message["result"]["contents"]) {
            item["text"] = normal_ids(item["text"].get<std::string>());
        }
    }
    if (message.is_object() && message.contains("result") && message["result"].contains("content")) {
        for (Json& item : message["result"]["content"]) {
            const auto text = item["text"].get<std::string>();
            auto parsed = gc::core::py_json_loads(normal_ids(text));
            if (parsed) {
                item["text"] = std::move(*parsed);
            }
        }
    }
    return message;
}

std::vector<Json> sse_messages(const std::string& body) {
    std::vector<Json> out;
    std::size_t pos = 0;
    while ((pos = body.find("data: ", pos)) != std::string::npos) {
        const auto end = body.find("\r\n", pos);
        out.push_back(Json::parse(body.substr(pos + 6, end - pos - 6)));
        pos = end;
    }
    return out;
}

// Los mensajes de error del propio Pydantic sobre el cuerpo (JSON roto, no es JSON-RPC) no se copian.
bool loose_message(const std::string& name) {
    static const std::vector<std::string> names{"JSON roto", "cuerpo vacío", "no es un objeto", "lista", "lista vacía",
                                                "sin jsonrpc", "jsonrpc viejo", "sin method"};
    return std::ranges::find(names, name) != names.end();
}

} // namespace

TEST_CASE("paridad MCP: los argumentos de las tools (Pydantic en modo laxo)", "[parity][mcp]") {
    const Json f = load_wire();
    const Json& real = f["tools"];
    REQUIRE(real.size() == mcp::all_tools().size());
    for (std::size_t i = 0; i < real.size(); ++i) {
        same(real[i], mcp::all_tools()[i], "tools/list " + real[i]["name"].get<std::string>());
    }
    CHECK(mcp::server_instructions() == f["instructions"].get<std::string>());

    std::size_t errors = 0;
    for (const Json& c : f["matrix"]) {
        const auto tool = c["tool"].get<std::string>();
        const Json* entry = nullptr;
        for (const Json& t : mcp::all_tools()) {
            if (t["name"] == tool) {
                entry = &t;
            }
        }
        REQUIRE(entry != nullptr);
        const auto r = mcp::validate_tool_args(*entry, c["args"]);
        const std::string what = tool + c["args"].dump();
        INFO(what);
        if (c.contains("error")) {
            ++errors;
            REQUIRE_FALSE(r);
            CHECK(r.error().code == gc::ErrorCode::InvalidArgument);
            CHECK(r.error().message == c["error"].get<std::string>());
        } else {
            if (!r) {
                INFO(r.error().message);
                REQUIRE(r);
            }
            same(c["coerced"], *r, what);
        }
    }
    CHECK(errors > 80);
}

TEST_CASE("paridad MCP: el servidor por HTTP (Streamable HTTP y JSON-RPC)", "[parity][mcp]") {
    const Json f = load_wire();
    const auto tmp = std::filesystem::temp_directory_path() / "gc_mcp_wire_parity";
    std::filesystem::remove_all(tmp);
    const auto data = tmp / "data";
    std::filesystem::create_directories(data / "history");
    {
        auto text = gc::core::py_json_dumps(Json{{"projects", f["projects"]}});
        REQUIRE(text);
        std::ofstream(data / "projects.json", std::ios::binary) << *text;
    }
    for (const auto& [pid, text] : f["histories"].items()) {
        std::ofstream(gc::core::history_path(data, pid), std::ios::binary) << text.get<std::string>();
    }
    gc::core::ProjectsStore store(data / "projects.json", tmp / "clips");
    gc::core::RandomIdSource ids;
    mcp::RuntimeEnv env;
    const Json& e = f["environment"];
    env.transcribe_model = e["transcribe_model"].get<std::string>();
    env.tts_engines = e["tts_engines"];
    env.sfx_categories = e["sfx_categories"].get<std::vector<std::string>>();
    env.ffmpeg = e["ffmpeg"].get<bool>();
    env.ffprobe = e["ffprobe"].get<bool>();
    env.data_dir = e["data_dir"].get<std::string>();
    env.gpu = e["gpu"];
    env.ai_provider = e["ai_provider"];

    mcp::Audit audit(data);
    mcp::McpServer server(mcp::EditToolsEnv{.store = store, .data_dir = data, .ids = ids}, env, &audit);

    std::string current;
    std::string gone;
    for (const Json& c : f["steps"]) {
        const auto name = c["name"].get<std::string>();
        INFO(name);
        mcp::HttpRequest request;
        request.method = c["method"].get<std::string>();
        request.body = c["body"].get<std::string>();
        request.headers = {{"host", "127.0.0.1:8000"},
                           {"accept", "application/json, text/event-stream"},
                           {"content-type", "application/json"}};
        const auto which = c["session"].get<std::string>();
        const std::string sid = which == "cur" ? current : which == "gone" ? gone : which == "none" ? "" : c["literal"].get<std::string>();
        if (!sid.empty()) {
            request.headers["mcp-session-id"] = sid;
        }
        for (const auto& [key, value] : c["headers"].items()) {
            std::string lower = key;
            std::ranges::transform(lower, lower.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            if (value.is_null()) {
                request.headers.erase(lower);
            } else {
                request.headers[lower] = value.get<std::string>();
            }
        }

        const auto r = server.handle_http(request);
        CHECK(r.status == c["status"].get<int>());
        const Json& expected_type = c["content_type"];
        CHECK(Json(r.content_type ? Json(*r.content_type) : Json()) == expected_type);
        const Json& marked = c["session_header"];
        if (marked == "$SID") {
            REQUIRE(r.session_id);
            if (name == "initialize") {
                current = *r.session_id;
            }
            CHECK(*r.session_id == current);
        } else if (marked.is_null()) {
            CHECK_FALSE(r.session_id);
        }
        if (name == "DELETE") {
            gone = current;
        }

        const auto kind = c["kind"].get<std::string>();
        if (kind == "text") {
            CHECK(r.body == c["response"].get<std::string>());
        } else if (kind == "sse") {
            const auto messages = sse_messages(r.body);
            REQUIRE(messages.size() == c["response"].size());
            for (std::size_t i = 0; i < messages.size(); ++i) {
                Json expected = c["response"][i];
                if (c.contains("tools_list")) {
                    // Solo las tools hechas, en su orden.
                    Json kept = Json::array();
                    for (const Json& t : expected["result"]["tools"]) {
                        if (std::ranges::find(mcp::tool_names(), t["name"].get<std::string>()) != mcp::tool_names().end()) {
                            kept.push_back(t);
                        }
                    }
                    expected["result"]["tools"] = kept;
                    CHECK(kept.size() == mcp::tool_names().size());
                    // El orden de las claves del esquema no cambia lo que significa.
                    CHECK(nlohmann::json::parse(expected.dump()) == nlohmann::json::parse(messages[i].dump()));
                    continue;
                }
                same(normal_message(expected), normal_message(messages[i]), name);
            }
        } else if (c["response"].is_null()) {
            CHECK(r.body.empty());
        } else if (loose_message(name)) {
            const Json got = Json::parse(r.body);
            CHECK(got["error"]["code"] == c["response"]["error"]["code"]);
            CHECK(got["id"].is_null());
        } else {
            same(c["response"], Json::parse(r.body), name);
        }
    }

    // La auditoría: las mismas entradas (sin la hora, lo que tardó ni el texto de los crashes).
    const auto entries = audit.read_all();
    REQUIRE(entries);
    REQUIRE(entries->size() == f["audit"].size());
    for (std::size_t i = 0; i < entries->size(); ++i) {
        Json expected = f["audit"][i];
        Json actual = (*entries)[i];
        for (Json* entry : {&expected, &actual}) {
            entry->erase("ts");
            entry->erase("ms");
            auto& error = *entry;
            if (error.contains("error") && error["error"].is_string() &&
                !error["error"].get<std::string>().starts_with("ValueError") && !error["error"].get<std::string>().starts_with("MCPError")) {
                error["error"] = "crash";
            }
        }
        same(expected, normal_message(actual), "auditoría " + std::to_string(i));
    }
    std::error_code ec;
    std::filesystem::remove_all(tmp, ec);
}
