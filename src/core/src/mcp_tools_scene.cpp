#include "gc/core/mcp_tools_scene.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <string>
#include <utility>

#include "gc/core/projects_store.hpp"
#include "gc/core/scene_direction.hpp"
#include "json_reader.hpp"
#include "py.hpp"

namespace gc::core::mcp {

namespace {

using detail::py_str;
using detail::py_strip;
using detail::py_truthy;

// _PATCH_KEYS: los campos de un tramo que la IA puede cambiar (los del PATCH de HTTP).
constexpr std::array<std::string_view, 15> kPatchKeys{
    "status",   "composition_id",     "placed_clip_id", "mode",      "instruction",
    "strict",   "materials",          "reference_id",   "start",     "end",
    "text",     "composition_intent", "complexity",     "no_visual", "components"};

// sorted(_PATCH_KEYS) como lo escribe Python en el error.
constexpr std::string_view kPatchKeysText =
    "['complexity', 'components', 'composition_id', 'composition_intent', 'end', 'instruction', 'materials', "
    "'mode', 'no_visual', 'placed_clip_id', 'reference_id', 'start', 'status', 'strict', 'text']";

// MCPError(code, message, retryable=…, param=…).
std::unexpected<Error> mcp_error(std::string_view code, std::string message, bool retryable = false,
                                 std::string param = {}) {
    return std::unexpected(Error{.code = code == "resource_not_found" ? ErrorCode::NotFound : ErrorCode::InvalidArgument,
                                 .message = std::move(message),
                                 .path = std::move(param),
                                 .hint = {},
                                 .mcp_code = std::string(code),
                                 .retryable = retryable});
}

// `except LookupError as exc: raise MCPError("resource_not_found", str(exc))`: el resto (un crash, un
// fallo de disco) sigue tal cual.
template <class T>
Result<T> lookup_as_mcp(Result<T> r) {
    if (!r && r.error().code == ErrorCode::NotFound && r.error().mcp_code.empty()) {
        return mcp_error("resource_not_found", r.error().message);
    }
    return r;
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

std::string text_arg(const Json& args, const char* name) {
    const Json& v = arg(args, name);
    return v.is_null() ? std::string() : py_str(v);
}

// _project_or_raise.
Result<Project> project_or_raise(const EditToolsEnv& env, const std::string& pid) {
    auto loaded = env.store.get_project(pid);
    if (!loaded) {
        return std::unexpected(loaded.error());
    }
    if (!*loaded) {
        return mcp_error("resource_not_found", "Proyecto no encontrado: " + pid);
    }
    return std::move(**loaded);
}

double wall_clock() {
    return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
}

struct Ctx {
    const EditToolsEnv& env;
    scene::SceneEnv scene;
    std::string pid;
};

Result<Json> scene_get_blueprint(Ctx& c) {
    auto proj = project_or_raise(c.env, c.pid);
    if (!proj) {
        return std::unexpected(proj.error());
    }
    Json opts = Json::array();
    for (const scene::DirectionInfo& d : scene::directions()) {
        opts.push_back(Json{{"key", d.key}, {"label", d.label}, {"summary", d.summary}});
    }
    Json vocab = Json::array();
    for (const std::string_view v : scene::vocabulary()) {
        vocab.push_back(v);
    }
    return Json{{"blueprint", scene::load_blueprint(c.scene, *proj)},
                {"direction_options", std::move(opts)},
                {"default_direction", scene::kDefaultDirection},
                {"vocabulary_suggestions", std::move(vocab)}};
}

Result<Json> scene_set_blueprint(Ctx& c, const Json& args) {
    if (auto p = project_or_raise(c.env, c.pid); !p) {
        return std::unexpected(p.error());
    }
    const Json& bp = arg(args, "blueprint");
    if (!bp.is_object()) {
        return mcp_error("invalid_parameter", "Pasa 'blueprint' como objeto.", true, "blueprint");
    }
    const Json& raw = bp.contains("direction") ? bp["direction"] : Json();
    const std::string direction = py_strip(py_truthy(raw) ? py_str(raw) : std::string());
    if (!direction.empty() && !scene::is_direction(direction)) {
        return mcp_error("invalid_parameter",
                         "direction desconocida: " + direction + ". Elige una de scene_get_blueprint.", true,
                         "direction");
    }
    auto saved = lookup_as_mcp(scene::save_blueprint(c.scene, c.env.store, c.pid, bp));
    if (!saved) {
        return saved;
    }
    return Json{{"blueprint", std::move(*saved)}};
}

Result<Json> scene_direction_status(Ctx& c) {
    auto proj = project_or_raise(c.env, c.pid);
    if (!proj) {
        return std::unexpected(proj.error());
    }
    return scene::status_summary(c.scene, *proj);
}

Result<Json> scene_direction_auto_split(Ctx& c, const Json& args) {
    auto proj = project_or_raise(c.env, c.pid);
    if (!proj) {
        return std::unexpected(proj.error());
    }
    const Json& segments = arg(args, "segments");
    auto keep = segments.is_array() ? scene::normalize_doc(c.scene, Json{{"segments", segments}})
                                    : scene::load(c.scene, *proj);
    if (!keep) {
        return keep;
    }
    auto out = scene::auto_segments(c.scene, *proj, (*keep)["segments"]);
    if (!out) {
        return out;
    }
    return Json{{"segments", std::move(*out)}};
}

Result<Json> scene_direction_update_segment(Ctx& c, const Json& args) {
    if (auto p = project_or_raise(c.env, c.pid); !p) {
        return std::unexpected(p.error());
    }
    const Json& patch = arg(args, "patch");
    if (!patch.is_object() || patch.empty()) {
        return mcp_error("invalid_parameter", "Pasa 'patch' con los campos a cambiar.", true, "patch");
    }
    Json clean = Json::object();
    for (const auto& [k, v] : patch.items()) {
        if (std::ranges::find(kPatchKeys, k) != kPatchKeys.end()) {
            clean[k] = v;
        }
    }
    if (clean.empty()) {
        return mcp_error("invalid_parameter",
                         "Ningún campo válido en 'patch'. Permitidos: " + std::string(kPatchKeysText) + ".", true,
                         "patch");
    }
    return lookup_as_mcp(scene::update_segment(c.scene, c.env.store, c.pid, text_arg(args, "segment_id"), clean));
}

Result<Json> scene_direction_set(Ctx& c, const Json& args) {
    if (auto p = project_or_raise(c.env, c.pid); !p) {
        return std::unexpected(p.error());
    }
    const Json& segments = arg(args, "segments");
    if (!segments.is_array()) {
        return mcp_error("invalid_parameter", "Pasa 'segments' como lista de tramos.", true, "segments");
    }
    return lookup_as_mcp(scene::save(c.scene, c.env.store, c.pid, Json{{"segments", segments}}));
}

} // namespace

const std::vector<EditToolInfo>& scene_tools() {
    static const std::vector<EditToolInfo> tools{{"scene_get_blueprint", "read"},
                                                 {"scene_set_blueprint", "write"},
                                                 {"scene_direction_status", "read"},
                                                 {"scene_direction_auto_split", "read"},
                                                 {"scene_direction_update_segment", "write"},
                                                 {"scene_direction_set", "write"}};
    return tools;
}

Result<Json> call_scene_tool(const EditToolsEnv& env, std::string_view tool, const Json& args) {
    Ctx c{env, scene::SceneEnv{env.ids, env.unix_now ? env.unix_now : wall_clock}, text_arg(args, "project_id")};
    if (tool == "scene_get_blueprint") {
        return scene_get_blueprint(c);
    }
    if (tool == "scene_set_blueprint") {
        return scene_set_blueprint(c, args);
    }
    if (tool == "scene_direction_status") {
        return scene_direction_status(c);
    }
    if (tool == "scene_direction_auto_split") {
        return scene_direction_auto_split(c, args);
    }
    if (tool == "scene_direction_update_segment") {
        return scene_direction_update_segment(c, args);
    }
    if (tool == "scene_direction_set") {
        return scene_direction_set(c, args);
    }
    return fail(ErrorCode::NotFound, "Tool desconocida: " + std::string(tool));
}

} // namespace gc::core::mcp
