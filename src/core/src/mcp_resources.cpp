#include "gc/core/mcp_resources.hpp"

#include <array>
#include <vector>

#include "gc/core/io.hpp"
#include "gc/core/mcp_dto.hpp"
#include "gc/core/mcp_help.hpp"

#include "py.hpp"

namespace gc::core::mcp {

namespace {

constexpr std::string_view kJson = "application/json";

constexpr std::array<ResourceInfo, 7> kResources{{
    {"capabilities://index", kJson, "Dominios, verbos y defaults del editor (descubrimiento)."},
    {"help://{domain}", std::nullopt, "Guía de uso de un dominio."},
    {"config://runtime", kJson, "GPU, proveedor de IA, binarios, defaults y rutas."},
    {"project://{pid}", kJson, "Resumen del proyecto (formato, material, timeline, historial)."},
    {"project://{pid}/timeline", kJson, "Timeline en detalle (pistas y clips, sin words/keyframes)."},
    {"project://{pid}/clip/{cid}", kJson, "Un clip de la timeline en detalle COMPLETO."},
    {"project://{pid}/media", kJson, "Inventario de material del proyecto."},
}};

Result<std::string> dumps(const Json& data) {
    return py_json_dumps(data);
}

std::string param(const Json& params, std::string_view key) {
    if (!params.is_object()) {
        return {};
    }
    const auto it = params.find(key);
    return it != params.end() && it->is_string() ? it->get<std::string>() : std::string();
}

Result<Project> project_or_raise(const ResourceContext& ctx, const std::string& pid) {
    auto p = ctx.project ? ctx.project(pid) : std::nullopt;
    if (!p) {
        return fail(ErrorCode::NotFound, "Proyecto no encontrado: " + pid);
    }
    return std::move(*p);
}

// La plantilla encaja con la URI: lo literal igual y cada {nombre}, un trozo no vacío sin «/».
std::optional<Json> match(std::string_view tpl, std::string_view uri) {
    Json params = Json::object();
    std::size_t t = 0;
    std::size_t u = 0;
    while (t < tpl.size()) {
        if (tpl[t] == '{') {
            const std::size_t close = tpl.find('}', t);
            const std::string_view name = tpl.substr(t + 1, close - t - 1);
            std::size_t end = u;
            while (end < uri.size() && uri[end] != '/') {
                ++end;
            }
            if (end == u) {
                return std::nullopt;
            }
            params[std::string(name)] = std::string(uri.substr(u, end - u));
            u = end;
            t = close + 1;
        } else {
            if (u >= uri.size() || uri[u] != tpl[t]) {
                return std::nullopt;
            }
            ++t;
            ++u;
        }
    }
    if (u != uri.size()) {
        return std::nullopt;
    }
    return params;
}

} // namespace

std::span<const ResourceInfo> resource_catalog() {
    return kResources;
}

std::optional<std::pair<std::string_view, Json>> match_resource(std::string_view uri) {
    for (const ResourceInfo& r : kResources) {
        if (auto params = match(r.uri, uri)) {
            return std::pair<std::string_view, Json>{r.uri, std::move(*params)};
        }
    }
    return std::nullopt;
}

Json history_summary(const History& history) {
    return {{"can_undo", history.can_undo()}, {"can_redo", history.can_redo()}, {"checkpoints", history.checkpoints()}};
}

Result<std::string> read_resource(std::string_view uri, const Json& params, const ResourceContext& ctx) {
    if (uri == "capabilities://index") {
        return dumps(overview(ctx.env));
    }
    if (uri == "config://runtime") {
        return dumps(runtime_info(ctx.env));
    }
    if (uri == "help://{domain}") {
        const std::string domain = param(params, "domain");
        if (const auto text = guide(domain)) {
            return std::string(*text);
        }
        return fail(ErrorCode::NotFound,
                    "Dominio sin guía: " + domain + ". Válidos: " + detail::py_repr(Json(help_domains())));
    }
    const std::string pid = param(params, "pid");
    if (uri == "project://{pid}") {
        auto p = project_or_raise(ctx, pid);
        if (!p) {
            return std::unexpected(p.error());
        }
        const Json history = history_summary(ctx.history ? ctx.history(pid) : History());
        return dumps(project_context(*p, history));
    }
    if (uri == "project://{pid}/timeline") {
        auto p = project_or_raise(ctx, pid);
        if (!p) {
            return std::unexpected(p.error());
        }
        auto detail = timeline_detail(p->timeline ? &*p->timeline : nullptr);
        if (!detail) {
            return std::unexpected(detail.error());
        }
        return dumps(*detail);
    }
    if (uri == "project://{pid}/clip/{cid}") {
        auto p = project_or_raise(ctx, pid);
        if (!p) {
            return std::unexpected(p.error());
        }
        const std::string cid = param(params, "cid");
        if (p->timeline) {
            for (const auto& c : p->timeline->clips) {
                if (c->id == cid) {
                    return dumps(clip_detail(*c));
                }
            }
        }
        return fail(ErrorCode::NotFound, "Clip no encontrado en la timeline: " + cid);
    }
    if (uri == "project://{pid}/media") {
        auto p = project_or_raise(ctx, pid);
        if (!p) {
            return std::unexpected(p.error());
        }
        return dumps(media_list(*p));
    }
    return fail(ErrorCode::NotFound, "Resource desconocido: " + std::string(uri));
}

} // namespace gc::core::mcp
