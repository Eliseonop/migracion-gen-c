// Comandos de edición: nombres, lectura desde JSON (kwargs de Python), ejecución
// y el pipeline completo de timeline_store.apply_op.

#include <algorithm>
#include <array>
#include <functional>
#include <set>
#include <unordered_set>

#include "edit_ops.hpp"
#include "edit_util.hpp"
#include "gc/core/edit.hpp"
#include "gc/core/no_overlap.hpp"
#include "gc/core/timeline_rules.hpp"
#include "json_reader.hpp"
#include "py.hpp"

namespace gc::core {

namespace {

using detail::py_float;
using detail::py_int;
using detail::py_number;
using detail::py_truthy;

// Mismo orden que las alternativas de `Command` (lo comprueba el static_assert).
constexpr std::array<std::string_view, 42> kNames{
    "add_track",        "reorder_track",  "rename_track",      "remove_track",       "link_tracks",
    "unlink_track",     "add_clip",       "move_clip",         "remove_clip",        "split_clip",
    "duplicate_clip",   "set_project_format", "set_markers",   "set_clip_opacity",   "set_clip_speed",
    "set_clip_transition", "set_text_role", "set_clip_blend",  "set_clip_flip",      "set_clip_layout",
    "set_clip_note",    "update_clip",    "reframe_clip",      "crop_clip",          "set_clip_keyframes",
    "set_clip_beats",   "set_clip_masks", "set_clip_effects",  "set_clip_volume",    "set_track_audio",
    "set_clip_audio_fx", "replace_media",  "apply_effect",      "set_clip_animation", "freeze_frame",
    "add_subtitles",    "add_shape",      "add_cinema_bars",   "add_adjustment_layer", "set_clip_bg_removal",
    "paste_clip_attributes", "follow_object",
};
static_assert(kNames.size() == std::variant_size_v<Command>, "kNames debe tener un nombre por comando");

std::unexpected<Error> bad_param(std::string_view op, std::string_view key, std::string_view what) {
    return fail(ErrorCode::InvalidArgument,
                std::string(op) + ": el parámetro '" + std::string(key) + "' " + std::string(what), std::string(key));
}

// Los parámetros de una operación, leídos como los kwargs de una función de Python:
// un parámetro que la función no tiene, o uno obligatorio que falta, es un error.
class Kwargs {
public:
    Kwargs(std::string_view op, const Json& params) : op_(op), params_(params) {}

    [[nodiscard]] Result<std::string> str(std::string_view key) {
        const Json* v = get(key);
        if (v == nullptr) {
            return missing(key);
        }
        if (!v->is_string()) {
            return bad_param(op_, key, "debe ser texto");
        }
        return v->get<std::string>();
    }

    // Con valor por defecto en la firma (`place="above"`): ausente = el defecto, pero un
    // null explícito llega a la función como None (y la función decide).
    [[nodiscard]] Result<std::string> str_or(std::string_view key, std::string_view fallback) {
        const Json* v = get(key);
        if (v == nullptr) {
            return std::string(fallback);
        }
        if (!v->is_string()) {
            return bad_param(op_, key, "debe ser texto");
        }
        return v->get<std::string>();
    }

    // `x or ""`: null y "" valen como texto vacío.
    [[nodiscard]] Result<std::string> str_or_empty(std::string_view key) {
        const Json* v = get(key);
        if (v == nullptr) {
            return missing(key);
        }
        if (v->is_null()) {
            return std::string();
        }
        if (!v->is_string()) {
            return bad_param(op_, key, "debe ser texto");
        }
        return v->get<std::string>();
    }

    // Opcional. Con `falsy_is_none` (el `x or default` de Python), "" y 0 cuentan como ausente.
    [[nodiscard]] Result<std::optional<std::string>> opt_str(std::string_view key, bool falsy_is_none,
                                                             bool required = false) {
        const Json* v = get(key);
        if (v == nullptr) {
            return required ? Result<std::optional<std::string>>(missing(key)) : std::optional<std::string>{};
        }
        if (v->is_null() || (falsy_is_none && !py_truthy(*v))) {
            return std::optional<std::string>{};
        }
        if (!v->is_string()) {
            return bad_param(op_, key, "debe ser texto");
        }
        return std::optional<std::string>{v->get<std::string>()};
    }

    // Número usado en comparaciones (`x < 0`): int, float o bool, nunca texto.
    [[nodiscard]] Result<double> number(std::string_view key) {
        const Json* v = get(key);
        if (v == nullptr) {
            return missing(key);
        }
        const auto n = py_number(*v);
        if (!n) {
            return bad_param(op_, key, "debe ser un número");
        }
        return *n;
    }
    [[nodiscard]] Result<std::optional<double>> opt_number(std::string_view key) {
        const Json* v = get(key);
        if (v == nullptr || v->is_null()) {
            return std::optional<double>{};
        }
        const auto n = py_number(*v);
        if (!n) {
            return bad_param(op_, key, "debe ser un número");
        }
        return std::optional<double>{*n};
    }
    // float(x) de Python: también acepta texto numérico.
    [[nodiscard]] Result<std::optional<double>> opt_float(std::string_view key) {
        const Json* v = get(key);
        if (v == nullptr || v->is_null()) {
            return std::optional<double>{};
        }
        const auto n = py_float(*v);
        if (!n) {
            return bad_param(op_, key, "debe ser un número");
        }
        return std::optional<double>{*n};
    }
    // int(x) de Python.
    [[nodiscard]] Result<std::optional<std::int64_t>> opt_int(std::string_view key) {
        const Json* v = get(key);
        if (v == nullptr || v->is_null()) {
            return std::optional<std::int64_t>{};
        }
        const auto n = py_int(*v);
        if (!n) {
            return bad_param(op_, key, "debe ser un entero");
        }
        return std::optional<std::int64_t>{*n};
    }
    [[nodiscard]] Result<Json> json(std::string_view key) {
        const Json* v = get(key);
        if (v == nullptr) {
            return missing(key);
        }
        return *v;
    }
    // Parámetro con valor por defecto en la firma: ausente = el defecto, tal cual.
    [[nodiscard]] Json json_or(std::string_view key, Json fallback = Json()) {
        const Json* v = get(key);
        return v == nullptr ? std::move(fallback) : *v;
    }
    // bool(x) de un parámetro con defecto.
    [[nodiscard]] bool truthy_or(std::string_view key, bool fallback) {
        const Json* v = get(key);
        return v == nullptr ? fallback : py_truthy(*v);
    }
    // `x is not None` → bool(x)
    [[nodiscard]] std::optional<bool> opt_bool(std::string_view key) {
        const Json* v = get(key);
        return v == nullptr ? std::nullopt : detail::opt_truthy(*v);
    }

    // Comprueba que no sobra ningún parámetro.
    [[nodiscard]] Result<void> finish() const {
        if (params_.is_null()) {
            return {};
        }
        for (const auto& [key, value] : params_.items()) {
            if (std::ranges::find(used_, key) == used_.end()) {
                return bad_param(op_, key, "no existe");
            }
        }
        return {};
    }

private:
    const Json* get(std::string_view key) {
        used_.emplace_back(key);
        if (!params_.is_object()) {
            return nullptr;
        }
        const auto it = params_.find(key);
        return it == params_.end() ? nullptr : &*it;
    }
    std::unexpected<Error> missing(std::string_view key) const { return bad_param(op_, key, "es obligatorio"); }

    std::string_view op_;
    const Json& params_;
    std::vector<std::string> used_;
};

using Parser = std::function<Result<Command>(Kwargs&)>;

const std::vector<std::pair<std::string_view, Parser>>& parsers() {
    static const std::vector<std::pair<std::string_view, Parser>> table{
        {"add_track",
         [](Kwargs& k) -> Result<Command> {
             GC_TRY(kind, k.str("kind"));
             GC_TRY(name, k.opt_str("name", true));
             GC_TRY(track_id, k.opt_str("track_id", true));
             return ops::AddTrack{kind, name, track_id};
         }},
        {"reorder_track",
         [](Kwargs& k) -> Result<Command> {
             GC_TRY(track_id, k.str("track_id"));
             GC_TRY(target, k.str("target_track_id"));
             GC_TRY(place, k.str_or("place", "above"));
             return ops::ReorderTrack{track_id, target, place};
         }},
        {"rename_track",
         [](Kwargs& k) -> Result<Command> {
             GC_TRY(track_id, k.str("track_id"));
             GC_TRY(name, k.str_or_empty("name"));
             return ops::RenameTrack{track_id, name};
         }},
        {"remove_track",
         [](Kwargs& k) -> Result<Command> {
             GC_TRY(track_id, k.str("track_id"));
             return ops::RemoveTrack{track_id};
         }},
        {"link_tracks",
         [](Kwargs& k) -> Result<Command> {
             GC_TRY(track_id, k.str("track_id"));
             GC_TRY(to, k.opt_str("to_track_id", true, true));
             return ops::LinkTracks{track_id, to};
         }},
        {"unlink_track",
         [](Kwargs& k) -> Result<Command> {
             GC_TRY(track_id, k.str("track_id"));
             return ops::UnlinkTrack{track_id};
         }},
        {"add_clip",
         [](Kwargs& k) -> Result<Command> {
             GC_TRY(clip, k.json("clip"));
             return ops::AddClip{clip};
         }},
        {"move_clip",
         [](Kwargs& k) -> Result<Command> {
             GC_TRY(clip_id, k.str("clip_id"));
             GC_TRY(start, k.opt_number("start"));
             GC_TRY(track_id, k.opt_str("track_id", false));
             return ops::MoveClip{clip_id, start, track_id};
         }},
        {"remove_clip",
         [](Kwargs& k) -> Result<Command> {
             GC_TRY(clip_id, k.str("clip_id"));
             return ops::RemoveClip{clip_id};
         }},
        {"split_clip",
         [](Kwargs& k) -> Result<Command> {
             GC_TRY(clip_id, k.str("clip_id"));
             GC_TRY(at_time, k.number("at_time"));
             return ops::SplitClip{clip_id, at_time};
         }},
        {"duplicate_clip",
         [](Kwargs& k) -> Result<Command> {
             GC_TRY(clip_id, k.str("clip_id"));
             GC_TRY(start, k.opt_number("start"));
             return ops::DuplicateClip{clip_id, start};
         }},
        {"set_project_format",
         [](Kwargs& k) -> Result<Command> {
             GC_TRY(aspect, k.opt_str("aspect", false));
             GC_TRY(width, k.opt_float("width"));
             GC_TRY(height, k.opt_float("height"));
             GC_TRY(fps, k.opt_number("fps"));
             GC_TRY(resolution, k.opt_int("resolution"));
             return ops::SetProjectFormat{aspect, width, height, fps, resolution};
         }},
        {"set_markers",
         [](Kwargs& k) -> Result<Command> {
             GC_TRY(markers, k.json("markers"));
             return ops::SetMarkers{markers};
         }},
        // --- Propiedades de clip ---
        {"set_clip_opacity",
         [](Kwargs& k) -> Result<Command> {
             GC_TRY(clip_id, k.str("clip_id"));
             GC_TRY(raw, k.json("opacity"));
             GC_TRY(opacity, detail::to_py_float(raw, "opacity"));
             return ops::SetClipOpacity{clip_id, opacity};
         }},
        {"set_clip_speed",
         [](Kwargs& k) -> Result<Command> {
             GC_TRY(clip_id, k.str("clip_id"));
             GC_TRY(speed, k.opt_float("speed"));
             return ops::SetClipSpeed{clip_id, speed, k.opt_bool("keep_pitch"), k.opt_bool("reverse")};
         }},
        {"set_clip_transition",
         [](Kwargs& k) -> Result<Command> {
             GC_TRY(clip_id, k.str("clip_id"));
             GC_TRY(appear, k.opt_str("appear", false));
             GC_TRY(exit, k.opt_str("exit", false));
             return ops::SetClipTransition{clip_id, appear, exit};
         }},
        {"set_text_role",
         [](Kwargs& k) -> Result<Command> {
             GC_TRY(clip_id, k.str("clip_id"));
             GC_TRY(role, k.str("role"));
             return ops::SetTextRole{clip_id, role};
         }},
        {"set_clip_blend",
         [](Kwargs& k) -> Result<Command> {
             GC_TRY(clip_id, k.str("clip_id"));
             GC_TRY(mode, k.opt_str("blend_mode", true, true));
             return ops::SetClipBlend{clip_id, mode};
         }},
        {"set_clip_flip",
         [](Kwargs& k) -> Result<Command> {
             GC_TRY(clip_id, k.str("clip_id"));
             return ops::SetClipFlip{clip_id, k.opt_bool("flip_h"), k.opt_bool("flip_v")};
         }},
        {"set_clip_layout",
         [](Kwargs& k) -> Result<Command> {
             GC_TRY(clip_id, k.str("clip_id"));
             GC_TRY(position, k.opt_str("position", false));
             GC_TRY(start, k.opt_number("start"));
             GC_TRY(duration, k.opt_number("duration"));
             return ops::SetClipLayout{clip_id, position, start, duration};
         }},
        {"set_clip_note",
         [](Kwargs& k) -> Result<Command> {
             GC_TRY(clip_id, k.str("clip_id"));
             GC_TRY(note, k.opt_str("note", true, true));
             return ops::SetClipNote{clip_id, note, k.json_or("source", "user")};
         }},
        {"update_clip",
         [](Kwargs& k) -> Result<Command> {
             GC_TRY(clip_id, k.str("clip_id"));
             return ops::UpdateClip{clip_id, k.json_or("patch")};
         }},
        // --- Encuadre ---
        {"reframe_clip",
         [](Kwargs& k) -> Result<Command> {
             GC_TRY(clip_id, k.str("clip_id"));
             GC_TRY(mode, k.str_or("mode", "center"));
             GC_TRY(zoom, k.opt_float("zoom"));
             return ops::ReframeClip{clip_id, mode, zoom, k.json_or("pan_from"), k.json_or("pan_to"),
                                     k.json_or("keyframes")};
         }},
        {"crop_clip",
         [](Kwargs& k) -> Result<Command> {
             GC_TRY(track_id, k.opt_str("track_id", false));
             return ops::CropClip{k.json_or("clip_ids"), track_id,       k.truthy_or("reset", false), k.json_or("cx"),
                                  k.json_or("cy"),       k.json_or("w"), k.json_or("h")};
         }},
        // --- Animación, ritmo y efectos ---
        {"set_clip_keyframes",
         [](Kwargs& k) -> Result<Command> {
             GC_TRY(clip_id, k.str("clip_id"));
             GC_TRY(keyframes, k.json("keyframes"));
             return ops::SetClipKeyframes{clip_id, keyframes};
         }},
        {"set_clip_beats",
         [](Kwargs& k) -> Result<Command> {
             GC_TRY(clip_id, k.str("clip_id"));
             GC_TRY(beats, k.json("beats"));
             return ops::SetClipBeats{clip_id, beats};
         }},
        {"set_clip_masks",
         [](Kwargs& k) -> Result<Command> {
             GC_TRY(clip_id, k.str("clip_id"));
             GC_TRY(masks, k.json("masks"));
             return ops::SetClipMasks{clip_id, masks};
         }},
        {"set_clip_effects",
         [](Kwargs& k) -> Result<Command> {
             GC_TRY(clip_id, k.str("clip_id"));
             return ops::SetClipEffects{clip_id, k.json_or("effects"), k.truthy_or("replace", false),
                                        k.json_or("filters")};
         }},
        // --- Audio ---
        {"set_clip_volume",
         [](Kwargs& k) -> Result<Command> {
             GC_TRY(clip_id, k.str("clip_id"));
             return ops::SetClipVolume{clip_id, k.json_or("volume"), k.json_or("muted"), k.json_or("fade"),
                                       k.json_or("fade_dur", 0.5)};
         }},
        {"set_track_audio",
         [](Kwargs& k) -> Result<Command> {
             GC_TRY(track_id, k.str("track_id"));
             return ops::SetTrackAudio{track_id,
                                       k.json_or("volume"),
                                       k.json_or("muted"),
                                       k.json_or("audio_fx"),
                                       k.truthy_or("replace_fx", false),
                                       k.json_or("fade"),
                                       k.json_or("fade_dur", 0.5)};
         }},
        {"set_clip_audio_fx",
         [](Kwargs& k) -> Result<Command> {
             GC_TRY(clip_id, k.str("clip_id"));
             GC_TRY(audio_fx, k.json("audio_fx"));
             return ops::SetClipAudioFx{clip_id, audio_fx, k.truthy_or("replace", false), k.json_or("ramp")};
         }},
        // --- Material ---
        {"replace_media",
         [](Kwargs& k) -> Result<Command> {
             GC_TRY(sources, k.json("sources"));
             GC_TRY(target, k.json("target"));
             return ops::ReplaceMedia{sources, target};
         }},
        // --- Catálogo y material nuevo ---
        {"apply_effect",
         [](Kwargs& k) -> Result<Command> {
             GC_TRY(clip_ids, k.json("clip_ids"));
             return ops::ApplyEffect{clip_ids,       k.json_or("effect"),         k.json_or("params"),
                                     k.json_or("start"), k.json_or("end"),         k.truthy_or("remove", false),
                                     k.json_or("uid"),   k.json_or("stack")};
         }},
        {"set_clip_animation",
         [](Kwargs& k) -> Result<Command> {
             GC_TRY(clip_ids, k.json("clip_ids"));
             GC_TRY(slot, k.json("slot"));
             return ops::SetClipAnimation{clip_ids, slot, k.json_or("animation"), k.json_or("duration")};
         }},
        {"freeze_frame",
         [](Kwargs& k) -> Result<Command> {
             GC_TRY(clip_id, k.str("clip_id"));
             GC_TRY(at_time, k.json("at_time"));
             GC_TRY(image, k.json("image"));
             return ops::FreezeFrame{clip_id, at_time, image, k.json_or("duration", 3.0)};
         }},
        {"add_subtitles",
         [](Kwargs& k) -> Result<Command> {
             GC_TRY(source_clip_id, k.str("source_clip_id"));
             GC_TRY(segments, k.json("segments"));
             GC_TRY(track_id, k.opt_str("track_id", false));
             return ops::AddSubtitles{source_clip_id, segments, k.json_or("style"), track_id,
                                      k.json_or("transcript_id")};
         }},
        {"add_shape",
         [](Kwargs& k) -> Result<Command> {
             GC_TRY(track_id, k.opt_str("track_id", false));
             return ops::AddShape{k.json_or("shape"), track_id, k.json_or("start", 0.0), k.json_or("duration"),
                                  k.json_or("points")};
         }},
        {"add_cinema_bars",
         [](Kwargs& k) -> Result<Command> {
             return ops::AddCinemaBars{k.json_or("ratio", "2.39"), k.json_or("start", 0.0), k.json_or("duration"),
                                       k.truthy_or("animate", false)};
         }},
        {"add_adjustment_layer",
         [](Kwargs& k) -> Result<Command> {
             GC_TRY(track_id, k.opt_str("track_id", false));
             return ops::AddAdjustmentLayer{k.json_or("start", 0.0), k.json_or("duration", 5.0),
                                            k.json_or("filters"),   k.json_or("effects"),
                                            k.json_or("intensity", 1.0), track_id};
         }},
        {"set_clip_bg_removal",
         [](Kwargs& k) -> Result<Command> {
             GC_TRY(clip_id, k.str("clip_id"));
             GC_TRY(bg, k.json("bg_removal"));
             return ops::SetClipBgRemoval{clip_id, bg, k.truthy_or("replace", false)};
         }},
        // --- Compuestos ---
        {"paste_clip_attributes",
         [](Kwargs& k) -> Result<Command> {
             GC_TRY(source, k.str("source_clip_id"));
             GC_TRY(targets, k.json("target_clip_ids"));
             return ops::PasteClipAttributes{source, targets, k.json_or("groups")};
         }},
        {"follow_object",
         [](Kwargs& k) -> Result<Command> {
             GC_TRY(follower, k.str("follower_clip_id"));
             GC_TRY(video, k.str("video_clip_id"));
             GC_TRY(track, k.json("track"));
             GC_TRY(box_w, k.json("box_w"));
             GC_TRY(anchor_t, k.json("anchor_t"));
             GC_TRY(dims, k.json("dims"));
             return ops::FollowObject{follower, video, track, box_w, anchor_t, dims,
                                      k.json_or("mode", "position_scale"), k.json_or("min_gap", 0.1)};
         }},
    };
    return table;
}

} // namespace

std::string_view command_name(const Command& command) { return kNames[command.index()]; }

std::vector<std::string_view> command_names() { return {kNames.begin(), kNames.end()}; }

Result<Command> parse_command(std::string_view op, const Json& params) {
    if (!params.is_null() && !params.is_object()) {
        return fail(ErrorCode::InvalidArgument, std::string(op) + ": los parámetros deben ser un objeto");
    }
    const auto& table = parsers();
    const auto it = std::ranges::find(table, op, &std::pair<std::string_view, Parser>::first);
    if (it == table.end()) {
        std::string known;
        for (const auto name : kNames) {
            known += (known.empty() ? "" : ", ") + std::string(name);
        }
        return fail(ErrorCode::NotFound, "Operación desconocida: " + std::string(op) + " (usa " + known + ")");
    }
    Kwargs kwargs(op, params);
    auto command = it->second(kwargs);
    if (!command) {
        return command;
    }
    if (auto rest = kwargs.finish(); !rest) {
        return std::unexpected(rest.error());
    }
    return command;
}

Result<EditResult> run_command(const Timeline& timeline, const Command& command, IdSource& ids) {
    return std::visit([&](const auto& op) { return detail::run(timeline, op, ids); }, command);
}

Result<EditResult> apply_command(const Timeline& timeline, const Command& command, IdSource& ids) {
    auto res = run_command(timeline, command, ids);
    if (!res) {
        return res;
    }

    // Una pista nunca tiene dos clips a la vez: lo que la operación haya montado sube
    // de pista o empuja a los de detrás.
    if (auto settled = settle_timeline(timeline, res->timeline, ids)) {
        const auto touched = touched_clip_ids(timeline, settled->timeline);
        std::set<std::string> touched_tracks; // ordenadas, como sorted() en Python
        for (const auto& c : settled->timeline.clips) {
            if (touched.contains(c->id)) {
                touched_tracks.insert(c->track_id);
            }
        }
        // Los avisos de solape de la operación ya no valen: se recalculan.
        std::vector<std::string> warnings;
        for (auto& w : res->warnings) {
            if (!w.starts_with("solape en ")) {
                warnings.push_back(std::move(w));
            }
        }
        for (const auto& tid : touched_tracks) {
            for (auto& w : track_overlap_warnings(settled->timeline, tid)) {
                warnings.push_back(std::move(w));
            }
        }
        for (auto& n : settled->notes) {
            warnings.push_back(std::move(n));
        }
        std::vector<std::string> changed = std::move(settled->created_tracks);
        for (auto& id : res->changed) {
            changed.push_back(std::move(id));
        }
        res = EditResult{std::move(settled->timeline), std::move(changed), std::move(warnings)};
    }

    // Rechazar solo errores NUEVOS: no bloquear por suciedad que ya traía el proyecto.
    const auto before = validate_timeline(timeline);
    if (!before) {
        return std::unexpected(before.error());
    }
    const auto after = validate_timeline(res->timeline);
    if (!after) {
        return std::unexpected(after.error());
    }
    const std::unordered_set<std::string> known(before->begin(), before->end());
    std::string fresh;
    for (const auto& e : *after) {
        if (!known.contains(e)) {
            fresh += (fresh.empty() ? "" : "; ") + e;
        }
    }
    if (!fresh.empty()) {
        return fail(ErrorCode::Validation, "La operación dejaría el timeline inválido: " + fresh);
    }
    return res;
}

} // namespace gc::core
