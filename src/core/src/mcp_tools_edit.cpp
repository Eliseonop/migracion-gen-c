#include "gc/core/mcp_tools_edit.hpp"

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>

#include "gc/core/clip_motion.hpp"
#include "gc/core/edit.hpp"
#include "gc/core/fx_catalog.hpp"
#include "gc/core/history_store.hpp"
#include "gc/core/mcp_dto.hpp"
#include "gc/core/mcp_registry.hpp"
#include "gc/core/serialize.hpp"
#include "gc/core/session.hpp"
#include "gc/core/settings.hpp"
#include "gc/core/sfx_disk.hpp"
#include "gc/core/timeline_rules.hpp"

#include "json_reader.hpp"
#include "py.hpp"

namespace gc::core::mcp {

namespace {

using detail::py_float;
using detail::py_iter;
using detail::py_lower;
using detail::py_path_stem;
using detail::py_round;
using detail::py_split;
using detail::py_truthy;

constexpr double kImageDefaultDur = 5.0; // clip_kind.IMAGE_DEFAULT_DUR

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

// Un argumento obligatorio de texto.
Result<std::string> text_arg(const Json& args, const char* name) {
    const Json& v = arg(args, name);
    if (!v.is_string()) {
        return fail(ErrorCode::InvalidArgument, std::string("falta el argumento de texto ") + name, name);
    }
    return v.get<std::string>();
}

// --- timeline_store ----------------------------------------------------------------------------

struct StoreResult {
    Timeline timeline;
    std::vector<std::string> changed;
    std::vector<std::string> warnings;
    History history;
};

// timeline_store._current: la timeline del proyecto o, sin ella, una nueva con el fps de los ajustes.
Result<Timeline> current_timeline(const EditToolsEnv& env, std::string_view pid) {
    auto proj = env.store.get_project(pid);
    if (!proj) {
        return std::unexpected(proj.error());
    }
    if (!*proj) {
        return value_error("Proyecto no encontrado.");
    }
    if ((*proj)->timeline) {
        return std::move(*(*proj)->timeline);
    }
    const Json settings = SettingsFile(env.data_dir / "settings.json").load();
    auto exported = normalize_export_settings(arg(settings, "export"));
    if (!exported) {
        return std::unexpected(exported.error());
    }
    Timeline fresh;
    fresh.fps = exported->at("fps").get<std::int64_t>();
    return fresh;
}

// Guarda lo que deja una operación: primero el historial y después la timeline, como Python.
Result<void> persist(const EditToolsEnv& env, std::string_view pid, const History& history, const Timeline* timeline) {
    if (auto saved = save_history(history_path(env.data_dir, pid), history); !saved) {
        return std::unexpected(saved.error());
    }
    if (timeline != nullptr) {
        if (auto saved = env.store.save_timeline(pid, to_json(*timeline)); !saved) {
            return std::unexpected(saved.error());
        }
    }
    return {};
}

// timeline_store.apply_op: el proyecto, la op con sus reglas (sin solapes y solo errores nuevos), el
// estado anterior al historial y a guardar.
Result<StoreResult> apply_op(const EditToolsEnv& env, std::string_view pid, std::string_view op, const Json& params) {
    auto current = current_timeline(env, pid);
    if (!current) {
        return std::unexpected(current.error());
    }
    auto command = parse_command(op, params);
    if (!command) {
        return std::unexpected(command.error());
    }
    auto res = apply_command(*current, *command, env.ids);
    if (!res) {
        return std::unexpected(res.error());
    }
    History history = load_history(history_path(env.data_dir, pid));
    history.record(std::make_shared<const Timeline>(std::move(*current)));
    if (auto saved = persist(env, pid, history, &res->timeline); !saved) {
        return std::unexpected(saved.error());
    }
    return StoreResult{std::move(res->timeline), std::move(res->changed), std::move(res->warnings), std::move(history)};
}

Result<StoreResult> step_history(const EditToolsEnv& env, std::string_view pid, bool undo) {
    History history = load_history(history_path(env.data_dir, pid));
    auto current = current_timeline(env, pid);
    if (!current) {
        return std::unexpected(current.error());
    }
    const auto snapshot = std::make_shared<const Timeline>(*current);
    const TimelineSnapshot restored = undo ? history.undo(snapshot) : history.redo(snapshot);
    if (restored == nullptr) {
        return StoreResult{std::move(*current), {}, {undo ? "nada que deshacer" : "nada que rehacer"}, std::move(history)};
    }
    if (auto saved = persist(env, pid, history, restored.get()); !saved) {
        return std::unexpected(saved.error());
    }
    return StoreResult{*restored, {undo ? "undo" : "redo"}, {}, std::move(history)};
}

Result<StoreResult> make_checkpoint(const EditToolsEnv& env, std::string_view pid, const std::string& name) {
    History history = load_history(history_path(env.data_dir, pid));
    auto current = current_timeline(env, pid);
    if (!current) {
        return std::unexpected(current.error());
    }
    history.checkpoint(name, std::make_shared<const Timeline>(*current));
    if (auto saved = persist(env, pid, history, nullptr); !saved) {
        return std::unexpected(saved.error());
    }
    return StoreResult{std::move(*current), {name}, {}, std::move(history)};
}

// El checkpoint se busca antes que el proyecto, y volver a él también se puede deshacer.
Result<StoreResult> restore(const EditToolsEnv& env, std::string_view pid, const std::string& name) {
    History history = load_history(history_path(env.data_dir, pid));
    const TimelineSnapshot state = history.checkpoint_state(name);
    if (state == nullptr) {
        return value_error("Checkpoint inexistente: " + name);
    }
    auto current = current_timeline(env, pid);
    if (!current) {
        return std::unexpected(current.error());
    }
    history.record(std::make_shared<const Timeline>(std::move(*current)));
    if (auto saved = persist(env, pid, history, state.get()); !saved) {
        return std::unexpected(saved.error());
    }
    return StoreResult{*state, {"restore"}, {}, std::move(history)};
}

// tools_edit._edit_result.
Result<Json> edit_result(const Result<StoreResult>& res) {
    if (!res) {
        return std::unexpected(res.error());
    }
    auto detail = timeline_detail(&res->timeline);
    if (!detail) {
        return std::unexpected(detail.error());
    }
    Json out = Json::object();
    out["ok"] = true;
    out["changed"] = res->changed;
    out["warnings"] = res->warnings;
    out["can_undo"] = res->history.can_undo();
    out["can_redo"] = res->history.can_redo();
    out["checkpoints"] = res->history.checkpoints();
    out["timeline"] = std::move(*detail);
    return out;
}

// --- add_to_timeline ---------------------------------------------------------------------------

struct AssetInfo {
    std::string kind;
    std::string filename;
    double source_duration = 0.0;
    std::string name;
    double offset = 0.0;
    bool segment = false;
    Json reframe = nullptr; // null: sin encuadre
};

template <class T>
std::string label_or_filename(const T& item) {
    return item.label && !item.label->empty() ? *item.label : item.filename;
}

Result<AssetInfo> resolve_clip_asset(const Project& proj, const std::string& id) {
    for (const ClipInfo& c : proj.clips) {
        if (std::to_string(c.index) != id && !(c.id && !c.id->empty() && *c.id == id)) {
            continue;
        }
        AssetInfo info{.kind = "video", .filename = c.filename, .name = label_or_filename(c)};
        info.source_duration = py_round(c.end - c.start, 3);
        // Segmento por referencia: el archivo es el vídeo original y el tramo empieza en in_point.
        if (c.in_point && c.out_point) {
            info.segment = true;
            info.offset = *c.in_point;
            info.source_duration = py_round(*c.out_point - *c.in_point, 3);
        }
        if (c.face_track && c.reframe) {
            info.reframe = to_json(*c.reframe);
        }
        return info;
    }
    return value_error("Clip no encontrado en el proyecto: " + id);
}

// _resolve_asset: el material del proyecto con los campos que necesita un clip.
Result<AssetInfo> resolve_asset(const EditToolsEnv& env, const Project& proj, std::string_view kind,
                                const std::string& id) {
    if (kind == "clips") {
        return resolve_clip_asset(proj, id);
    }
    if (kind == "audios") {
        for (const AudioInfo& a : proj.audios) {
            if (a.id == id) {
                return AssetInfo{.kind = "audio",
                                 .filename = a.filename,
                                 .source_duration = a.duration.value_or(0.0),
                                 .name = label_or_filename(a)};
            }
        }
        return value_error("Audio no encontrado en el proyecto: " + id);
    }
    if (kind == "images") {
        for (const ImageInfo& im : proj.images) {
            if (im.id == id) {
                return AssetInfo{.kind = "image",
                                 .filename = im.filename,
                                 .source_duration = kImageDefaultDur,
                                 .name = label_or_filename(im)};
            }
        }
        return value_error("Imagen no encontrada en el proyecto: " + id);
    }
    if (kind == "sfx") {
        const auto path = sfx_resolve(env.sfx_base, id);
        if (!path) {
            return value_error("SFX no encontrado: " + id + " (usa un id de search_sfx)");
        }
        return AssetInfo{.kind = "audio",
                         .filename = id,
                         .source_duration = env.probe_duration ? env.probe_duration(*path) : 3.0,
                         .name = py_path_stem(id)};
    }
    return value_error("asset_kind inválido: " + std::string(kind) + " (usa clips|audios|images|sfx)", "asset_kind");
}

Result<double> float_arg(const Json& args, const char* name, double fallback) {
    const Json& v = arg(args, name);
    if (v.is_null()) {
        return fallback;
    }
    const auto x = py_float(v);
    if (!x) {
        return fail(ErrorCode::InvalidArgument, std::string("float() no válido en ") + name, name);
    }
    return *x;
}

// La pista del material: la que se pide, la primera de su tipo o una nueva (que es otro paso de deshacer).
Result<std::pair<std::string, bool>> track_for_asset(const EditToolsEnv& env, const std::string& pid,
                                                     const Project& proj, const std::string& clip_kind,
                                                     const Json& requested) {
    if (requested.is_string()) {
        return std::pair{requested.get<std::string>(), false};
    }
    const std::string track_kind(*track_kind_for_clip(clip_kind));
    if (proj.timeline) {
        const auto& tracks = proj.timeline->tracks;
        const auto it = std::ranges::find(tracks, track_kind, &TimelineTrack::kind);
        if (it != tracks.end()) {
            return std::pair{it->id, false};
        }
    }
    auto added = apply_op(env, pid, "add_track", Json{{"kind", track_kind}});
    if (!added) {
        return std::unexpected(added.error());
    }
    return std::pair{added->changed.at(0), true};
}

Result<Json> add_to_timeline(const EditToolsEnv& env, const std::string& pid, const Json& args) {
    auto stored = env.store.get_project(pid);
    if (!stored) {
        return std::unexpected(stored.error());
    }
    if (!*stored) {
        return value_error("Proyecto no encontrado: " + pid);
    }
    const Project& proj = **stored;
    auto asset_kind = text_arg(args, "asset_kind");
    auto asset_id = text_arg(args, "asset_id");
    if (!asset_kind || !asset_id) {
        return std::unexpected(!asset_kind ? asset_kind.error() : asset_id.error());
    }
    auto info = resolve_asset(env, proj, *asset_kind, *asset_id);
    if (!info) {
        return std::unexpected(info.error());
    }
    const double src_dur = info->source_duration;
    auto ip = float_arg(args, "in_point", 0.0);
    auto op_end = float_arg(args, "out_point", src_dur);
    auto start = float_arg(args, "start", 0.0);
    if (!ip || !op_end || !start) {
        return std::unexpected(!ip ? ip.error() : !op_end ? op_end.error() : start.error());
    }
    if (*op_end <= *ip) {
        return value_error("out_point debe ser > in_point; pasa out_point si el asset no tiene duración conocida");
    }
    auto track = track_for_asset(env, pid, proj, info->kind, arg(args, "track_id"));
    if (!track) {
        return std::unexpected(track.error());
    }

    Json clip = Json::object();
    clip["track_id"] = track->first;
    clip["kind"] = info->kind;
    clip["asset_kind"] = *asset_kind;
    clip["asset_id"] = *asset_id;
    clip["filename"] = info->filename;
    clip["name"] = info->name;
    clip["start"] = *start;
    clip["in_point"] = py_round(*ip + info->offset, 3);
    clip["out_point"] = py_round(*op_end + info->offset, 3);
    clip["source_duration"] = py_round(src_dur + info->offset, 3);
    if (info->kind == "video" || info->kind == "image") {
        clip["layout"] = "fill";
        clip["frame"] = "full";
    }
    if (info->segment) {
        clip["ref_segment"] = true;
    }
    if (!info->reframe.is_null()) {
        clip["reframe"] = info->reframe;
    }
    Json params = Json::object();
    params["clip"] = std::move(clip);
    auto out = edit_result(apply_op(env, pid, "add_clip", params));
    if (out && track->second) {
        Json& changed = (*out)["changed"];
        changed.insert(changed.begin(), track->first);
    }
    return out;
}

// --- animate_clip ------------------------------------------------------------------------------

struct FollowedAudio {
    Json envelope = nullptr;
    std::vector<std::string> warnings;
};

// _follow_audio_envelope: el RMS del clip de audio o SFX, alineado al clip visual.
Result<FollowedAudio> follow_audio_envelope(const EditToolsEnv& env, const std::string& pid, const std::string& clip_id,
                                            const std::string& audio_id) {
    auto stored = env.store.get_project(pid);
    if (!stored) {
        return std::unexpected(stored.error());
    }
    if (!*stored) {
        return value_error("Proyecto no encontrado: " + pid);
    }
    const Project& proj = **stored;
    if (!proj.timeline) {
        return value_error("el proyecto no tiene timeline");
    }
    const auto find = [&](const std::string& id) -> const TimelineClip* {
        const auto& clips = proj.timeline->clips;
        const auto it = std::ranges::find_if(clips, [&](const auto& c) { return c->id == id; });
        return it != clips.end() ? &**it : nullptr;
    };
    const TimelineClip* visual = find(clip_id);
    const TimelineClip* audio = find(audio_id);
    if (visual == nullptr) {
        return value_error("Clip inexistente: " + clip_id);
    }
    if (audio == nullptr) {
        return value_error("Clip de audio inexistente: " + audio_id);
    }
    if (audio->kind != "audio" && audio->kind != "video") {
        return value_error("follow_audio_id debe ser un clip de audio, SFX o vídeo con sonido");
    }
    if (!env.audio_envelope) {
        return fail(ErrorCode::Unsupported, "seguir un audio necesita gc_media");
    }
    FollowedAudio out;
    out.envelope = Json::array();
    const auto window = overlap_window(*visual, *audio);
    const double src = window ? audio_src_at(*audio, window->first) : audio->in_point;
    const double dur = window ? window->second - window->first
                              : std::min(gc::core::clip_timeline_duration(*audio),
                                         gc::core::clip_timeline_duration(*visual));
    const auto samples = env.audio_envelope(proj, *audio, src, dur);
    if (!samples) {
        return value_error("no se encuentra el archivo de audio para seguir");
    }
    if (!window) {
        out.warnings.emplace_back("el audio no se solapa con el clip; se usa desde el inicio del audio");
    }
    for (const auto& [t, amp] : *samples) {
        out.envelope.push_back(Json::array({window ? py_round(window->first - visual->start + t, 3) : t, amp}));
    }
    return out;
}

Result<Json> animate_clip(const EditToolsEnv& env, const std::string& pid, const Json& args) {
    auto clip_id = text_arg(args, "clip_id");
    if (!clip_id) {
        return std::unexpected(clip_id.error());
    }
    FollowedAudio followed;
    if (py_truthy(arg(args, "follow_audio_id"))) {
        auto audio_id = text_arg(args, "follow_audio_id");
        if (!audio_id) {
            return std::unexpected(audio_id.error());
        }
        auto res = follow_audio_envelope(env, pid, *clip_id, *audio_id);
        if (!res) {
            return std::unexpected(res.error());
        }
        followed = std::move(*res);
    }
    const auto or_default = [&](const char* name, double fallback) {
        return args.contains(name) ? arg(args, name) : Json(fallback);
    };
    Json params = Json::object();
    params["clip_id"] = *clip_id;
    params["motion"] = arg(args, "motion");
    params["duration"] = arg(args, "duration");
    params["intensity"] = or_default("intensity", 1.0);
    params["turns"] = or_default("turns", 1.0);
    params["envelope"] = followed.envelope;
    auto out = edit_result(apply_op(env, pid, "animate_clip", params));
    if (out) {
        for (const std::string& w : followed.warnings) {
            (*out)["warnings"].push_back(w);
        }
    }
    return out;
}

// --- list_effects ------------------------------------------------------------------------------

std::string joined_lower(std::initializer_list<const Json*> fields, const Json& tags) {
    std::string hay;
    bool first = true;
    const auto add = [&](const Json& v) {
        if (!first) {
            hay += ' ';
        }
        first = false;
        if (v.is_string()) {
            hay += v.get_ref<const std::string&>();
        }
    };
    for (const Json* f : fields) {
        add(*f);
    }
    if (tags.is_array()) {
        for (const Json& t : tags) {
            add(t);
        }
    }
    return py_lower(hay);
}

bool has_all(const std::string& hay, const std::vector<std::string>& words) {
    return std::ranges::all_of(words, [&](const std::string& w) { return hay.contains(w); });
}

// `list(catalog().get(key) or [])` de fx_catalog.
const Json& catalog_list(const Json& data, const char* key) {
    static const Json empty = Json::array();
    const Json& v = arg(data, key);
    return v.is_array() ? v : empty;
}

Json effect_entry(const Json& e) {
    Json params = Json::array();
    for (const Json& p : e.at("params")) {
        Json copy = p;
        copy.erase("label");
        params.push_back(std::move(copy));
    }
    Json out = Json::object();
    out["id"] = e.at("id");
    out["name"] = e.at("name");
    out["category"] = e.at("category");
    out["description"] = e.at("description");
    out["params"] = std::move(params);
    return out;
}

Result<Json> list_effects(const Json& args) {
    const Json& category = arg(args, "category");
    const Json& query = arg(args, "query");
    const auto words = py_split(py_lower(query.is_string() ? query.get<std::string>() : std::string()));
    const Json& data = fx_catalog()->data();

    Json effects = Json::array();
    for (const Json& e : catalog_list(data, "effects")) {
        if (arg(e, "engine") == "generator" || (py_truthy(category) && arg(e, "category") != category)) {
            continue;
        }
        if (has_all(joined_lower({&arg(e, "id"), &arg(e, "name"), &arg(e, "description")}, arg(e, "tags")), words)) {
            effects.push_back(effect_entry(e));
        }
    }
    Json anims = Json::object();
    for (const Json& a : catalog_list(data, "anims")) {
        if (has_all(joined_lower({&a.at("id"), &a.at("name")}, arg(a, "tags")), words)) {
            Json entry = Json::object();
            entry["id"] = a.at("id");
            entry["name"] = a.at("name");
            entry["dur"] = arg(a, "dur");
            anims[a.at("slot").get<std::string>()].push_back(std::move(entry));
        }
    }
    Json categories = Json::array();
    for (const Json& c : catalog_list(data, "categories")) {
        categories.push_back(c.at("id"));
    }
    Json out = Json::object();
    out["categories"] = std::move(categories);
    out["effects"] = std::move(effects);
    out["animations"] = std::move(anims);
    return out;
}

// --- Las tools que solo traducen a una op ------------------------------------------------------

struct Param {
    const char* name;
    Json fallback = nullptr; // el valor por defecto de Python; null también es None
};

struct OpTool {
    std::string_view name;
    std::string_view op;
    std::vector<Param> params;
    std::vector<const char*> lists = {}; // list(x or [])
    const char* dict = nullptr;          // dict(x or {})
};

const std::vector<OpTool>& op_tools() {
    static const std::vector<OpTool> tools = {
        {"move_clip", "move_clip", {{"clip_id"}, {"start"}, {"track_id"}}},
        {"split_clip", "split_clip", {{"clip_id"}, {"at_time"}}},
        {"remove_clip", "remove_clip", {{"clip_id"}}},
        {"update_clip", "update_clip", {{"clip_id"}, {"patch"}}},
        {"set_timeline_markers", "set_markers", {{"markers"}}},
        {"paste_clip_attributes",
         "paste_clip_attributes",
         {{"source_clip_id"}, {"target_clip_ids"}, {"groups"}},
         {"target_clip_ids"}},
        {"add_adjustment_layer",
         "add_adjustment_layer",
         {{"start", 0.0}, {"duration", 5.0}, {"filters"}, {"effects"}, {"intensity", 1.0}}},
        {"add_cinema_bars", "add_cinema_bars", {{"ratio", "2.39"}, {"start", 0.0}, {"duration"}, {"animate", false}}},
        {"apply_recipe", "apply_recipe", {{"recipe"}, {"clip_ids"}, {"params"}}, {"clip_ids"}, "params"},
        {"reframe_clip", "reframe_clip", {{"clip_id"}, {"mode", "center"}, {"zoom"}, {"pan_from"}, {"pan_to"}}},
        {"crop_clip", "crop_clip", {{"clip_ids"}, {"track_id"}, {"reset", false}, {"cx"}, {"cy"}, {"w"}, {"h"}}},
        {"add_subtitles",
         "add_subtitles",
         {{"source_clip_id"}, {"segments"}, {"track_id"}, {"transcript_id"}, {"style"}}},
        {"set_project_format", "set_project_format", {{"aspect"}, {"resolution"}, {"width"}, {"height"}, {"fps"}}},
        {"set_clip_effects", "set_clip_effects", {{"clip_id"}, {"effects"}, {"replace", false}, {"filters"}}},
        {"apply_effect",
         "apply_effect",
         {{"clip_ids"}, {"effect"}, {"params"}, {"start"}, {"end"}, {"remove", false}, {"uid"}, {"stack"}},
         {"clip_ids"}},
        {"set_clip_animation",
         "set_clip_animation",
         {{"clip_ids"}, {"slot"}, {"animation"}, {"duration"}},
         {"clip_ids"}},
        {"set_clip_masks", "set_clip_masks", {{"clip_id"}, {"masks"}}},
        {"set_clip_audio_fx", "set_clip_audio_fx", {{"clip_id"}, {"audio_fx"}, {"replace", false}, {"ramp"}}},
        {"set_clip_volume", "set_clip_volume", {{"clip_id"}, {"volume"}, {"muted"}, {"fade"}, {"fade_dur", 0.5}}},
        {"set_track_audio",
         "set_track_audio",
         {{"track_id"}, {"volume"}, {"muted"}, {"audio_fx"}, {"fade"}, {"fade_dur", 0.5}}},
        {"set_clip_keyframes", "set_clip_keyframes", {{"clip_id"}, {"keyframes"}}},
        {"duplicate_clip", "duplicate_clip", {{"clip_id"}, {"start"}}},
        {"add_shape", "add_shape", {{"shape"}, {"track_id"}, {"start", 0.0}, {"duration"}, {"points"}}},
        {"add_track", "add_track", {{"kind"}, {"name"}}},
        {"rename_track", "rename_track", {{"track_id"}, {"name"}}},
        {"reorder_track", "reorder_track", {{"track_id"}, {"target_track_id"}, {"place", "above"}}},
        {"remove_track", "remove_track", {{"track_id"}}},
        {"link_tracks", "link_tracks", {{"track_id"}, {"to_track_id"}}},
        {"unlink_track", "unlink_track", {{"track_id"}}},
    };
    return tools;
}

Result<Json> call_op_tool(const EditToolsEnv& env, const OpTool& tool, const std::string& pid, const Json& args) {
    Json params = Json::object();
    for (const Param& p : tool.params) {
        params[p.name] = args.contains(p.name) ? arg(args, p.name) : p.fallback;
    }
    for (const char* name : tool.lists) {
        Json& v = params[name];
        if (!py_truthy(v)) {
            v = Json::array();
            continue;
        }
        auto items = py_iter(v);
        if (!items) {
            return fail(ErrorCode::Internal, std::string("TypeError: ") + name + " no se puede recorrer");
        }
        v = std::move(*items);
    }
    if (tool.dict != nullptr) {
        Json& v = params[tool.dict];
        if (!py_truthy(v)) {
            v = Json::object();
        } else if (!v.is_object()) {
            return fail(ErrorCode::Internal, std::string("TypeError: ") + tool.dict + " no es un dict");
        }
    }
    return edit_result(apply_op(env, pid, tool.op, params));
}

} // namespace

const std::vector<EditToolInfo>& edit_tools() {
    static const std::vector<EditToolInfo> tools = {
        {"add_to_timeline", "write"},
        {"move_clip", "write"},
        {"split_clip", "write"},
        {"remove_clip", "destructive"},
        {"update_clip", "write"},
        {"set_timeline_markers", "write"},
        {"paste_clip_attributes", "write"},
        {"add_adjustment_layer", "write"},
        {"add_cinema_bars", "write"},
        {"apply_recipe", "write"},
        {"reframe_clip", "write"},
        {"crop_clip", "write"},
        {"add_subtitles", "write"},
        {"set_project_format", "write"},
        {"set_clip_effects", "write"},
        {"list_effects", "read"},
        {"apply_effect", "write"},
        {"set_clip_animation", "write"},
        {"set_clip_masks", "write"},
        {"set_clip_audio_fx", "write"},
        {"set_clip_volume", "write"},
        {"set_track_audio", "write"},
        {"set_clip_keyframes", "write"},
        {"animate_clip", "write"},
        {"duplicate_clip", "write"},
        {"add_shape", "write"},
        {"add_track", "write"},
        {"rename_track", "write"},
        {"reorder_track", "write"},
        {"remove_track", "destructive"},
        {"link_tracks", "write"},
        {"unlink_track", "write"},
        {"undo", "write"},
        {"redo", "write"},
        {"checkpoint", "write"},
        {"restore_checkpoint", "write"},
    };
    return tools;
}

Result<Json> call_edit_tool(const EditToolsEnv& env, std::string_view tool, const Json& args) {
    if (tool == "list_effects") {
        return list_effects(args);
    }
    const bool known = std::ranges::any_of(edit_tools(), [&](const EditToolInfo& t) { return t.name == tool; });
    if (!known) {
        return fail(ErrorCode::NotFound, "Tool desconocida: " + std::string(tool));
    }
    auto pid = text_arg(args, "project_id");
    if (!pid) {
        return std::unexpected(pid.error());
    }
    if (tool == "add_to_timeline") {
        return add_to_timeline(env, *pid, args);
    }
    if (tool == "animate_clip") {
        return animate_clip(env, *pid, args);
    }
    if (tool == "undo" || tool == "redo") {
        return edit_result(step_history(env, *pid, tool == "undo"));
    }
    if (tool == "checkpoint" || tool == "restore_checkpoint") {
        auto name = text_arg(args, "name");
        if (!name) {
            return std::unexpected(name.error());
        }
        return edit_result(tool == "checkpoint" ? make_checkpoint(env, *pid, *name) : restore(env, *pid, *name));
    }
    const auto& tools = op_tools();
    const auto it = std::ranges::find(tools, tool, &OpTool::name);
    return call_op_tool(env, *it, *pid, args);
}

} // namespace gc::core::mcp
