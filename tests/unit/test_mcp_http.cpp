// El servidor MCP por un socket de verdad: gc_agent con cpp-httplib. Lo que responde a cada petición
// lo comprueba la paridad (test_mcp_server_parity.cpp); aquí, que llega por la red.

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include <httplib.h>

#include "gc/agent/mcp_http_server.hpp"
#include "gc/core/ids.hpp"
#include "gc/core/mcp_server.hpp"
#include "gc/core/projects_store.hpp"

using gc::Json;
namespace mcp = gc::core::mcp;

TEST_CASE("servidor MCP: initialize y una tool por un socket", "[mcp][agent]") {
    const auto tmp = std::filesystem::temp_directory_path() / "gc_mcp_http_test";
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(tmp);
    std::ofstream(tmp / "projects.json", std::ios::binary)
        << R"({"projects": [{"id": "p1", "name": "Uno", "created_at": "2026-01-01T00:00:00+00:00"}]})";
    gc::core::ProjectsStore store(tmp / "projects.json", tmp / "clips");
    gc::core::RandomIdSource ids;
    mcp::McpServer server(mcp::EditToolsEnv{.store = store, .data_dir = tmp, .ids = ids}, mcp::RuntimeEnv{});
    gc::agent::McpHttpServer http(server);
    const auto port = http.bind("127.0.0.1", 0);
    REQUIRE(port);
    std::thread thread([&] { http.serve(); });

    httplib::Client client("127.0.0.1", *port);
    const httplib::Headers accept{{"Accept", "application/json, text/event-stream"}};
    const auto init = client.Post(
        "/mcp", accept,
        R"({"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {"protocolVersion": "2025-06-18", "capabilities": {}, "clientInfo": {"name": "t", "version": "1"}}})",
        "application/json");
    REQUIRE(init);
    CHECK(init->status == 200);
    CHECK(init->get_header_value("Content-Type") == "text/event-stream");
    const std::string session = init->get_header_value("Mcp-Session-Id");
    CHECK(session.size() == 32);

    httplib::Headers with_session = accept;
    with_session.emplace("Mcp-Session-Id", session);
    const auto call = client.Post("/mcp/", with_session,
                                  R"({"jsonrpc": "2.0", "id": 2, "method": "tools/call", "params": {"name": "list_projects", "arguments": {}}})",
                                  "application/json");
    REQUIRE(call);
    CHECK(call->status == 200);
    CHECK(call->body.contains("project_id"));
    CHECK(call->body.contains("p1"));

    // Un sitio web no debe llegar al servidor local.
    httplib::Headers evil = with_session;
    evil.emplace("Origin", "http://evil.com");
    const auto rejected = client.Post("/mcp", evil, R"({"jsonrpc": "2.0", "id": 3, "method": "ping"})", "application/json");
    REQUIRE(rejected);
    CHECK(rejected->status == 403);

    // Lo que no cabe se rechaza antes de leerlo.
    const auto big = client.Post("/mcp", with_session, std::string(mcp::kMaxRequestBody + 1, 'x'), "application/json");
    REQUIRE(big);
    CHECK(big->status == 413);

    const auto closed = client.Delete("/mcp", with_session);
    REQUIRE(closed);
    CHECK(closed->status == 200);

    http.stop();
    thread.join();
    std::error_code ec;
    std::filesystem::remove_all(tmp, ec);
}
