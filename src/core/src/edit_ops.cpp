// Operaciones de edición (espejo de backend/app/timeline_ops.py).
//
// Todas siguen el mismo patrón: copiar la timeline (barato: los clips se
// comparten), cambiar SOLO lo necesario con `mut()` y devolver EditResult. Nunca
// tocan la timeline de entrada.

#include "edit_ops.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <tuple>

#include "edit_util.hpp"
#include "gc/core/serialize.hpp"
#include "gc/core/timeline_rules.hpp"
#include "json_reader.hpp"
#include "py.hpp"

namespace gc::core::detail {

namespace {

constexpr std::array<std::string_view, 3> kTrackKinds{"video", "audio", "text"};

std::string_view track_prefix(std::string_view kind) {
    return kind == "video" ? "V" : kind == "audio" ? "A" : "T";
}

// --- Formato de salida (espejo de lib/projectFormat.js) ---

struct Aspect {
    std::string_view id;
    int w;
    int h;
};
constexpr std::array<Aspect, 6> kAspects{{
    {"16:9", 16, 9},
    {"9:16", 9, 16},
    {"1:1", 1, 1},
    {"4:3", 4, 3},
    {"3:4", 3, 4},
    {"4:5", 4, 5},
}};
constexpr std::array<std::int64_t, 4> kResolutions{480, 720, 1080, 2160};
constexpr double kDimMin = 144.0;
constexpr double kDimMax = 4096.0;

// Dimensión válida para H.264/yuv420p: par y dentro de [144, 4096]. floor(x + .5)
// es el Math.round de JS (round() de Python redondearía .5 al par).
std::int64_t even_dim(double n) {
    const double c = std::min(kDimMax, std::max(kDimMin, std::floor(n + 0.5)));
    return static_cast<std::int64_t>(std::floor(c / 2.0 + 0.5) * 2.0);
}

std::pair<std::int64_t, std::int64_t> size_for_ratio(double rw, double rh, double short_side) {
    const double r = rw / rh;
    double s = std::max(kDimMin, short_side);
    const double k = r >= 1.0 ? r : 1.0 / r;
    if (s * k > kDimMax) {
        s = kDimMax / k;
    }
    const double lng = s * k;
    return r >= 1.0 ? std::pair{even_dim(lng), even_dim(s)} : std::pair{even_dim(s), even_dim(lng)};
}

// timeline_ops._split_words: una palabra que empieza justo en el corte va a la
// SEGUNDA mitad, que se re-relativiza.
std::pair<std::vector<Word>, std::vector<Word>> split_words(const std::vector<Word>& words, double rel) {
    std::vector<Word> first;
    std::vector<Word> second;
    for (const Word& w : words) {
        if (w.start < rel - 1e-9) {
            first.push_back(w);
        }
    }
    for (const Word& w : words) {
        if (w.start >= rel - 1e-9) {
            Word moved = w;
            moved.start = round3(w.start - rel);
            moved.end = round3(std::max(0.0, w.end - rel));
            second.push_back(std::move(moved));
        }
    }
    return {std::move(first), std::move(second)};
}

} // namespace

Result<void> shift_keyframe_items(TimelineClip& clip, double rel, IdSource& ids) {
    if (!clip.keyframes || !clip.keyframes->is_object()) {
        return {};
    }
    const auto items = clip.keyframes->find("items");
    if (items == clip.keyframes->end() || !py_truthy(*items)) {
        return {};
    }
    if (!items->is_array() && !items->is_object() && !items->is_string()) {
        return invalid("keyframes.items no es una lista");
    }
    Json shifted = Json::array();
    if (items->is_array()) { // un dict o un texto se recorren por claves/caracteres: ningún item
        for (const Json& k : *items) {
            if (!k.is_object()) {
                continue;
            }
            Json item = k;
            item["id"] = make_uid(ids, "k");
            const auto t = k.find("t");
            const auto tv = t == k.end() ? std::optional<double>(0.0) : py_float(*t);
            if (!tv) {
                return invalid("keyframes: 't' no es un número");
            }
            item["t"] = py_round(*tv - rel, 4);
            shifted.push_back(std::move(item));
        }
    }
    Json kf = *clip.keyframes;
    kf["items"] = std::move(shifted);
    clip.keyframes = std::move(kf);
    return {};
}

// --- Pistas ---------------------------------------------------------------------------------

Result<EditResult> run(const Timeline& tl, const ops::AddTrack& op, IdSource& ids) {
    if (std::ranges::find(kTrackKinds, op.kind) == kTrackKinds.end()) {
        return invalid("kind de pista inválido: " + op.kind + " (usa ('video', 'audio', 'text'))");
    }
    Timeline out = tl;
    const std::string tid = has_text(op.track_id) ? *op.track_id : make_uid(ids, "K");
    if (std::ranges::any_of(out.tracks, [&](const TimelineTrack& t) { return t.id == tid; })) {
        return invalid("Ya existe una pista con id " + tid);
    }
    TimelineTrack track;
    track.id = tid;
    track.kind = op.kind;
    track.name = has_text(op.name) ? *op.name
                                   : std::string(track_prefix(op.kind)) + std::to_string(out.tracks.size() + 1);
    insert_track(out.tracks, std::move(track));
    return EditResult{std::move(out), {tid}, {}};
}

Result<EditResult> run(const Timeline& tl, const ops::ReorderTrack& op, IdSource&) {
    auto tracks = reorder_tracks(tl.tracks, op.track_id, op.target_track_id, op.place);
    if (!tracks) {
        return std::unexpected(tracks.error());
    }
    Timeline out = tl;
    out.tracks = std::move(*tracks);
    return EditResult{std::move(out), {op.track_id}, {}};
}

Result<EditResult> run(const Timeline& tl, const ops::RenameTrack& op, IdSource&) {
    const std::string label = py_slice(py_strip(op.name), 32);
    if (label.empty()) {
        return invalid("el nombre de la pista no puede estar vacío");
    }
    Timeline out = tl;
    const auto i = find_track(out, op.track_id);
    if (!i) {
        return std::unexpected(i.error());
    }
    out.tracks[*i].name = label;
    return EditResult{std::move(out), {op.track_id}, {}};
}

Result<EditResult> run(const Timeline& tl, const ops::RemoveTrack& op, IdSource&) {
    if (const auto i = find_track(tl, op.track_id); !i) {
        return std::unexpected(i.error());
    }
    Timeline out = tl;
    std::vector<std::string> changed{op.track_id};
    for (const auto& c : out.clips) {
        if (c->track_id == op.track_id) {
            changed.push_back(c->id);
        }
    }
    std::erase_if(out.tracks, [&](const TimelineTrack& t) { return t.id == op.track_id; });
    for (auto& t : out.tracks) {
        if (t.linked_track_id == op.track_id) {
            t.linked_track_id.reset();
        }
    }
    std::erase_if(out.clips, [&](const Cow<TimelineClip>& c) { return c->track_id == op.track_id; });
    return EditResult{std::move(out), std::move(changed), {}};
}

Result<EditResult> run(const Timeline& tl, const ops::LinkTracks& op, IdSource&) {
    Timeline out = tl;
    const auto i = find_track(out, op.track_id);
    if (!i) {
        return std::unexpected(i.error());
    }
    if (!has_text(op.to_track_id) || *op.to_track_id == op.track_id) {
        out.tracks[*i].linked_track_id.reset();
        return EditResult{std::move(out), {op.track_id}, {}};
    }
    if (const auto to = find_track(out, *op.to_track_id); !to) {
        return std::unexpected(to.error());
    }
    out.tracks[*i].linked_track_id = *op.to_track_id;
    return EditResult{std::move(out), {op.track_id}, {}};
}

Result<EditResult> run(const Timeline& tl, const ops::UnlinkTrack& op, IdSource&) {
    Timeline out = tl;
    const auto i = find_track(out, op.track_id);
    if (!i) {
        return std::unexpected(i.error());
    }
    out.tracks[*i].linked_track_id.reset();
    return EditResult{std::move(out), {op.track_id}, {}};
}

// --- Clips ---------------------------------------------------------------------------------

Result<EditResult> run(const Timeline& tl, const ops::AddClip& op, IdSource& ids) {
    if (!op.clip.is_object()) {
        return invalid("clip debe ser un objeto");
    }
    Timeline out = tl;
    Json data = op.clip;
    if (const auto id = data.find("id"); id == data.end() || !py_truthy(*id)) {
        data["id"] = make_uid(ids, "c"); // id requerido por el modelo; se genera si falta
    }
    auto clip = clip_from_json(data);
    if (!clip) {
        return std::unexpected(clip.error());
    }
    const auto t = find_track(out, clip->track_id);
    if (!t) {
        return std::unexpected(t.error());
    }
    const auto fits = clip_fits_track(clip->kind, out.tracks[*t].kind);
    if (!fits) {
        return std::unexpected(fits.error());
    }
    if (!*fits) {
        return invalid("kind '" + clip->kind + "' no cuadra con la pista '" + out.tracks[*t].kind + "'");
    }
    if (std::ranges::any_of(out.clips, [&](const Cow<TimelineClip>& c) { return c->id == clip->id; })) {
        return invalid("Ya existe un clip con id " + clip->id);
    }
    const std::string id = clip->id;
    const std::string track_id = clip->track_id;
    out.clips.emplace_back(std::move(*clip));
    auto warnings = track_overlap_warnings(out, track_id);
    return EditResult{std::move(out), {id}, std::move(warnings)};
}

Result<EditResult> run(const Timeline& tl, const ops::MoveClip& op, IdSource&) {
    Timeline out = tl;
    const auto ci = find_clip(out, op.clip_id);
    if (!ci) {
        return std::unexpected(ci.error());
    }
    if (op.track_id) {
        const auto t = find_track(out, *op.track_id);
        if (!t) {
            return std::unexpected(t.error());
        }
        const auto fits = clip_fits_track(out.clips[*ci]->kind, out.tracks[*t].kind);
        if (!fits) {
            return std::unexpected(fits.error());
        }
        if (!*fits) {
            return invalid("no se puede mover un clip '" + out.clips[*ci]->kind + "' a una pista '" +
                           out.tracks[*t].kind + "'");
        }
        out.clips[*ci].mut().track_id = *op.track_id;
    }
    if (op.start) {
        if (*op.start < 0) {
            return invalid("start no puede ser negativo");
        }
        out.clips[*ci].mut().start = round3(*op.start);
    }
    auto warnings = track_overlap_warnings(out, out.clips[*ci]->track_id);
    return EditResult{std::move(out), {op.clip_id}, std::move(warnings)};
}

Result<EditResult> run(const Timeline& tl, const ops::RemoveClip& op, IdSource&) {
    if (const auto ci = find_clip(tl, op.clip_id); !ci) {
        return std::unexpected(ci.error());
    }
    Timeline out = tl;
    std::erase_if(out.clips, [&](const Cow<TimelineClip>& c) { return c->id == op.clip_id; });
    return EditResult{std::move(out), {op.clip_id}, {}};
}

Result<EditResult> run(const Timeline& tl, const ops::SplitClip& op, IdSource& ids) {
    const auto ci = find_clip(tl, op.clip_id);
    if (!ci) {
        return std::unexpected(ci.error());
    }
    const TimelineClip& c = *tl.clips[*ci];
    const bool text = c.kind == "text";
    // Duración y corte en tiempo de TIMELINE → fuente (velocidad e invertido).
    const double speed = text ? 1.0 : clip_speed(c);
    const double dur = text ? std::max(0.0, c.out_point - c.in_point) : clip_timeline_duration(c);
    const double rel = op.at_time - c.start;
    if (rel <= 1e-6 || rel >= dur - 1e-6) {
        return invalid("at_time " + py_repr(op.at_time) + " debe caer DENTRO del clip [" + py_repr(c.start) + ", " +
                       py_repr(c.start + dur) + "]");
    }
    const bool reverse = c.reverse && !text;
    const double cut = reverse ? c.out_point - rel * speed : c.in_point + rel * speed;

    TimelineClip a = c;
    a.id = make_uid(ids, "c");
    (reverse ? a.in_point : a.out_point) = round3(cut);
    a.source_duration = c.source_duration;

    TimelineClip b = c;
    b.id = make_uid(ids, "c");
    b.start = round3(c.start + rel);
    (reverse ? b.out_point : b.in_point) = round3(cut);

    // Keyframes de pose (tiempo LOCAL): la parte derecha continúa la animación donde
    // iba, en vez de reiniciarla.
    if (auto r = shift_keyframe_items(b, rel, ids); !r) {
        return std::unexpected(r.error());
    }
    if (text && !c.words.empty()) {
        auto [wa, wb] = split_words(c.words, rel);
        a.words = std::move(wa);
        b.words = std::move(wb);
    }

    Timeline out = tl;
    const std::string a_id = a.id;
    const std::string b_id = b.id;
    const std::string track_id = c.track_id;
    const auto at = out.clips.begin() + static_cast<std::ptrdiff_t>(*ci);
    *at = Cow<TimelineClip>(std::move(a));
    out.clips.insert(at + 1, Cow<TimelineClip>(std::move(b)));
    auto warnings = track_overlap_warnings(out, track_id);
    return EditResult{std::move(out), {a_id, b_id}, std::move(warnings)};
}

Result<EditResult> run(const Timeline& tl, const ops::DuplicateClip& op, IdSource& ids) {
    const auto ci = find_clip(tl, op.clip_id);
    if (!ci) {
        return std::unexpected(ci.error());
    }
    const TimelineClip& c = *tl.clips[*ci];
    TimelineClip d = c;
    d.id = make_uid(ids, "c");
    d.dup_of = has_text(c.dup_of) ? *c.dup_of : c.id; // la raíz del linaje
    if (op.start) {
        if (*op.start < 0) {
            return invalid("start no puede ser negativo");
        }
        d.start = round3(*op.start);
    } else {
        const double speed = c.speed == 0.0 ? 1.0 : c.speed;
        d.start = round3(c.start + std::max(0.0, c.out_point - c.in_point) / (speed > 0 ? speed : 1.0));
    }
    Timeline out = tl;
    const std::string id = d.id;
    const std::string track_id = d.track_id;
    out.clips.emplace_back(std::move(d));
    auto warnings = track_overlap_warnings(out, track_id);
    return EditResult{std::move(out), {id}, std::move(warnings)};
}

// --- Timeline -------------------------------------------------------------------------------

Result<EditResult> run(const Timeline& tl, const ops::SetProjectFormat& op, IdSource&) {
    Timeline out = tl;
    const Aspect* aspect = nullptr;
    if (op.aspect) {
        const auto it = std::ranges::find(kAspects, *op.aspect, &Aspect::id);
        if (it == kAspects.end()) {
            return invalid("aspect inválido: " + *op.aspect + " (usa ['16:9', '9:16', '1:1', '4:3', '3:4', '4:5'])");
        }
        aspect = &*it;
    }
    if (op.resolution && std::ranges::find(kResolutions, *op.resolution) == kResolutions.end()) {
        return invalid("resolution inválida: " + std::to_string(*op.resolution) + " (usa [480, 720, 1080, 2160])");
    }
    if (aspect != nullptr || op.resolution) {
        const double rw = aspect != nullptr ? aspect->w : static_cast<double>(out.width);
        const double rh = aspect != nullptr ? aspect->h : static_cast<double>(out.height);
        if (rh == 0.0) {
            return invalid("formato actual inválido (alto 0)");
        }
        const double shortest =
            op.resolution ? static_cast<double>(*op.resolution) : static_cast<double>(std::min(out.width, out.height));
        std::tie(out.width, out.height) = size_for_ratio(rw, rh, shortest);
    }
    for (const auto& [value, target] : {std::pair{op.width, &out.width}, std::pair{op.height, &out.height}}) {
        if (value) {
            if (!std::isfinite(*value)) {
                return invalid("dimensión inválida");
            }
            *target = even_dim(*value);
        }
    }
    if (op.fps) {
        if (*op.fps <= 0) {
            return invalid("fps debe ser > 0");
        }
        if (!std::isfinite(*op.fps)) {
            return invalid("fps inválido");
        }
        out.fps = static_cast<std::int64_t>(std::trunc(*op.fps));
    }
    return EditResult{std::move(out), {}, {}};
}

Result<EditResult> run(const Timeline& tl, const ops::SetMarkers& op, IdSource& ids) {
    std::vector<Json> norm;
    if (py_truthy(op.markers)) {
        if (!op.markers.is_array()) {
            return invalid("markers debe ser una lista");
        }
        for (const Json& m : op.markers) {
            const auto t = m.is_object() ? m.find("t") : Json::const_iterator{};
            if (!m.is_object() || t == m.end() || t->is_null()) {
                return invalid("cada marcador necesita 't' (segundos de timeline)");
            }
            Json item = Json::object();
            const auto id = m.find("id");
            item["id"] = id != m.end() && py_truthy(*id) ? py_str(*id) : make_uid(ids, "m");
            const auto tv = py_float(*t);
            if (!tv) {
                return invalid("marcador: 't' no es un número");
            }
            item["t"] = round3(std::max(0.0, *tv));
            if (const auto label = m.find("label"); label != m.end() && py_truthy(*label)) {
                item["label"] = py_slice(py_str(*label), 60);
            }
            if (const auto color = m.find("color"); color != m.end() && py_truthy(*color)) {
                item["color"] = py_str(*color);
            }
            norm.push_back(std::move(item));
        }
    }
    std::ranges::stable_sort(norm, {}, [](const Json& m) { return m["t"].get<double>(); });
    Timeline out = tl;
    out.markers = std::move(norm);
    std::vector<std::string> changed;
    for (const Json& m : out.markers) {
        changed.push_back(m["id"].get<std::string>());
    }
    return EditResult{std::move(out), std::move(changed), {}};
}

} // namespace gc::core::detail
