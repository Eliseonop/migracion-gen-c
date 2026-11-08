#include "gc/agent/mcp_http_server.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <utility>

#include <httplib.h>

namespace gc::agent {

namespace {

std::string lower(std::string text) {
    std::ranges::transform(text, text.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

} // namespace

struct McpHttpServer::Impl {
    explicit Impl(core::mcp::McpServer& target) : server(target) {}

    void attend(const httplib::Request& req, httplib::Response& res) const {
        core::mcp::HttpRequest request;
        request.method = req.method;
        request.body = req.body;
        for (const auto& [name, value] : req.headers) {
            request.headers[lower(name)] = value;
        }
        const core::mcp::HttpResponse out = server.handle_http(request);
        res.status = out.status;
        if (out.session_id) {
            res.set_header("Mcp-Session-Id", *out.session_id);
        }
        res.set_content(out.body, out.content_type ? *out.content_type : std::string("text/plain; charset=utf-8"));
    }

    core::mcp::McpServer& server;
    httplib::Server http;
};

McpHttpServer::McpHttpServer(core::mcp::McpServer& server) : impl_(std::make_unique<Impl>(server)) {
    impl_->http.set_payload_max_length(core::mcp::kMaxRequestBody);
    const auto attend = [impl = impl_.get()](const httplib::Request& req, httplib::Response& res) { impl->attend(req, res); };
    for (const char* path : {"/mcp", "/mcp/"}) {
        impl_->http.Post(path, attend);
        impl_->http.Get(path, attend);
        impl_->http.Delete(path, attend);
        impl_->http.Put(path, attend);
        impl_->http.Patch(path, attend);
        impl_->http.Options(path, attend);
    }
}

McpHttpServer::~McpHttpServer() = default;

Result<int> McpHttpServer::bind(const std::string& host, int port) {
    if (port == 0) {
        const int chosen = impl_->http.bind_to_any_port(host);
        if (chosen < 0) {
            return fail(ErrorCode::Io, "No se pudo abrir un puerto en " + host);
        }
        return chosen;
    }
    if (!impl_->http.bind_to_port(host, port)) {
        return fail(ErrorCode::Io, "No se pudo abrir " + host + ":" + std::to_string(port) + " (¿está ya en uso?)");
    }
    return port;
}

void McpHttpServer::serve() {
    impl_->http.listen_after_bind();
}

void McpHttpServer::stop() {
    impl_->http.stop();
}

} // namespace gc::agent
