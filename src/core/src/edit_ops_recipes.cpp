// Recetas en un clic: los trucos de «10 CapCut Tricks» montados de una vez con las
// piezas del editor, en una sola operación deshacible (espejo de backend/app/recipes.py
// y de timeline_ops.apply_recipe).

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

#include "edit_ops.hpp"
#include "edit_util.hpp"
#include "gc/core/bg.hpp"
#include "gc/core/keyframes.hpp"
#include "gc/core/masks.hpp"
#include "gc/core/serialize.hpp"
#include "gc/core/sfx_library.hpp"
#include "gc/core/shapes.hpp"
#include "gc/core/text_metrics.hpp"
#include "gc/core/timeline_rules.hpp"

namespace gc::core::detail {

namespace {

constexpr std::array<std::string_view, 5> kRecipeIds{"cinema_grade", "text_reflection", "pass_through_text",
                                                     "film_strips", "subject_pop"};
constexpr std::size_t kFilmMax = 6;
constexpr double kFilmStep = 0.35;
constexpr std::int64_t kPopFrames = 6;
constexpr double kZoomTo = 60.0;

struct Outcome {
    std::vector<std::string> changed;
    std::vector<std::string> warnings;
};

bool in(std::string_view v, std::initializer_list<std::string_view> list) {
    return std::ranges::find(list, v) != list.end();
}

// Fin de la timeline: el final del último clip (0 si no hay ninguno).
double timeline_end(const Timeline& tl) {
    std::optional<double> end;
    for (const auto& c : tl.clips) {
        const double e = c->start + clip_span_duration(*c);
        if (!end || e > *end) {
            end = e;
        }
    }
    return end.value_or(0.0);
}

// Los clips seleccionados (`clip_ids`) de esos tipos, en el orden de la timeline: al menos
// `n_min` y como mucho `n_max`. Como `set(clip_ids)` en Python, un id que es una lista o un
// dict es un error; los que no son texto no coinciden con ningún clip.
Result<std::vector<std::size_t>> need(const Timeline& tl, const std::vector<Json>& clip_ids,
                                      std::initializer_list<std::string_view> kinds, std::size_t n_min = 1,
                                      std::size_t n_max = 99) {
    std::vector<std::string> ids;
    for (const Json& x : clip_ids) {
        if (x.is_array() || x.is_object()) {
            return invalid("clip_ids: cada id debe ser un texto");
        }
        if (x.is_string()) {
            ids.push_back(x.get<std::string>());
        }
    }
    std::vector<std::size_t> ok;
    for (std::size_t i = 0; i < tl.clips.size(); ++i) {
        const auto& c = *tl.clips[i];
        if (std::ranges::find(ids, c.id) != ids.end() && in(c.kind, kinds)) {
            ok.push_back(i);
        }
    }
    if (ok.size() < n_min) {
        const std::string what = n_min > 1                         ? "al menos 2 clips de vídeo o imagen"
                                 : *kinds.begin() == "text"  ? "un texto"
                                 : *kinds.begin() == "video" ? "un vídeo"
                                                             : "un clip";
        return invalid("esta receta necesita " + what + " seleccionado");
    }
    if (ok.size() > n_max) {
        ok.resize(n_max);
    }
    return ok;
}

// Pista nueva justo encima de `after`; sin ella, encima de la última de su tipo.
std::string insert_track_after(Timeline& tl, const std::optional<std::string>& after, std::string kind,
                               std::string name, IdSource& ids) {
    TimelineTrack t;
    t.id = make_uid(ids, "K");
    t.kind = std::move(kind);
    t.name = std::move(name);
    std::optional<std::ptrdiff_t> found;
    if (after) {
        for (std::size_t i = 0; i < tl.tracks.size(); ++i) {
            if (tl.tracks[i].id == *after) {
                found = static_cast<std::ptrdiff_t>(i);
                break;
            }
        }
    }
    std::ptrdiff_t idx = found.value_or(static_cast<std::ptrdiff_t>(tl.tracks.size()) - 1);
    if (!found) {
        for (std::size_t i = 0; i < tl.tracks.size(); ++i) {
            if (tl.tracks[i].kind == t.kind) {
                idx = static_cast<std::ptrdiff_t>(i);
            }
        }
    }
    const std::string id = t.id;
    tl.tracks.insert(tl.tracks.begin() + (idx + 1), std::move(t));
    return id;
}

// `float(d.get(key, fallback) or fallback)`
Result<double> param_float(const Json& d, std::string_view key, double fallback) {
    const Json& v = d.contains(key) ? d.at(key) : Json(fallback);
    return to_py_float(py_truthy(v) ? v : Json(fallback), key);
}

// --- Recetas -------------------------------------------------------------------------

Result<Outcome> cinema_grade(Timeline& tl, IdSource& ids) {
    const double end = timeline_end(tl);
    ops::AddAdjustmentLayer layer;
    layer.start = 0.0;
    layer.duration = end != 0.0 ? end : 5.0;
    layer.filters = Json::array({Json{{"id", "teal_orange"}, {"amount", 0.7}}, Json{{"id", "faded"}, {"amount", 0.25}}});
    GC_TRY(r, run(tl, layer, ids));
    const bool wide = static_cast<double>(tl.width) / static_cast<double>(tl.height) >= 1;
    ops::AddCinemaBars bars;
    bars.ratio = wide ? "2.39" : "16:9";
    bars.animate = true;
    GC_TRY(r2, run(r.timeline, bars, ids));
    tl = std::move(r2.timeline);
    Outcome out{std::move(r.changed), {}};
    out.changed.insert(out.changed.end(), r2.changed.begin(), r2.changed.end());
    return out;
}

Result<Outcome> text_reflection(Timeline& tl, const std::vector<Json>& clip_ids, IdSource& ids) {
    GC_TRY(sel, need(tl, clip_ids, {"text"}));
    Outcome out;
    const auto W = static_cast<double>(tl.width);
    const auto H = static_cast<double>(tl.height);
    for (const std::size_t idx : sel) {
        const TimelineClip c = *tl.clips[idx];
        const Json st = c.style && c.style->is_object() ? *c.style : Json::object();
        const auto rows = text_rows(st, W, H, c.text.value_or(""));
        const double block = static_cast<double>(rows.size()) * text_font_px(st, H) * text_line_height(st) / H;
        GC_TRY(y, to_py_float(st.contains("y") ? st.at("y") : Json(0.5), "style.y"));
        Json data = to_json(c);
        data["id"] = make_uid(ids, "c");
        data["dup_of"] = c.id;
        data["flip_v"] = !c.flip_v;
        data["blend_mode"] = "overlay";
        data["name"] = (c.name && !c.name->empty() ? *c.name : std::string("Texto")) + " (reflejo)";
        const double ny = py_round(y + block, 4);
        const Json& op_raw = st.contains("opacity") ? st.at("opacity") : Json(1);
        GC_TRY(opacity, to_py_float(py_truthy(op_raw) ? op_raw : Json(1), "style.opacity"));
        Json style = st;
        style["y"] = ny;
        style["opacity"] = 0.6 * opacity;
        data["style"] = std::move(style);
        // Solo se ve la parte cercana al texto: una máscara lineal que se funde hacia abajo.
        data["masks"] = Json::array(
            {normalize_mask(Json{{"type", "linear"}, {"x", 0.5}, {"y", ny}, {"feather", 0.12}, {"id", make_uid(ids, "m")}})});
        Json& kf = data["keyframes"];
        if (kf.is_object() && py_truthy(field(kf, "items"))) {
            Json& items = kf["items"];
            if (!items.is_array()) {
                return invalid("keyframes.items debe ser una lista de objetos");
            }
            for (Json& it : items) {
                if (!it.is_object()) {
                    return invalid("keyframes.items: cada keyframe debe ser un objeto");
                }
                if (!it.contains("props") || !it.at("props").is_object()) {
                    continue;
                }
                for (const char* key : {"y", "my"}) {
                    if (it["props"].contains(key)) {
                        GC_TRY(v, to_py_float(it["props"][key], key));
                        it["props"][key] = py_round(v + block, 4);
                    }
                }
            }
        }
        const std::string track = insert_track_after(tl, std::nullopt, "text", "Reflejo", ids);
        data["track_id"] = track;
        GC_TRY(clip, clip_from_json(data));
        out.changed.push_back(track);
        out.changed.push_back(clip.id);
        tl.clips.emplace_back(std::move(clip));
    }
    return out;
}

Result<Outcome> pass_through_text(Timeline& tl, const std::vector<Json>& clip_ids, const Json& params,
                                  IdSource& ids) {
    GC_TRY(sel, need(tl, clip_ids, {"text"}));
    Outcome out;
    for (const std::size_t idx : sel) {
        TimelineClip c = *tl.clips[idx];
        const double dur = clip_span_duration(c);
        GC_TRY(zoom_time, param_float(params, "zoom_time", 1.0));
        const double z = std::min(zoom_time, dur * 0.8);
        const KfProps base = clip_props_at(c, std::max(0.0, dur - z));
        upsert_keyframe_at(c, 0.0, Json::object(), std::nullopt, ids);
        upsert_keyframe_at(c, py_round(dur - z, 4), Json::object(), std::nullopt, ids);
        // Mismo tramo y misma curva: crece despacio y al final «atraviesa», apagándose.
        upsert_keyframe_at(c, py_round(dur, 4), Json{{"scale", base.get("scale") * kZoomTo}, {"opacity", 0.0}},
                           Interp::CubicIn, ids);
        out.changed.push_back(c.id);
        tl.clips[idx] = std::move(c);
    }
    return out;
}

// Instantes de timeline de los beats de los clips con ritmo (cada `every`).
Result<std::vector<double>> beat_times(const Timeline& tl) {
    std::vector<double> out;
    for (const auto& cp : tl.clips) {
        const TimelineClip& c = *cp;
        if (!c.beats || !c.beats->is_object() || c.beats->empty() || c.disabled) {
            continue;
        }
        const Json& b = *c.beats;
        const Json& every_raw = field(b, "every");
        const auto every_n = py_int(py_truthy(every_raw) ? every_raw : Json(1));
        if (!every_n) {
            return invalid("beats.every debe ser un entero");
        }
        const std::int64_t every = std::max<std::int64_t>(1, *every_n);
        const double sp = clip_speed(c);
        const Json& times_raw = field(b, "times");
        if (!py_truthy(times_raw)) {
            continue;
        }
        const auto times = py_iter(times_raw);
        if (!times) {
            return invalid("beats.times debe ser una lista");
        }
        for (std::size_t k = 0; k < times->size(); ++k) {
            if (static_cast<std::int64_t>(k) % every != 0) {
                continue;
            }
            const auto t = py_number((*times)[k]);
            if (!t) {
                return invalid("beats.times: cada beat debe ser un número");
            }
            if (!(c.in_point <= *t && *t <= c.out_point)) {
                continue;
            }
            const double rel = c.reverse ? c.out_point - *t : *t - c.in_point;
            out.push_back(py_round(c.start + rel / sp, 4));
        }
    }
    std::ranges::sort(out);
    return out;
}

Result<Outcome> film_strips(Timeline& tl, const std::vector<Json>& clip_ids, IdSource& ids) {
    GC_TRY(sel, need(tl, clip_ids, {"video", "image"}, 2, kFilmMax));
    std::ranges::stable_sort(sel, [&](std::size_t a, std::size_t b) {
        const auto& ca = *tl.clips[a];
        const auto& cb = *tl.clips[b];
        return ca.start != cb.start ? ca.start < cb.start : ca.id < cb.id;
    });
    const std::size_t n = sel.size();
    double s0 = tl.clips[sel.front()]->start;
    for (const std::size_t i : sel) {
        s0 = std::min(s0, tl.clips[i]->start);
    }
    GC_TRY(all_beats, beat_times(tl));
    std::vector<double> beats;
    for (const double t : all_beats) {
        if (t >= s0 - 1e-3) {
            beats.push_back(t);
        }
    }
    Outcome out;
    std::vector<double> starts;
    if (beats.size() >= n) {
        starts.assign(beats.begin(), beats.begin() + static_cast<std::ptrdiff_t>(n));
    } else {
        for (std::size_t i = 0; i < n; ++i) {
            starts.push_back(py_round(s0 + static_cast<double>(i) * kFilmStep, 4));
        }
        out.warnings.emplace_back("sin beats suficientes: entran cada 0,35 s (detecta los beats de la música "
                                  "para que entren a ritmo)");
    }
    std::optional<std::string> prev;
    for (std::size_t i = 0; i < n; ++i) {
        const std::string track = insert_track_after(tl, prev, "video", "Franja " + std::to_string(i + 1), ids);
        prev = track;
        TimelineClip& c = tl.clips[sel[i]].mut();
        c.track_id = track;
        c.start = starts[i];
        c.layout = "overlay";
        c.frame = "free";
        Json transform = c.transform && py_truthy(*c.transform) ? *c.transform : Json::object();
        transform["x"] = 0.5;
        transform["y"] = 0.5;
        c.transform = std::move(transform);
        const double nd = static_cast<double>(n);
        c.masks = {normalize_mask(Json{{"type", "film"},
                                       {"x", 0.5},
                                       {"y", py_round((static_cast<double>(i) + 0.5) / nd, 4)},
                                       {"w", 1.0},
                                       {"h", py_round(1 / nd, 4)},
                                       {"id", make_uid(ids, "m")}})};
        c.appear = i % 2 == 0 ? "slide_left" : "slide_right";
        out.changed.push_back(track);
        out.changed.push_back(c.id);
    }
    return out;
}

Result<Outcome> subject_pop(Timeline& tl, const std::vector<Json>& clip_ids, const Json& params,
                            IdSource& ids) {
    GC_TRY(sel, need(tl, clip_ids, {"video"}));
    const TimelineClip v = *tl.clips[sel.front()];
    const std::int64_t fps = std::max<std::int64_t>(1, tl.fps != 0 ? tl.fps : 30);
    const Json& frames_raw = params.contains("frames") ? params.at("frames") : Json(kPopFrames);
    const auto frames = py_int(py_truthy(frames_raw) ? frames_raw : Json(kPopFrames));
    if (!frames) {
        return invalid("frames debe ser un entero");
    }
    double lead = static_cast<double>(std::max<std::int64_t>(1, *frames)) / static_cast<double>(fps);
    const double start = std::max(0.0, v.start - lead);
    lead = v.start - start;
    if (lead <= 0) {
        return invalid("el clip empieza en 0: no hay plano anterior sobre el que adelantar el sujeto");
    }
    const double dur = lead + 0.3;
    const double sp = clip_speed(v);
    Json data = to_json(v);
    const std::string track = insert_track_after(tl, v.track_id, "video", "Sujeto", ids);
    const std::string prov = recommended_bg_provider(v); // vídeo → RVM: el sujeto es una persona
    data["id"] = make_uid(ids, "c");
    data["dup_of"] = v.id;
    data["track_id"] = track;
    data["start"] = py_round(start, 4);
    data["out_point"] = py_round(std::min(v.out_point, v.in_point + dur * sp), 4);
    data["name"] = (v.name && !v.name->empty() ? *v.name : std::string("Vídeo")) + " (sujeto)";
    data["bg_removal"] = Json{{"enabled", true},
                              {"mode", "auto"},
                              {"auto", Json{{"enabled", true},
                                            {"status", "idle"},
                                            {"provider", prov},
                                            {"mask_height", bg_provider_mask_height(prov)}}}};
    if (v.reverse) {
        data["in_point"] = py_round(std::max(v.in_point, v.out_point - dur * sp), 4);
        data["out_point"] = v.out_point;
    }
    GC_TRY(dup, clip_from_json(data));
    Outcome out{{track, dup.id}, {}};
    const std::string dup_id = dup.id;
    const std::string dup_name = dup.name.value_or("");
    tl.clips.emplace_back(std::move(dup));

    // Destello blanco en el corte: «barras» que cubren todo el cuadro, de 1 a 0.
    std::vector<std::string> created;
    const std::string ftrack = top_free_video_track(tl, v.start, 0.25, created, ids);
    const std::string flash_id = make_uid(ids, "c");
    GC_TRY(shape, normalize_shape(Json{{"type", "letterbox"}, {"bar", 0.5}, {"fill", "#ffffff"}}));
    Json raw = Json::object();
    raw["id"] = flash_id;
    raw["track_id"] = ftrack;
    raw["kind"] = "shape";
    raw["asset_kind"] = "shape";
    raw["asset_id"] = "letterbox";
    raw["filename"] = "";
    raw["name"] = "Destello";
    raw["start"] = py_round(v.start, 4);
    raw["in_point"] = 0.0;
    raw["out_point"] = 0.25;
    raw["source_duration"] = 0.25;
    raw["layout"] = "fill";
    raw["frame"] = "full";
    raw["shape"] = std::move(shape);
    GC_TRY(flash, clip_from_json(raw));
    upsert_keyframe_at(flash, 0.0, Json{{"opacity", 0.9}}, std::nullopt, ids);
    upsert_keyframe_at(flash, 0.25, Json{{"opacity", 0.0}}, Interp::EaseOut, ids);
    tl.clips.emplace_back(std::move(flash));
    out.changed.insert(out.changed.end(), created.begin(), created.end());
    out.changed.push_back(flash_id);

    out.warnings.push_back("genera el recorte IA del clip «" + dup_name +
                           "» (Eliminar fondo) para que se vea solo el sujeto");
    const auto lib = sfx_library();
    const std::vector<Json> wanted{
        Json{{"what", "Subida"},
             {"kind", "spot"},
             {"keywords", {"riser", "whoosh", "swoosh", "rise"}},
             {"start", 0.0},
             {"duration", 0.8},
             {"volume", 0.7}},
        Json{{"what", "Obturador"},
             {"kind", "spot"},
             {"keywords", {"shutter", "camera", "click", "snap"}},
             {"start", 0.0},
             {"duration", 0.5},
             {"volume", 0.8}}};
    auto [matched, missing] = match_sounds(wanted, lib->items);
    if (!matched.empty()) {
        for (Json& m : matched) {
            const auto it = lib->durations.find(py_str(m["sfx"]["id"]));
            m["sfx"]["duration"] = it != lib->durations.end() ? it->second : 0.0;
        }
        GC_TRY(r, run(tl, ops::AddSoundDesign{dup_id, Json(matched)}, ids));
        tl = std::move(r.timeline);
        out.changed.insert(out.changed.end(), r.changed.begin(), r.changed.end());
    }
    if (!missing.empty()) {
        std::string list;
        for (const Json& m : missing) {
            std::string what = m.at("what").get<std::string>();
            detail::ascii_lower_inplace(what);
            list += (list.empty() ? "" : ", ") + what;
        }
        out.warnings.push_back("no hay en tu biblioteca: " + list);
    }
    return out;
}

// dict(x or {}): un dict o una lista de pares clave-valor.
Result<Json> params_dict(const Json& v) {
    if (!py_truthy(v)) {
        return Json::object();
    }
    if (v.is_object()) {
        return v;
    }
    if (v.is_array()) {
        Json out = Json::object();
        for (const Json& el : v) {
            if (!el.is_array() || el.size() != 2 || el[0].is_array() || el[0].is_object()) {
                return invalid("params debe ser un diccionario");
            }
            out[py_str(el[0])] = el[1];
        }
        return out;
    }
    return invalid("params debe ser un diccionario");
}

} // namespace

Result<EditResult> run(const Timeline& tl, const ops::ApplyRecipe& op, IdSource& ids) {
    if (!op.recipe.is_string() ||
        std::ranges::find(kRecipeIds, op.recipe.get<std::string>()) == kRecipeIds.end()) {
        std::string all;
        for (const auto id : kRecipeIds) {
            all += (all.empty() ? "'" : ", '") + std::string(id) + "'";
        }
        return invalid("receta desconocida: " + py_str(op.recipe) + " (usa [" + all + "])");
    }
    const std::string recipe = op.recipe.get<std::string>();
    std::vector<Json> clip_ids; // list(clip_ids or [])
    if (py_truthy(op.clip_ids)) {
        auto list = py_iter(op.clip_ids);
        if (!list) {
            return invalid("clip_ids debe ser una lista");
        }
        clip_ids = std::move(*list);
    }
    GC_TRY(params, params_dict(op.params));
    Timeline out = tl;
    Result<Outcome> r = recipe == "cinema_grade"      ? cinema_grade(out, ids)
                        : recipe == "text_reflection" ? text_reflection(out, clip_ids, ids)
                        : recipe == "pass_through_text" ? pass_through_text(out, clip_ids, params, ids)
                        : recipe == "film_strips"       ? film_strips(out, clip_ids, ids)
                                                        : subject_pop(out, clip_ids, params, ids);
    if (!r) {
        return std::unexpected(r.error());
    }
    return EditResult{std::move(out), std::move(r->changed), std::move(r->warnings)};
}

} // namespace gc::core::detail
