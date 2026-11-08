#pragma once

// Las tools de LECTURA del servidor MCP (espejo de tools_capabilities.py, tools_context.py y
// tools_read.py de backend/app/mcp_server en video-yt): describe_capabilities, list_projects,
// resolve_project, get_project_context, get_timeline, inspect_clip, list_media y
// search_transcript. Leen projects.json con el ProjectsStore y el historial de
// <data>/history/<pid>.json; los DTO salen de mcp_dto.hpp y mcp_capabilities.hpp.
//
// Errores: lo que en Python es un ValueError (llega al agente como ToolError: su texto va por
// tool_error_text de mcp_registry.hpp) sale con ErrorCode::NotFound o InvalidArgument y el mismo
// mensaje; un crash de Python (TypeError, AttributeError, OverflowError…), que el servidor oculta
// al agente, sale con ErrorCode::Internal.
//
// Los argumentos que en Python pueden llegar de cualquier tipo (query, limit) van como Json: un
// null es el None de Python.

#include <filesystem>
#include <string_view>

#include "gc/core/error.hpp"
#include "gc/core/json.hpp"
#include "gc/core/mcp_capabilities.hpp"
#include "gc/core/projects_store.hpp"

namespace gc::core::mcp {

// Sin dominio (null o vacío), overview(); con él, describe(dominio).
[[nodiscard]] Result<Json> describe_capabilities(const Json& domain, const RuntimeEnv& env);

// {"count", "projects": [project_summary…]} en el orden de projects.json.
[[nodiscard]] Result<Json> list_projects(const ProjectsStore& store);

// Por id exacto; si no, por nombre exacto (sin espacios alrededor y en minúsculas), prefijo de
// id y nombre que contiene la consulta, en ese orden: el primer grupo con algún proyecto (sin
// ids repetidos) decide. Uno: {project_id, name, match}; varios: {ambiguous, candidates (10 como
// mucho), note}; ninguno: «Ningún proyecto coincide con: <query>».
[[nodiscard]] Result<Json> resolve_project(const ProjectsStore& store, const Json& query);

// _history_summary sobre el historial ya leído: {can_undo, can_redo, checkpoints} con el _norm de
// timeline_history.py, que acepta lo que `list()` y `dict()` de Python acepten (un texto, las
// claves de un dict, una lista de pares…) y falla como ellos.
[[nodiscard]] Result<Json> history_summary(const Json& raw_history);
// Igual, desde el archivo: sin archivo o ilegible (_load_history), un historial vacío.
[[nodiscard]] Result<Json> history_summary_file(const std::filesystem::path& file);

// project_context del proyecto con el resumen de <data_dir>/history/<pid>.json.
[[nodiscard]] Result<Json> get_project_context(const ProjectsStore& store, const std::filesystem::path& data_dir,
                                               std::string_view project_id);

[[nodiscard]] Result<Json> get_timeline(const ProjectsStore& store, std::string_view project_id);
[[nodiscard]] Result<Json> inspect_clip(const ProjectsStore& store, std::string_view project_id,
                                        std::string_view clip_id);
[[nodiscard]] Result<Json> list_media(const ProjectsStore& store, std::string_view project_id);

// Busca `query` (sin espacios alrededor y en minúsculas) en los segmentos de las transcripciones
// del proyecto y de cada clip; devuelve las primeras max(1, int(limit)), con el int() de Python.
[[nodiscard]] Result<Json> search_transcript(const ProjectsStore& store, std::string_view project_id,
                                             const Json& query, const Json& limit = 20);

} // namespace gc::core::mcp
