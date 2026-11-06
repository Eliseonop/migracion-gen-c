#pragma once

// Los resources del servidor MCP (espejo de backend/app/mcp_server/resources.py de video-yt):
// estado de solo lectura que el cliente lee bajo demanda.
//
//   capabilities://index        dominios, verbos y valores por defecto (overview)
//   help://{domain}             la guía larga de un dominio
//   config://runtime            GPU, proveedor de IA, binarios, valores por defecto y rutas
//   project://{pid}             resumen del proyecto (formato, material, timeline, historial)
//   project://{pid}/timeline    la timeline en detalle (sin words ni keyframes)
//   project://{pid}/clip/{cid}  un clip de la timeline COMPLETO
//   project://{pid}/media       el inventario del material
//
// Cada uno es un texto: el JSON de json.dumps(ensure_ascii=False), o la guía tal cual.

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "gc/core/error.hpp"
#include "gc/core/json.hpp"
#include "gc/core/mcp_capabilities.hpp"
#include "gc/core/model.hpp"
#include "gc/core/session.hpp"

namespace gc::core::mcp {

struct ResourceInfo {
    std::string_view uri; // la plantilla, con {parámetros}
    std::optional<std::string_view> mime_type;
    std::string_view description;
};

// Los siete resources, en el orden en que se registran.
[[nodiscard]] std::span<const ResourceInfo> resource_catalog();

// El resource de una URI concreta y sus parámetros, como las plantillas del SDK de MCP: cada
// {parámetro} es un trozo sin «/» que no está vacío. Nada si no encaja con ninguna.
[[nodiscard]] std::optional<std::pair<std::string_view, Json>> match_resource(std::string_view uri);

// resources._history: el resumen del historial de un proyecto ({can_undo, can_redo, checkpoints}).
[[nodiscard]] Json history_summary(const History& history);

// Lo que leen los resources: la máquina y los ajustes, los proyectos (projects.get_project;
// nada si no existe) y su historial (timeline_store._load_history).
struct ResourceContext {
    RuntimeEnv env;
    std::function<std::optional<Project>(std::string_view pid)> project;
    std::function<History(std::string_view pid)> history;
};

// Lee un resource por su plantilla y sus parámetros ({"pid": …, "cid": …, "domain": …}). Error
// con el texto del ValueError de Python («Proyecto no encontrado: x», «Clip no encontrado en la
// timeline: x», «Dominio sin guía: x. Válidos: [...]»), o Internal donde Python falla con otra
// excepción (los beats que no tienen len()).
[[nodiscard]] Result<std::string> read_resource(std::string_view uri, const Json& params, const ResourceContext& ctx);

} // namespace gc::core::mcp
