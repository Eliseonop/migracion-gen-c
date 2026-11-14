#include "gc/core/mcp_tools_scene.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

#include "gc/core/library.hpp"
#include "gc/core/motion_scene.hpp"
#include "gc/core/projects_store.hpp"
#include "gc/core/scene_direction.hpp"
#include "json_reader.hpp"
#include "py.hpp"

namespace gc::core::mcp {

namespace {

using detail::parse_py_int_saturating;
using detail::py_int;
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

Result<Json> scene_direction_get(Ctx& c) {
    auto proj = project_or_raise(c.env, c.pid);
    if (!proj) {
        return std::unexpected(proj.error());
    }
    auto [units, source] = scene::script_units(*proj);
    auto doc = scene::load(c.scene, *proj);
    if (!doc) {
        return doc;
    }
    Json modes = Json::array();
    for (const scene::ModeInfo& m : scene::modes()) {
        modes.push_back(Json{{"key", m.key}, {"label", m.label}, {"ai", m.ai}});
    }
    const LibraryFile library(c.env.data_dir);
    auto catalog = scene::material_catalog(*proj, library);
    if (!catalog) {
        return catalog;
    }
    Json out = std::move(*doc);
    out["script_source"] = std::move(source);
    out["units"] = std::move(units);
    out["modes"] = std::move(modes);
    out["materials"] = std::move(*catalog);
    out["duration"] = scene::timeline_duration(*proj);
    return out;
}

// max(1, min(int(limit or 5), 20)), y 5 si int() da un TypeError o un ValueError. int() de infinito es
// un OverflowError, que no se captura. Un texto con un entero que no cabe en 64 bits se acota por su signo.
Result<std::size_t> rank_limit(const Json& v) {
    constexpr std::size_t kDefault = 5;
    if (!py_truthy(v)) {
        return kDefault;
    }
    double n = 0.0;
    if (v.is_number_float()) {
        n = v.get<double>();
        if (std::isinf(n)) {
            return fail(ErrorCode::Internal, "int() de infinito en limit");
        }
        if (std::isnan(n)) {
            return kDefault;
        }
        n = std::trunc(n);
    } else if (v.is_number_unsigned()) {
        n = static_cast<double>(v.get<std::uint64_t>());
    } else if (const auto t = v.is_string() ? parse_py_int_saturating(v.get_ref<const std::string&>()) : py_int(v)) {
        n = static_cast<double>(*t);
    } else {
        return kDefault;
    }
    return static_cast<std::size_t>(std::clamp(n, 1.0, 20.0));
}

Result<Json> scene_rank_materials(Ctx& c, const Json& args) {
    auto proj = project_or_raise(c.env, c.pid);
    if (!proj) {
        return std::unexpected(proj.error());
    }
    // (text or "").strip(): un texto falso es «»; uno verdadero que no es str no tiene strip().
    const Json& text = arg(args, "text");
    if (py_truthy(text) && !text.is_string()) {
        return fail(ErrorCode::Internal, "'text' no tiene strip()");
    }
    if (!py_truthy(text) || py_strip(text.get_ref<const std::string&>()).empty()) {
        return mcp_error("invalid_parameter", "Pasa 'text' (la narración o la idea del tramo).", true, "text");
    }
    const LibraryFile library(c.env.data_dir);
    auto catalog = scene::material_catalog(*proj, library);
    if (!catalog) {
        return catalog;
    }
    auto n = rank_limit(arg(args, "limit"));
    if (!n) {
        return std::unexpected(n.error());
    }
    auto ranked = scene::rank_materials(*catalog, text.get_ref<const std::string&>(), *n);
    if (!ranked) {
        return ranked;
    }
    return Json{{"materials", std::move(*ranked)}, {"catalog_size", catalog->size()}};
}

Result<Json> scene_direction_pack(Ctx& c, const Json& args) {
    auto proj = project_or_raise(c.env, c.pid);
    if (!proj) {
        return std::unexpected(proj.error());
    }
    auto seg = scene::normalize_segment(c.scene, arg(args, "segment"));
    if (!seg) {
        return std::unexpected(seg.error());
    }
    if (!*seg) {
        return mcp_error("invalid_parameter", "Tramo inválido: pasa 'segment' con al menos {start, end} (segundos).",
                         true, "segment");
    }
    // `segments` (una lista) da los vecinos; si no, la escaleta guardada.
    const Json& segments = arg(args, "segments");
    auto doc = segments.is_array() ? scene::normalize_doc(c.scene, Json{{"segments", segments}})
                                   : scene::load(c.scene, *proj);
    if (!doc) {
        return doc;
    }
    const LibraryFile library(c.env.data_dir);
    auto pack = scene::build_pack(c.scene, *proj, library, **seg, *doc);
    if (!pack) {
        return pack;
    }
    auto text = scene::pack_text(*pack);
    if (!text) {
        return std::unexpected(text.error());
    }
    // PACES.get(str(pace or "medio"), PACES["medio"])
    constexpr double kDefaultPace = *motion::pace_seconds(motion::kDefaultPace);
    const Json& pace = arg(args, "pace");
    const double pace_s =
        motion::pace_seconds(py_truthy(pace) ? py_str(pace) : std::string(motion::kDefaultPace)).value_or(kDefaultPace);
    const std::int64_t tokens = scene::estimate_tokens(*text);
    auto skeleton = scene::skeleton_beats(*pack, pace_s);
    if (!skeleton) {
        return skeleton; // un ValueError (round() de NaN) llega al agente con su texto
    }
    auto brief = scene::brief_defaults(*pack);
    if (!brief) {
        return brief;
    }
    return Json{{"pack", std::move(*pack)},
                {"text", std::move(*text)},
                {"tokens", tokens},
                {"skeleton", std::move(*skeleton)},
                {"brief_defaults", std::move(*brief)}};
}

Result<Json> scene_set_material_meta(Ctx& c, const Json& args) {
    // scope = "project" si no viene; si viene (también null), tiene que ser «project» o «library».
    std::string scope = "project";
    if (args.is_object() && args.contains("scope")) {
        const Json& s = args["scope"];
        if (!(s == "project" || s == "library")) {
            return mcp_error("invalid_parameter", "scope debe ser 'project' o 'library'.", true, "scope");
        }
        scope = s.get<std::string>();
    }
    if (scope == "project") {
        if (auto p = project_or_raise(c.env, c.pid); !p) {
            return std::unexpected(p.error());
        }
    }
    LibraryFile library(c.env.data_dir);
    const Json& meta = arg(args, "meta");
    auto r = scene::set_material_meta(c.env.store, library, c.pid, arg(args, "kind"), py_str(arg(args, "material_id")),
                                      py_truthy(meta) ? meta : Json::object(), scope);
    if (!r && r.error().code == ErrorCode::InvalidArgument) { // ValueError
        return mcp_error("invalid_parameter", r.error().message, true);
    }
    return lookup_as_mcp(std::move(r));
}

} // namespace

const std::vector<EditToolInfo>& scene_tools() {
    // En el orden de tools_scene.register.
    static const std::vector<EditToolInfo> tools{{"scene_get_blueprint", "read"},
                                                 {"scene_set_blueprint", "write"},
                                                 {"scene_direction_get", "read"},
                                                 {"scene_direction_status", "read"},
                                                 {"scene_direction_pack", "read"},
                                                 {"scene_rank_materials", "read"},
                                                 {"scene_direction_auto_split", "read"},
                                                 {"scene_direction_update_segment", "write"},
                                                 {"scene_direction_set", "write"},
                                                 {"scene_set_material_meta", "write"}};
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
    if (tool == "scene_direction_get") {
        return scene_direction_get(c);
    }
    if (tool == "scene_rank_materials") {
        return scene_rank_materials(c, args);
    }
    if (tool == "scene_direction_pack") {
        return scene_direction_pack(c, args);
    }
    if (tool == "scene_set_material_meta") {
        return scene_set_material_meta(c, args);
    }
    return fail(ErrorCode::NotFound, "Tool desconocida: " + std::string(tool));
}

} // namespace gc::core::mcp
