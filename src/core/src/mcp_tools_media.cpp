#include "gc/core/mcp_tools_media.hpp"

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

#include "gc/core/mcp_registry.hpp"
#include "gc/core/media_upgrade.hpp"
#include "gc/core/projects_store.hpp"
#include "gc/core/settings.hpp"
#include "gc/core/sfx_disk.hpp"
#include "json_reader.hpp"
#include "py.hpp"

namespace gc::core::mcp {

namespace {

using detail::py_int;
using detail::py_str;
using detail::py_strip;
using detail::py_truthy;

constexpr std::int64_t kSfxDefaultLimit = 50;
constexpr std::int64_t kSfxMaxLimit = 300;

std::unexpected<Error> value_error(std::string message, std::string param = {}) {
    const bool not_found = classify_value_error(message).first == "resource_not_found";
    return fail(not_found ? ErrorCode::NotFound : ErrorCode::InvalidArgument, std::move(message), std::move(param));
}

const Json& arg(const Json& args, const char* name) {
    static const Json none;
    if (args.is_object()) {
        const auto it = args.find(name);
        if (it != args.end()) {
            return *it;
        }
    }
    return none;
}

std::string text_arg(const Json& args, const char* name, std::string fallback = {}) {
    const Json& v = arg(args, name);
    return v.is_null() ? fallback : py_str(v);
}

// _project_or_raise.
Result<Project> project_or_raise(const EditToolsEnv& env, const std::string& pid) {
    auto loaded = env.store.get_project(pid);
    if (!loaded) {
        return std::unexpected(loaded.error());
    }
    if (!*loaded) {
        return value_error("Proyecto no encontrado: " + pid);
    }
    return std::move(**loaded);
}

// replace_media_version: un LookupError de media_upgrade es MCPError(resource_not_found) con su pista.
Result<Json> replace_media_version(const EditToolsEnv& env, const Json& args) {
    const std::string pid = text_arg(args, "project_id");
    if (auto p = project_or_raise(env, pid); !p) {
        return std::unexpected(p.error());
    }
    const std::string ident = text_arg(args, "clip_index");
    const VideoDimsProbe dims = env.video_dims ? env.video_dims : [](const std::filesystem::path&) {
        return std::optional<std::pair<std::int64_t, std::int64_t>>();
    };
    Result<Json> res;
    if (py_truthy(arg(args, "dry_run"))) {
        res = find_media_versions(env.store, pid, ident, dims);
    } else {
        std::vector<std::string> olds;
        const Json& raw = arg(args, "old_indexes");
        if (raw.is_array()) {
            for (const Json& o : raw) {
                olds.push_back(py_str(o)); // _find_material: str(ident)
            }
        }
        res = replace_media_with(env.store, pid, ident, olds, dims,
                                 [&env](std::string_view p, std::string_view op, const Json& params) {
                                     return timeline_store_apply_op(env, p, op, params);
                                 });
    }
    if (!res && res.error().code == ErrorCode::NotFound) {
        Error e = res.error();
        e.hint = "list_media para ver los index.";
        return std::unexpected(std::move(e));
    }
    return res;
}

// delete_media: el material sale del proyecto y, si se puede, su archivo del disco.
Result<Json> delete_media(const EditToolsEnv& env, const Json& args) {
    const std::string kind = text_arg(args, "kind");
    const char* media_kind = kind == "clips" ? "video" : kind == "audios" ? "audio" : kind == "images" ? "image" : nullptr;
    if (media_kind == nullptr) {
        return value_error("kind inválido: " + kind + " (usa clips|audios|images)", "kind");
    }
    const std::string pid = text_arg(args, "project_id");
    auto project = project_or_raise(env, pid);
    if (!project) {
        return std::unexpected(project.error());
    }
    const std::string ident = text_arg(args, "ident");
    auto removed = env.store.remove_material(pid, kind, ident);
    if (!removed) {
        return std::unexpected(removed.error());
    }
    if (!*removed) {
        return value_error("Material no encontrado: " + kind + "/" + ident);
    }
    bool file_deleted = false;
    const Json& filename = (**removed).contains("filename") ? (**removed)["filename"] : Json("");
    const auto path = resolve_project_media(env.store.clips_dir(), *project, media_kind,
                                            filename.is_string() ? filename.get<std::string>() : py_str(filename));
    std::error_code ec;
    if (path && std::filesystem::exists(*path, ec)) {
        file_deleted = std::filesystem::remove(*path, ec) && !ec; // si falla, el material ya salió del proyecto
    }
    return Json{{"ok", true}, {"deleted", ident}, {"kind", kind}, {"file_deleted", file_deleted}};
}

// search_sfx: sfx.search con max(1, min(int(limit), 300)) y lo que el agente necesita de cada sonido.
Result<Json> search_sfx(const EditToolsEnv& env, const Json& args) {
    // Sin limit, el 50 por defecto; un null explícito es int(None) de Python (TypeError).
    const bool given = args.is_object() && args.contains("limit");
    const auto limit = given ? py_int(args["limit"]) : std::optional<std::int64_t>(kSfxDefaultLimit);
    if (!limit) {
        return fail(ErrorCode::Internal, "int() de limit"); // TypeError o ValueError de Python: un crash
    }
    const Json settings = SettingsFile(env.data_dir / "settings.json").load();
    const Json res = sfx_search(env.sfx_base, settings, text_arg(args, "query"), text_arg(args, "category"),
                                static_cast<std::size_t>(std::max<std::int64_t>(1, std::min(*limit, kSfxMaxLimit))));
    const auto get = [](const Json& obj, const char* key) {
        return obj.is_object() && obj.contains(key) ? obj[key] : Json();
    };
    Json categories = Json::array();
    for (const Json& c : res.value("categories", Json::array())) {
        categories.push_back(get(c, "id"));
    }
    Json items = Json::array();
    for (const Json& it : res.value("items", Json::array())) {
        items.push_back({{"id", get(it, "id")},
                         {"name", get(it, "name")},
                         {"category", get(it, "category")},
                         {"uso", get(it, "uso")},
                         {"url", get(it, "url")}});
    }
    return Json{{"available", get(res, "available")},
                {"total", get(res, "total")},
                {"categories", std::move(categories)},
                {"items", std::move(items)}};
}

// set_clip_ai_description: description_ai aparte de la descripción del usuario; el índice vuelve como
// número si son dígitos (solo puede encontrarse uno que es el texto de un entero).
Result<Json> set_clip_ai_description(const EditToolsEnv& env, const Json& args) {
    const std::string pid = text_arg(args, "project_id");
    if (auto p = project_or_raise(env, pid); !p) {
        return std::unexpected(p.error());
    }
    const std::string ident = text_arg(args, "clip_index");
    const Json& raw = arg(args, "description");
    const std::string description = py_truthy(raw) ? py_strip(py_str(raw)) : std::string();
    auto item = env.store.update_material(pid, "clips", ident, Json{{"description_ai", description}});
    if (!item) {
        return std::unexpected(item.error());
    }
    if (!*item) {
        return value_error("Clip no encontrado: " + ident);
    }
    const bool digits = !ident.empty() && std::ranges::all_of(ident, [](char c) { return c >= '0' && c <= '9'; });
    Json index = ident;
    if (digits) {
        if (const auto n = py_int(Json(ident))) {
            index = *n;
        }
    }
    return Json{{"ok", true},
                {"clip_index", std::move(index)},
                {"description_ai", (**item).contains("description_ai") ? (**item)["description_ai"] : Json()}};
}

} // namespace

const std::vector<EditToolInfo>& media_tools() {
    static const std::vector<EditToolInfo> tools{{"replace_media_version", "write"},
                                                 {"delete_media", "destructive"},
                                                 {"search_sfx", "read"},
                                                 {"set_clip_ai_description", "write"}};
    return tools;
}

Result<Json> call_media_tool(const EditToolsEnv& env, std::string_view tool, const Json& args) {
    if (tool == "replace_media_version") {
        return replace_media_version(env, args);
    }
    if (tool == "delete_media") {
        return delete_media(env, args);
    }
    if (tool == "search_sfx") {
        return search_sfx(env, args);
    }
    if (tool == "set_clip_ai_description") {
        return set_clip_ai_description(env, args);
    }
    return fail(ErrorCode::NotFound, "Tool desconocida: " + std::string(tool));
}

} // namespace gc::core::mcp
