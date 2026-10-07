// Operaciones de propiedades de clip, encuadre, animación y material
// (espejo de backend/app/timeline_ops.py).

#include <algorithm>
#include <array>
#include <cmath>
#include <unordered_map>

#include "edit_ops.hpp"
#include "edit_util.hpp"
#include "gc/core/keyframes.hpp"
#include "gc/core/masks.hpp"
#include "gc/core/serialize.hpp"
#include "gc/core/timeline_rules.hpp"

namespace gc::core::detail {

namespace {

constexpr std::array<std::string_view, 8> kAppear{"none", "fade", "dissolve", "wipe",
                                                  "zoom", "slide_up", "slide_left", "pop"};
constexpr std::array<std::string_view, 8> kExit{"none", "fade", "dissolve", "wipe",
                                                "zoom", "slide_down", "slide_right", "pop"};
constexpr std::array<std::string_view, 4> kFramePositions{"full", "top", "bottom", "free"};
constexpr std::array<std::string_view, 12> kBlendModes{"normal",     "darken",      "multiply",   "color_burn",
                                                       "lighten",    "screen",      "color_dodge", "overlay",
                                                       "soft_light", "hard_light",  "difference", "exclusion"};
constexpr std::array<std::string_view, 15> kFilterIds{"bw",       "noir",      "sepia",       "vintage", "faded",
                                                      "matte",    "cinematic", "teal_orange", "night",   "matrix",
                                                      "contrast", "saturated", "warm",        "golden",  "cool"};
constexpr std::array<std::string_view, 3> kReframeModes{"center", "manual", "keyframes"};

template <std::size_t N>
bool in(const std::array<std::string_view, N>& list, std::string_view v) {
    return std::ranges::find(list, v) != list.end();
}

template <std::size_t N>
std::string py_tuple(const std::array<std::string_view, N>& list) {
    std::string out = "(";
    for (std::size_t i = 0; i < N; ++i) {
        out += (i ? ", '" : "'") + std::string(list[i]) + "'";
    }
    return out + ")";
}

// _clamp01 de timeline_ops: float(v) acotado a 0–1, o el defecto si no es un número.
double clamp01(const Json& v, double fallback = 0.5) {
    const auto f = py_float(v);
    if (!f) {
        return fallback;
    }
    return std::max(0.0, std::min(1.0, *f));
}

// dict.get(key, default)
const Json& get_or(const Json& obj, std::string_view key, const Json& fallback) {
    if (!obj.is_object()) {
        return fallback;
    }
    const auto it = obj.find(key);
    return it == obj.end() ? fallback : *it;
}

Keyframe keyframe_at(double t, double cx, double cy) {
    Keyframe k;
    k.t = t;
    k.cx = cx;
    k.cy = cy;
    return k;
}

// Aplica una operación de un solo clip: copia la timeline, busca el clip y deja
// que `fn` lo cambie (con `mut()`, que lo clona si está compartido).
template <class Fn>
Result<EditResult> edit_clip(const Timeline& tl, std::string_view clip_id, Fn&& fn) {
    Timeline out = tl;
    const auto ci = find_clip(out, clip_id);
    if (!ci) {
        return std::unexpected(ci.error());
    }
    if (auto r = fn(out.clips[*ci]); !r) {
        return std::unexpected(r.error());
    }
    return EditResult{std::move(out), {std::string(clip_id)}, {}};
}

} // namespace

// --- Propiedades escalares ------------------------------------------------------------------

Result<EditResult> run(const Timeline& tl, const ops::SetClipOpacity& op, IdSource&) {
    return edit_clip(tl, op.clip_id, [&](Cow<TimelineClip>& c) -> Result<void> {
        if (!(0.0 <= op.opacity && op.opacity <= 1.0)) {
            return invalid("opacity debe estar en [0, 1]");
        }
        c.mut().opacity = op.opacity;
        return {};
    });
}

Result<EditResult> run(const Timeline& tl, const ops::SetClipSpeed& op, IdSource&) {
    return edit_clip(tl, op.clip_id, [&](Cow<TimelineClip>& c) -> Result<void> {
        if (c->kind == "text" || c->kind == "image" || c->kind == "shape") {
            return invalid("speed no aplica a clips de texto/imagen/figura");
        }
        if (op.speed) {
            if (!(kSpeedMin <= *op.speed && *op.speed <= kSpeedMax)) {
                return invalid("speed debe estar en [0.1, 10.0]");
            }
            c.mut().speed = *op.speed;
        }
        if (op.keep_pitch) {
            c.mut().keep_pitch = *op.keep_pitch;
        }
        if (op.reverse) {
            c.mut().reverse = *op.reverse;
        }
        return {};
    });
}

Result<EditResult> run(const Timeline& tl, const ops::SetClipTransition& op, IdSource&) {
    return edit_clip(tl, op.clip_id, [&](Cow<TimelineClip>& c) -> Result<void> {
        if (op.appear) {
            if (!in(kAppear, *op.appear)) {
                return invalid("appear inválido: " + *op.appear + " (usa " + py_tuple(kAppear) + ")");
            }
            c.mut().appear = *op.appear;
        }
        if (op.exit) {
            if (!in(kExit, *op.exit)) {
                return invalid("exit inválido: " + *op.exit + " (usa " + py_tuple(kExit) + ")");
            }
            c.mut().exit = *op.exit;
        }
        return {};
    });
}

Result<EditResult> run(const Timeline& tl, const ops::SetTextRole& op, IdSource&) {
    return edit_clip(tl, op.clip_id, [&](Cow<TimelineClip>& c) -> Result<void> {
        if (c->kind != "text") {
            return invalid("text_role solo aplica a clips de texto");
        }
        if (op.role != "caption" && op.role != "free") {
            return invalid("role debe ser 'caption' o 'free'");
        }
        c.mut().text_role = op.role;
        return {};
    });
}

Result<EditResult> run(const Timeline& tl, const ops::SetClipBlend& op, IdSource&) {
    return edit_clip(tl, op.clip_id, [&](Cow<TimelineClip>& c) -> Result<void> {
        if (c->kind == "audio") {
            return invalid("blend_mode no aplica a clips de audio");
        }
        const std::string mode = has_text(op.blend_mode) ? *op.blend_mode : "normal";
        if (!in(kBlendModes, mode)) {
            return invalid("blend_mode debe ser uno de los modos de fusión");
        }
        c.mut().blend_mode = mode == "normal" ? std::nullopt : std::optional<std::string>(mode);
        return {};
    });
}

Result<EditResult> run(const Timeline& tl, const ops::SetClipFlip& op, IdSource&) {
    return edit_clip(tl, op.clip_id, [&](Cow<TimelineClip>& c) -> Result<void> {
        if (c->kind == "audio") {
            return invalid("flip no aplica a clips de audio");
        }
        if (op.flip_h) {
            c.mut().flip_h = *op.flip_h;
        }
        if (op.flip_v) {
            c.mut().flip_v = *op.flip_v;
        }
        return {};
    });
}

Result<EditResult> run(const Timeline& tl, const ops::SetClipLayout& op, IdSource&) {
    return edit_clip(tl, op.clip_id, [&](Cow<TimelineClip>& c) -> Result<void> {
        if (op.position) {
            if (!in(kFramePositions, *op.position)) {
                return invalid("position inválida: " + *op.position + " (usa " + py_tuple(kFramePositions) + ")");
            }
            c.mut().frame = *op.position;
        }
        if (op.start) {
            if (*op.start < 0) {
                return invalid("start no puede ser negativo");
            }
            c.mut().start = round3(*op.start);
        }
        if (op.duration) {
            if (*op.duration <= 0) {
                return invalid("duration debe ser > 0");
            }
            TimelineClip& m = c.mut();
            double end = m.in_point + *op.duration;
            // Texto, imagen, figura y ajuste no tienen fuente fija: se alarga la fuente.
            if (m.kind == "text" || m.kind == "image" || m.kind == "shape" || m.kind == "adjustment") {
                m.out_point = round3(end);
                m.source_duration = round3(std::max(m.source_duration, end));
            } else {
                if (m.source_duration != 0.0) {
                    end = std::min(end, m.source_duration);
                }
                m.out_point = round3(end);
            }
        }
        return {};
    });
}

Result<EditResult> run(const Timeline& tl, const ops::SetClipNote& op, IdSource&) {
    return edit_clip(tl, op.clip_id, [&](Cow<TimelineClip>& c) -> Result<void> {
        std::string text = py_strip(op.note.value_or(""));
        if (py_len(text) > 400) {
            text = py_rstrip(py_slice(text, 399)) + "…";
        }
        TimelineClip& m = c.mut();
        const bool known_source = op.source == "user" || op.source == "ai";
        m.note = text.empty() ? std::nullopt : std::optional<std::string>(text);
        m.note_source = !text.empty() && known_source ? std::optional<std::string>(op.source.get<std::string>())
                                                      : std::nullopt;
        return {};
    });
}

Result<EditResult> run(const Timeline& tl, const ops::UpdateClip& op, IdSource& ids) {
    static constexpr std::array<std::string_view, 15> kKeys{
        "opacity", "speed", "keep_pitch", "reverse", "appear", "exit",   "position", "start",
        "duration", "role", "note",       "flip_h",  "flip_v", "blend_mode", "disabled"};
    const Json patch = py_truthy(op.patch) ? op.patch : Json::object();
    if (!patch.is_object()) {
        return invalid("patch debe ser un objeto");
    }
    std::string unknown;
    for (const auto& [key, value] : patch.items()) {
        if (!in(kKeys, key)) {
            unknown += (unknown.empty() ? "'" : ", '") + key + "'";
        }
    }
    if (!unknown.empty()) {
        return invalid("patch: claves no válidas [" + unknown + "]");
    }
    if (patch.empty()) {
        return invalid("patch vacío: indica al menos una propiedad a cambiar");
    }
    auto has = [&](std::initializer_list<std::string_view> keys) {
        return std::ranges::any_of(keys, [&](std::string_view k) { return patch.contains(k); });
    };
    const Json& p = patch;
    auto get = [&](std::string_view k) -> const Json& { return field(p, k); };

    // Cada grupo es la sub-operación de siempre, encadenada sobre el resultado anterior.
    Timeline cur = tl;
    std::vector<std::string> warnings;
    auto step = [&](auto&& sub) -> Result<void> {
        auto r = run(cur, sub, ids);
        if (!r) {
            return std::unexpected(r.error());
        }
        cur = std::move(r->timeline);
        for (auto& w : r->warnings) {
            warnings.push_back(std::move(w));
        }
        return {};
    };
#define GC_STEP(expr)                                                                                                 \
    if (auto r_ = step(expr); !r_) {                                                                                  \
        return std::unexpected(r_.error());                                                                           \
    }

    if (has({"opacity"})) {
        GC_TRY(opacity, to_py_float(get("opacity"), "opacity"));
        GC_STEP((ops::SetClipOpacity{op.clip_id, opacity}));
    }
    if (has({"speed", "keep_pitch", "reverse"})) {
        GC_TRY(speed, opt_py_float(get("speed"), "speed"));
        GC_STEP((ops::SetClipSpeed{op.clip_id, speed, opt_truthy(get("keep_pitch")), opt_truthy(get("reverse"))}));
    }
    if (has({"appear", "exit"})) {
        GC_TRY(appear, opt_string(get("appear"), "appear", false));
        GC_TRY(exit, opt_string(get("exit"), "exit", false));
        GC_STEP((ops::SetClipTransition{op.clip_id, appear, exit}));
    }
    if (has({"position", "start", "duration"})) {
        GC_TRY(position, opt_string(get("position"), "position", false));
        GC_TRY(start, opt_py_number(get("start"), "start"));
        GC_TRY(duration, opt_py_number(get("duration"), "duration"));
        GC_STEP((ops::SetClipLayout{op.clip_id, position, start, duration}));
    }
    if (has({"role"})) {
        GC_TRY(role, req_string(get("role"), "role"));
        GC_STEP((ops::SetTextRole{op.clip_id, role}));
    }
    if (has({"note"})) {
        GC_TRY(note, opt_string(get("note"), "note", true));
        GC_STEP((ops::SetClipNote{op.clip_id, note, "user"}));
    }
    if (has({"flip_h", "flip_v"})) {
        GC_STEP((ops::SetClipFlip{op.clip_id, opt_truthy(get("flip_h")), opt_truthy(get("flip_v"))}));
    }
    if (has({"blend_mode"})) {
        GC_TRY(mode, opt_string(get("blend_mode"), "blend_mode", true));
        GC_STEP((ops::SetClipBlend{op.clip_id, mode}));
    }
    if (has({"disabled"})) {
        // Desactivar (tecla V): sigue en la timeline pero no se ve, no suena ni se exporta.
        const auto ci = find_clip(cur, op.clip_id);
        if (!ci) {
            return std::unexpected(ci.error());
        }
        cur.clips[*ci].mut().disabled = py_truthy(get("disabled"));
    }
#undef GC_STEP
    return EditResult{std::move(cur), {op.clip_id}, std::move(warnings)};
}

// --- Encuadre -------------------------------------------------------------------------------

Result<EditResult> run(const Timeline& tl, const ops::ReframeClip& op, IdSource&) {
    if (!in(kReframeModes, op.mode)) {
        return invalid("mode inválido: " + op.mode + " (usa " + py_tuple(kReframeModes) + ")");
    }
    return edit_clip(tl, op.clip_id, [&](Cow<TimelineClip>& c) -> Result<void> {
        if (!is_visual(*c)) {
            return invalid("reframe solo aplica a clips de vídeo o imagen");
        }
        const double z = op.zoom.value_or(1.0);
        if (!(0.1 <= z && z <= 1.0)) {
            return invalid("zoom debe estar en [0.1, 1.0]");
        }
        std::vector<Keyframe> kfs;
        if (op.mode == "center") {
            kfs.push_back(keyframe_at(c->in_point, 0.5, 0.5));
        } else if (op.mode == "manual") {
            const Json centered = {{"cx", 0.5}, {"cy", 0.5}};
            const Json& frm = py_truthy(op.pan_from) ? op.pan_from : centered;
            if (!frm.is_object()) {
                return invalid("pan_from debe ser un objeto {cx, cy}");
            }
            const Json half = 0.5;
            kfs.push_back(keyframe_at(c->in_point, clamp01(get_or(frm, "cx", half)), clamp01(get_or(frm, "cy", half))));
            if (!op.pan_to.is_null()) {
                if (!op.pan_to.is_object()) {
                    return invalid("pan_to debe ser un objeto {cx, cy}");
                }
                kfs.push_back(keyframe_at(c->out_point, clamp01(get_or(op.pan_to, "cx", half)),
                                          clamp01(get_or(op.pan_to, "cy", half))));
            }
        } else {
            if (!py_truthy(op.keyframes)) {
                return invalid("mode 'keyframes' requiere una lista de keyframes");
            }
            if (!op.keyframes.is_array()) {
                return invalid("keyframes debe ser una lista");
            }
            for (const Json& k : op.keyframes) {
                if (!k.is_object()) {
                    return invalid("cada keyframe debe ser un objeto {t, cx, cy}");
                }
                auto kf = keyframe_from_json(k);
                if (!kf) {
                    return std::unexpected(kf.error());
                }
                kfs.push_back(std::move(*kf));
            }
        }
        TimelineClip& m = c.mut();
        Reframe reframe = m.reframe.value_or(Reframe{});
        reframe.zoom = z;
        reframe.keyframes = std::move(kfs);
        m.reframe = std::move(reframe);
        return {};
    });
}

Result<EditResult> run(const Timeline& tl, const ops::CropClip& op, IdSource&) {
    Timeline out = tl;
    std::vector<std::string> ids;
    if (py_truthy(op.clip_ids)) {
        const auto items = py_iter(op.clip_ids);
        if (!items) {
            return invalid("clip_ids debe ser una lista");
        }
        for (const Json& id : *items) {
            GC_TRY(s, req_string(id, "clip_ids[]"));
            ids.push_back(std::move(s));
        }
    }
    if (op.track_id) {
        if (const auto t = find_track(out, *op.track_id); !t) {
            return std::unexpected(t.error());
        }
        for (const auto& c : out.clips) {
            if (c->track_id == *op.track_id && is_visual(*c)) {
                ids.push_back(c->id);
            }
        }
    }
    std::vector<std::string> unique; // dict.fromkeys: sin repetidos, en orden
    for (auto& id : ids) {
        if (std::ranges::find(unique, id) == unique.end()) {
            unique.push_back(std::move(id));
        }
    }
    if (unique.empty()) {
        return invalid("indica clip_ids o un track_id con clips visuales");
    }
    Json cx = op.cx;
    Json cy = op.cy;
    Json w = op.w;
    Json h = op.h;
    if (op.reset) {
        cx = 0.5;
        cy = 0.5;
        w = 1.0;
        h = 1.0;
    }
    if (w.is_null() || h.is_null()) {
        return invalid("indica w y h (0.05-1) o reset=true");
    }
    GC_TRY(wf, to_py_float(w, "w"));
    GC_TRY(hf, to_py_float(h, "h"));
    const double ww = std::min(1.0, std::max(0.05, wf));
    const double hh = std::min(1.0, std::max(0.05, hf));
    // Mismo acotado que clampCrop (clipLayout.js): la ventana no se sale de la fuente.
    double x = 0.5;
    double y = 0.5;
    if (ww < 1) {
        GC_TRY(fx, to_py_float(cx.is_null() ? Json(0.5) : cx, "cx"));
        x = std::min(1 - ww / 2, std::max(ww / 2, fx));
    }
    if (hh < 1) {
        GC_TRY(fy, to_py_float(cy.is_null() ? Json(0.5) : cy, "cy"));
        y = std::min(1 - hh / 2, std::max(hh / 2, fy));
    }

    std::vector<std::string> changed;
    for (const auto& cid : unique) {
        const auto ci = find_clip(out, cid);
        if (!ci) {
            return std::unexpected(ci.error());
        }
        const TimelineClip& c = *out.clips[*ci];
        if (!is_visual(c)) {
            return invalid("el clip " + cid + " no es de vídeo ni imagen");
        }
        if (c.layout != "overlay") {
            return invalid("el clip " + cid + " no es un objeto libre (layout=overlay); usa reframe_clip");
        }
        TimelineClip& m = out.clips[*ci].mut();
        Reframe rf = m.reframe.value_or(Reframe{});
        const std::optional<Keyframe> k0 = rf.keyframes.empty() ? std::nullopt : std::optional(rf.keyframes.front());
        rf.crop_w = py_round(ww, 4);
        rf.crop_h = py_round(hh, 4);
        Keyframe k = keyframe_at(m.in_point, py_round(x, 4), py_round(y, 4));
        if (k0) {
            k.zoom = k0->zoom;
            k.pan_mode = k0->pan_mode;
        }
        rf.keyframes = {std::move(k)};
        m.reframe = std::move(rf);
        // El recorte es FIJO: también en los snapshots de keyframes que fijan cx/cy.
        if (m.keyframes && m.keyframes->is_object() && !m.keyframes->empty() &&
            py_truthy(field(*m.keyframes, "items"))) {
            const auto items = py_iter(field(*m.keyframes, "items"));
            if (!items) {
                return invalid("keyframes.items no es una lista");
            }
            Json new_items = Json::array();
            for (const Json& it : *items) {
                if (!it.is_object()) {
                    return invalid("keyframes.items: cada item debe ser un objeto");
                }
                const Json& raw_props = field(it, "props");
                Json props = py_truthy(raw_props) ? raw_props : Json::object();
                if (!props.is_object()) {
                    return invalid("keyframes.items[].props debe ser un objeto");
                }
                if (props.contains("cx") || props.contains("cy")) {
                    props["cx"] = py_round(x, 4);
                    props["cy"] = py_round(y, 4);
                }
                Json item = it;
                item["props"] = std::move(props);
                new_items.push_back(std::move(item));
            }
            Json kf = *m.keyframes;
            kf["items"] = std::move(new_items);
            m.keyframes = std::move(kf);
        }
        changed.push_back(cid);
    }
    return EditResult{std::move(out), std::move(changed), {}};
}

// --- Animación, ritmo y efectos -------------------------------------------------------------

Result<EditResult> run(const Timeline& tl, const ops::SetClipKeyframes& op, IdSource&) {
    return edit_clip(tl, op.clip_id, [&](Cow<TimelineClip>& c) -> Result<void> {
        if (op.keyframes.is_null()) {
            c.mut().keyframes.reset();
            return {};
        }
        if (!op.keyframes.is_object()) {
            return invalid("keyframes debe ser un objeto {enabled, items}");
        }
        const Json& items = field(op.keyframes, "items");
        if (!items.is_null() && !items.is_array()) {
            return invalid("keyframes.items debe ser una lista");
        }
        const Json enabled = get_or(op.keyframes, "enabled", Json(true));
        c.mut().keyframes =
            Json{{"enabled", py_truthy(enabled)}, {"items", py_truthy(items) ? items : Json::array()}};
        return {};
    });
}

Result<EditResult> run(const Timeline& tl, const ops::SetClipBeats& op, IdSource&) {
    return edit_clip(tl, op.clip_id, [&](Cow<TimelineClip>& c) -> Result<void> {
        if (c->kind != "audio" && c->kind != "video") {
            return invalid("los beats solo aplican a clips de audio o vídeo");
        }
        if (op.beats.is_null()) {
            c.mut().beats.reset();
            return {};
        }
        if (!op.beats.is_object()) {
            return invalid("beats debe ser un objeto {times, bpm, every}");
        }
        std::vector<double> times;
        const Json& raw_times = field(op.beats, "times");
        if (py_truthy(raw_times)) {
            const auto items = py_iter(raw_times);
            if (!items) {
                return invalid("beats.times debe ser una lista");
            }
            for (const Json& t : *items) {
                GC_TRY(v, to_py_float(t, "beats.times[]"));
                if (v >= 0) {
                    times.push_back(round3(v));
                }
            }
        }
        std::ranges::sort(times);
        const Json& raw_every = field(op.beats, "every");
        const auto every = py_truthy(raw_every) ? py_int(raw_every) : std::optional<std::int64_t>(1);
        if (!every) {
            return invalid("every debe ser un entero");
        }
        if (*every != 1 && *every != 2 && *every != 4) {
            return invalid("every debe ser uno de [1, 2, 4]");
        }
        const Json& raw_bpm = field(op.beats, "bpm");
        GC_TRY(bpm, to_py_float(py_truthy(raw_bpm) ? raw_bpm : Json(0.0), "bpm"));
        c.mut().beats = Json{{"times", times}, {"bpm", py_round(bpm, 1)}, {"every", *every}};
        return {};
    });
}

Result<EditResult> run(const Timeline& tl, const ops::SetClipMasks& op, IdSource&) {
    return edit_clip(tl, op.clip_id, [&](Cow<TimelineClip>& c) -> Result<void> {
        if (!maskable(*c)) {
            return invalid("las máscaras solo aplican a vídeo, imagen, figura o texto");
        }
        if (!op.masks.is_null() && !op.masks.is_array()) {
            return invalid("masks debe ser una lista de máscaras");
        }
        std::vector<Json> masks;
        if (op.masks.is_array()) {
            for (const Json& m : op.masks) {
                if (m.is_object()) {
                    masks.push_back(normalize_mask(m));
                }
            }
        }
        c.mut().masks = std::move(masks);
        return {};
    });
}

Result<EditResult> run(const Timeline& tl, const ops::SetClipEffects& op, IdSource&) {
    return edit_clip(tl, op.clip_id, [&](Cow<TimelineClip>& c) -> Result<void> {
        if (!is_visual(*c) && c->kind != "adjustment") {
            return invalid("effects solo aplica a clips visuales (vídeo/imagen) o capas de ajuste");
        }
        if (op.effects.is_null() && op.filters.is_null()) {
            return invalid("pasa effects y/o filters");
        }
        Json filters_stack;
        if (!op.filters.is_null()) {
            const auto items = py_iter(op.filters);
            if (!items) {
                return invalid("filters debe ser una lista");
            }
            filters_stack = Json::array();
            for (const Json& f : *items) {
                const Json& fid = f.is_object() ? field(f, "id") : f;
                if (!fid.is_string() || !in(kFilterIds, fid.get<std::string>())) {
                    return invalid("filtro desconocido: " + fid.dump());
                }
                double amount = 1.0;
                if (f.is_object()) {
                    GC_TRY(a, to_py_float(get_or(f, "amount", Json(1)), "amount"));
                    amount = a;
                }
                filters_stack.push_back({{"id", fid}, {"amount", std::min(1.0, std::max(0.0, amount))}});
            }
        }
        if (!op.effects.is_null() && !op.effects.is_object()) {
            return invalid("effects debe ser un objeto");
        }
        TimelineClip& m = c.mut();
        if (!op.effects.is_null()) {
            Json base = !op.replace && m.effects && m.effects->is_object() ? *m.effects : Json::object();
            for (const auto& [key, value] : op.effects.items()) {
                base[key] = value;
            }
            m.effects = base.empty() ? std::nullopt : std::optional<Json>(std::move(base));
        }
        if (!op.filters.is_null()) {
            m.filters = std::move(filters_stack);
            m.look = "none"; // el `look` antiguo pasa a la pila
        }
        return {};
    });
}

// --- Material -------------------------------------------------------------------------------

namespace {

// _shift_times: suma `delta` a `key` de cada item (los números, bool incluido).
Result<void> shift_times(Json& items, double delta, const char* key = "t") {
    if (!py_truthy(items)) {
        return {};
    }
    if (!items.is_array()) {
        if (items.is_object() || items.is_string()) {
            return {}; // Python recorre claves/caracteres: ninguno es un dict
        }
        return invalid("lista de tiempos inválida");
    }
    for (Json& it : items) {
        if (!it.is_object()) {
            continue;
        }
        const Json& v = field(it, key);
        if (v.is_number() || v.is_boolean()) {
            it[key] = round3(*py_number(v) + delta);
        }
    }
    return {};
}

} // namespace

Result<EditResult> run(const Timeline& tl, const ops::ReplaceMedia& op, IdSource&) {
    Timeline out = tl;
    std::unordered_map<std::string, double> deltas;
    if (py_truthy(op.sources)) {
        const auto sources = py_iter(op.sources);
        if (!sources) {
            return invalid("sources debe ser una lista");
        }
        for (const Json& s : *sources) {
            if (!s.is_object()) {
                return invalid("cada source debe ser un objeto {filename, delta}");
            }
            if (!py_truthy(field(s, "filename"))) {
                continue;
            }
            const Json& raw_delta = field(s, "delta");
            GC_TRY(d, to_py_float(py_truthy(raw_delta) ? raw_delta : Json(0.0), "delta"));
            deltas[py_str(field(s, "filename"))] = d;
        }
    }
    if (deltas.empty()) {
        return invalid("replace_media: faltan los archivos a sustituir (sources)");
    }
    if (!op.target.is_object()) {
        return invalid("target debe ser un objeto");
    }
    const Json& raw_file = field(op.target, "filename");
    const std::string new_file = py_truthy(raw_file) ? py_str(raw_file) : std::string();
    const Json& raw_dur = field(op.target, "duration");
    GC_TRY(new_dur, to_py_float(py_truthy(raw_dur) ? raw_dur : Json(0.0), "duration"));
    if (new_file.empty() || deltas.contains(new_file)) {
        return invalid("replace_media: target.filename inválido");
    }

    std::vector<std::string> changed;
    std::vector<std::string> warnings;
    for (auto& cow : out.clips) {
        if (cow->asset_kind != "clips" || !deltas.contains(cow->filename)) {
            continue;
        }
        if ((cow->asset_scope.empty() ? std::string("project") : cow->asset_scope) != "project") {
            warnings.push_back("clip " + cow->id + ": usa la copia de la biblioteca; no se cambia");
            continue;
        }
        TimelineClip& c = cow.mut();
        const double d = deltas.at(c.filename);
        c.in_point = round3(std::max(0.0, c.in_point + d));
        c.out_point = round3(c.out_point + d);
        if (new_dur > 0) {
            if (c.out_point > new_dur) {
                warnings.push_back("clip " + c.id + ": el tramo pasaba del final del vídeo nuevo; recortado");
                c.out_point = round3(new_dur);
            }
            c.source_duration = round3(new_dur);
            if (c.out_point < new_dur - 0.05) {
                c.ref_segment = true;
            }
        }
        if (!op.target.contains("asset_id")) {
            return invalid("replace_media: falta target.asset_id");
        }
        c.asset_id = py_str(op.target["asset_id"]);
        c.asset_scope = "project";
        c.filename = new_file;
        const Json& mv = field(op.target, "media_version");
        if (py_truthy(mv)) {
            if (!mv.is_string()) {
                return invalid("target.media_version debe ser texto");
            }
            c.media_version = mv.get<std::string>();
        }
        if (d != 0.0) {
            if (c.reframe) {
                for (auto* list : {&c.reframe->keyframes, &c.reframe->keyframes2}) {
                    for (Keyframe& k : *list) {
                        k.t = round3(k.t + d);
                    }
                }
            }
            if (c.beats && c.beats->is_object() && field(*c.beats, "times").is_array()) {
                Json times = Json::array();
                for (const Json& t : (*c.beats)["times"]) {
                    if (t.is_number() || t.is_boolean()) {
                        times.push_back(round3(*py_number(t) + d));
                    }
                }
                (*c.beats)["times"] = std::move(times);
            }
        }
        if (c.bg_removal && c.bg_removal->is_object() && field(*c.bg_removal, "auto").is_object()) {
            Json& autom = (*c.bg_removal)["auto"];
            if (d != 0.0 && autom.contains("edits")) {
                if (auto r = shift_times(autom["edits"], d); !r) {
                    return std::unexpected(r.error());
                }
            }
            if (py_truthy(field(autom, "enabled"))) {
                autom["base_key"] = "";
                autom["status"] = "idle";
                warnings.push_back("clip " + c.id + ": vuelve a generar «Quitar fondo» con el vídeo nuevo");
            }
        }
        changed.push_back(c.id);
    }
    if (changed.empty()) {
        warnings.emplace_back("ningún clip de la timeline usaba esas versiones");
    }
    return EditResult{std::move(out), std::move(changed), std::move(warnings)};
}

} // namespace gc::core::detail
