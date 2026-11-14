#include "gc/core/scene_direction.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "gc/core/segment_context.hpp"
#include "gc/core/timeline_rules.hpp"
#include "json_reader.hpp"
#include "py.hpp"
#include "scene_util.hpp"

namespace gc::core::scene {

namespace {

using detail::clean_text;
using detail::less_nan_last;
using detail::max0;
using detail::or0;
using detail::py_fixed;
using detail::py_float;
using detail::py_max;
using detail::py_min;
using detail::r3;
using detail::py_int;
using detail::py_iter;
using detail::py_len;
using detail::py_lower;
using detail::py_round;
using detail::py_rstrip;
using detail::py_slice;
using detail::py_split;
using detail::py_str;
using detail::py_strip;
using detail::py_strip_accents;
using detail::py_truthy;
using detail::utf8_next;

constexpr int kVersion = 1;
constexpr int kBlueprintVersion = 1;
constexpr double kPauseSplit = 0.6; // s de silencio entre palabras que cortan frase
constexpr double kTargetSeg = 6.0;  // s objetivo por tramo en la división automática
constexpr double kMinSeg = 2.0;
constexpr double kMaxSeg = 10.0;

const std::vector<std::string_view>& component_sources() {
    static const std::vector<std::string_view> v{"material", "motion", "stickman", "text", "graphic", "keep"};
    return v;
}

constexpr std::array<std::string_view, 4> kPlanAllKeys{"mode", "composition_intent", "complexity", "no_visual"};

std::unexpected<Error> crash(std::string what) { return fail(ErrorCode::Internal, std::move(what)); }

bool in_list(const std::vector<std::string_view>& list, const Json& v) {
    return v.is_string() && std::ranges::find(list, v.get<std::string>()) != list.end();
}

const Json& get(const Json& obj, std::string_view key) {
    static const Json none;
    if (obj.is_object()) {
        const auto it = obj.find(key);
        if (it != obj.end()) {
            return *it;
        }
    }
    return none;
}

// _clean(text, limit): str(text or "").
std::string clean(const Json& text, std::size_t limit) {
    return clean_text(py_truthy(text) ? py_str(text) : std::string(), limit);
}

// re.match(r"^[A-Za-z0-9_-]{3,40}$", sid): el «$» también vale antes de un «\n» final.
bool valid_id(std::string_view sid) {
    if (!sid.empty() && sid.back() == '\n') {
        sid.remove_suffix(1);
    }
    if (sid.size() < 3 || sid.size() > 40) {
        return false;
    }
    return std::ranges::all_of(sid, [](char c) { return detail::ascii_alnum(c) || c == '_' || c == '-'; });
}

bool same_id(const Json& seg, std::string_view id) {
    const Json& v = get(seg, "id");
    return v.is_string() && v.get_ref<const std::string&>() == id;
}

std::string new_id(IdSource& ids) { return "sd_" + ids.hex32().substr(0, 8); }

// _complexity: int(v) entre 1 y 5, o None. int() de infinito es un OverflowError (no se captura).
Result<Json> complexity(const Json& v) {
    if (v.is_number_float()) {
        const double x = v.get<double>();
        if (std::isinf(x)) {
            return crash("int() de infinito en complexity");
        }
        if (std::isnan(x)) {
            return Json();
        }
        const double t = std::trunc(x);
        return t >= 1.0 && t <= 5.0 ? Json(static_cast<std::int64_t>(t)) : Json();
    }
    const auto n = py_int(v);
    return n && *n >= 1 && *n <= 5 ? Json(*n) : Json();
}

// _material_ref.
std::optional<Json> material_ref(const Json& raw) {
    if (!raw.is_object()) {
        return std::nullopt;
    }
    const Json& kind = get(raw, "kind");
    const Json& id = get(raw, "id");
    if (!(kind == "clips" || kind == "images") || id.is_null()) {
        return std::nullopt;
    }
    return Json{{"kind", kind}, {"id", py_str(id)}, {"scope", get(raw, "scope") == "library" ? "library" : "project"}};
}

// `for x in (v or [])`: los elementos que recorre Python; un número o un bool verdadero es un crash.
Result<std::vector<Json>> iter_or_empty(const Json& v, const char* what) {
    if (!py_truthy(v)) {
        return std::vector<Json>{};
    }
    auto items = py_iter(v);
    if (!items) {
        return crash(std::string("no se puede recorrer ") + what);
    }
    return std::move(*items);
}

// --- El guion ----------------------------------------------------------------------------------

struct WordT {
    std::string text;
    double start = 0.0;
    double end = 0.0;
};

bool is_caption(const TimelineClip& c) { return is_caption_clip(c); }

// sc._caption_words: las palabras de los subtítulos en tiempo de timeline.
std::vector<WordT> caption_words(const Timeline& tl) {
    std::vector<WordT> out;
    for (const auto& cow : tl.clips) {
        const TimelineClip& c = *cow;
        if (!is_caption(c)) {
            continue;
        }
        const double c0 = c.start;
        if (!c.words.empty()) {
            for (const Word& w : c.words) {
                out.push_back({w.text, c0 + w.start, c0 + w.end});
            }
        } else if (const std::string t = py_strip(c.text.value_or("")); !t.empty()) {
            out.push_back({t, c0, c0 + gc::core::clip_timeline_duration(c)});
        }
    }
    std::ranges::stable_sort(out, less_nan_last, &WordT::start);
    return out;
}

// sc._material_words: la transcripción del material de los clips de vídeo, en tiempo de timeline.
std::vector<WordT> material_words(const Project& proj, const Timeline& tl) {
    std::unordered_map<std::string, const ClipInfo*> by_index;
    for (const ClipInfo& m : proj.clips) {
        by_index[std::to_string(m.index)] = &m; // el último gana, como el dict de Python
    }
    std::vector<WordT> out;
    for (const auto& cow : tl.clips) {
        const TimelineClip& c = *cow;
        if (c.kind != "video") {
            continue;
        }
        const auto it = by_index.find(c.asset_id);
        if (it == by_index.end() || !it->second->transcript) {
            continue;
        }
        const double speed = gc::core::clip_speed(c);
        const double inp = c.in_point;
        const double outp = c.out_point;
        const auto to_tl = [&](double s) { return c.start + (s - inp) / speed; };
        for (const TranscriptSegment& seg : it->second->transcript->segments) {
            std::vector<WordT> items;
            if (!seg.words.empty()) {
                for (const Word& w : seg.words) {
                    items.push_back({w.text, w.start, w.end});
                }
            } else {
                items.push_back({seg.text, seg.start, seg.end});
            }
            for (const WordT& w : items) {
                if (w.end <= inp || w.start >= outp) {
                    continue;
                }
                out.push_back({w.text, to_tl(inp > w.start ? inp : w.start), to_tl(outp < w.end ? outp : w.end)});
            }
        }
    }
    std::ranges::stable_sort(out, less_nan_last, &WordT::start);
    return out;
}

// Los caracteres de un texto UTF-8 con el byte en el que empieza cada uno (y uno más, el final).
struct Chars {
    std::vector<char32_t> cps;
    std::vector<std::size_t> at;
};

Chars chars_of(std::string_view s) {
    Chars out;
    std::size_t i = 0;
    while (i < s.size()) {
        out.at.push_back(i);
        out.cps.push_back(utf8_next(s, i));
    }
    out.at.push_back(s.size());
    return out;
}

bool sentence_punct(char32_t c) { return c == U'.' || c == U'!' || c == U'?' || c == U'…'; }

// SENTENCE_END.search(texto sin espacios): r"[.!?…:;]$|[.!?…][\"»”)]?$".
bool sentence_end(std::string_view stripped) {
    const Chars ch = chars_of(stripped);
    const std::size_t n = ch.cps.size();
    if (n == 0) {
        return false;
    }
    const char32_t last = ch.cps[n - 1];
    if (sentence_punct(last) || last == U':' || last == U';') {
        return true;
    }
    const bool closer = last == U'"' || last == U'»' || last == U'”' || last == U')';
    return closer && n >= 2 && sentence_punct(ch.cps[n - 2]);
}

Json unit(double start, double end, const std::string& text) {
    return Json{{"start", r3(start)}, {"end", r3(end)}, {"text", clean_text(text, 600)}};
}

// _group_words: las palabras en frases (corta en una pausa de más de 0,6 s o en un final de frase).
Json group_words(const std::vector<WordT>& words) {
    std::vector<std::vector<const WordT*>> units;
    std::vector<const WordT*> cur;
    for (const WordT& w : words) {
        if (!cur.empty() && w.start - cur.back()->end > kPauseSplit) {
            units.push_back(std::move(cur));
            cur.clear();
        }
        cur.push_back(&w);
        if (sentence_end(py_strip(w.text))) {
            units.push_back(std::move(cur));
            cur.clear();
        }
    }
    if (!cur.empty()) {
        units.push_back(std::move(cur));
    }
    Json out = Json::array();
    for (const auto& u : units) {
        std::string text;
        for (std::size_t i = 0; i < u.size(); ++i) {
            text += (i ? " " : "") + u[i]->text;
        }
        out.push_back(unit(u.front()->start, u.back()->end, text));
    }
    return out;
}

bool chunk_char(char32_t c) { return !sentence_punct(c) && c != U'\n'; }

// _audio_units: sin tiempos por palabra, las frases del texto del audio con tiempos proporcionales.
Json audio_units(const Project& proj, const Timeline& tl) {
    std::unordered_map<std::string, const AudioInfo*> audios;
    for (const AudioInfo& a : proj.audios) {
        audios[a.id] = &a;
    }
    for (const auto& cow : tl.clips) {
        const TimelineClip& c = *cow;
        if (c.kind != "audio") {
            continue;
        }
        const auto it = audios.find(c.asset_id);
        const AudioInfo* info = it == audios.end() ? nullptr : it->second;
        const std::string text = info ? py_strip(info->text.value_or("")) : std::string();
        double src_dur = c.source_duration;
        if (src_dur == 0.0) { // float(...) or float(info.duration or 0.0)
            src_dur = info && info->duration ? *info->duration : 0.0;
        }
        if (text.empty() || src_dur <= 0) {
            continue;
        }
        const double c0 = c.start;
        const double speed = c.speed == 0.0 ? 1.0 : c.speed; // float(c.speed or 1.0) or 1.0
        const double inp = c.in_point;
        const double c_end = c0 + gc::core::clip_timeline_duration(c);
        const Chars ch = chars_of(text);
        const auto len = static_cast<double>(ch.cps.size());
        Json out = Json::array();
        // re.finditer(r"[^.!?…\n]+[.!?…]*", text)
        std::size_t i = 0;
        const std::size_t n = ch.cps.size();
        while (i < n) {
            if (!chunk_char(ch.cps[i])) {
                ++i;
                continue;
            }
            const std::size_t m0 = i;
            while (i < n && chunk_char(ch.cps[i])) {
                ++i;
            }
            while (i < n && sentence_punct(ch.cps[i])) {
                ++i;
            }
            const std::size_t m1 = i;
            const std::string chunk = py_strip(std::string_view(text).substr(ch.at[m0], ch.at[m1] - ch.at[m0]));
            if (chunk.empty()) {
                continue;
            }
            const double a0 = static_cast<double>(m0) / len * src_dur;
            const double a1 = static_cast<double>(m1) / len * src_dur;
            const double t0 = c0 + (a0 - inp) / speed;
            const double t1 = c0 + (a1 - inp) / speed;
            if (t1 <= c0 || t0 >= c_end) {
                continue;
            }
            out.push_back(unit(t0 > c0 ? t0 : c0, t1, chunk));
        }
        if (!out.empty()) {
            return out;
        }
    }
    return Json::array();
}

} // namespace

// --- Catálogos ------------------------------------------------------------------------------------

const std::vector<DirectionInfo>& directions() {
    static const std::vector<DirectionInfo> v{
        {"sketchbook", "Sketchbook",
         "El cuaderno de trabajo del creador: lápiz, tinta, flechas, tachones y notas al margen."},
        {"editorial_magazine", "Editorial Magazine",
         "Revista de alta gama: titulares enormes, fotos recortadas, columnas y líneas finas."},
        {"field_notes", "Scientific Field Notes",
         "Cuaderno de explorador: muestras, etiquetas, coordenadas, mediciones y círculos de observación."},
        {"museum_archive", "Museum Archive",
         "Fichas de archivo, fotos antiguas, sellos y etiquetas curatoriales sobre fondo marfil."},
        {"newspaper", "Newspaper / Breaking Press",
         "Periódico moderno: titulares gigantes, columnas, recortes y flechas editoriales."},
        {"paper_collage", "Paper Collage",
         "Todo construido con recortes físicos: fotos, formas, cinta adhesiva y letras recortadas."},
        {"blueprint", "Blueprint / Engineering",
         "Planos técnicos: cotas, líneas de construcción, esquemas y anotaciones en azul y blanco."},
        {"whiteboard", "Classroom / Whiteboard",
         "Pizarra blanca con rotulador: fórmulas, diagramas simples, círculos y flechas."},
        {"retro_edu_film", "Retro Educational Film",
         "Documental educativo de los 60–80: grano, tipografía retro, ilustración plana y diagramas."},
        {"swiss", "Minimal Swiss Editorial",
         "Diseño suizo: retícula estricta, sans-serif, espacio negativo y bloques geométricos."},
        {"infographic_news", "Infographic Newspaper",
         "Infografía editorial animada: datos grandes, porcentajes, gráficos, flechas y pequeños iconos."},
        {"investigation_board", "Investigation Board",
         "Tablón de investigación: fotos, documentos, post-its, chinchetas e hilos que conectan."},
        {"ink_wash", "Ink & Wash", "Tinta negra y manchas de acuarela: trazos imperfectos que aparecen progresivamente."},
        {"vintage_poster", "Vintage Scientific Poster",
         "Cartel científico antiguo: tipografía clásica, grabados, diagramas y textura de impresión."},
        {"technical_manual", "Technical Manual",
         "Manual de instrucciones industrial: numeración, piezas despiezadas, llamadas y flechas."},
        {"documentary", "Modern Documentary",
         "Documental premium: fotografía, tipografía discreta, mapas, documentos y cortes cinematográficos."},
        {"minimal_clean", "Minimal Clean",
         "El estilo limpio actual (superficies claras, Inter, un acento). Para overlays neutros."},
    };
    return v;
}

bool is_direction(std::string_view key) {
    return std::ranges::any_of(directions(), [key](const DirectionInfo& d) { return d.key == key; });
}

const std::vector<ModeInfo>& modes() {
    static const std::vector<ModeInfo> v{
        {"propose", "Necesita propuesta", "Propón la mejor representación visual para este tramo; decides tú el enfoque."},
        {"explain", "Explicar lo narrado",
         "Haz ENTENDER lo que dice la voz: el mecanismo, la causa o el dato. Claridad antes que estética."},
        {"represent", "Representar la idea",
         "Traduce la idea a una imagen o metáfora visual potente; no la expliques con texto."},
        {"reinforce", "Repetir / reforzar",
         "Vuelve a la escena del tramo de referencia (mismo lenguaje, personajes y elementos) "
         "para reforzar la idea; varía lo mínimo."},
        {"material", "Usar material", "El protagonista visual es el material indicado; el resto solo lo acompaña."},
    };
    return v;
}

const std::vector<std::string_view>& statuses() {
    static const std::vector<std::string_view> v{"empty", "ready", "generated", "placed"};
    return v;
}

const std::vector<std::string_view>& material_roles() {
    static const std::vector<std::string_view> v{"full",       "broll",     "overlay",        "pip",
                                                 "side_panel", "circular",  "background",     "reference",
                                                 "motion_element", "transition", "supporting"};
    return v;
}

const std::vector<std::string_view>& vocabulary() {
    static const std::vector<std::string_view> v{"movie_footage",   "paper_animation", "stickman",
                                                 "motion_graphic",  "diagram",         "handwritten_word",
                                                 "generated_image", "text",            "chart"};
    return v;
}

// --- El documento ------------------------------------------------------------------------------

std::optional<Json> normalize_component(const Json& raw) {
    if (!raw.is_object()) {
        return std::nullopt;
    }
    const std::optional<Json> mat = material_ref(get(raw, "material"));
    std::string src;
    if (in_list(component_sources(), get(raw, "source"))) {
        src = get(raw, "source").get<std::string>();
    } else if (mat) {
        src = "material";
    } else {
        return std::nullopt;
    }
    const Json& role = get(raw, "role");
    Json comp{{"source", src}, {"role", in_list(material_roles(), role) ? role : Json()}, {"note", clean(get(raw, "note"), 200)}};
    if (src == "material") {
        if (!mat) {
            return std::nullopt;
        }
        comp["material"] = *mat;
        const Json& rng = get(raw, "source_range");
        if (rng.is_object() && rng.contains("in")) {
            const auto in = py_float(rng["in"]);
            const auto out = in && rng.contains("out") ? py_float(rng["out"]) : std::nullopt;
            if (in && out) {
                comp["source_range"] = Json{{"in", r3(max0(*in))}, {"out", r3(*out)}};
            }
        }
    }
    return comp;
}

Result<Json> normalize_components(const Json& raw) {
    auto items = iter_or_empty(raw, "components");
    if (!items) {
        return std::unexpected(items.error());
    }
    Json out = Json::array();
    for (const Json& c : *items) {
        if (auto n = normalize_component(c); n && out.size() < 8) {
            out.push_back(std::move(*n));
        }
    }
    return out;
}

Result<std::optional<Json>> normalize_segment(SceneEnv& env, const Json& raw) {
    if (!raw.is_object()) {
        return std::nullopt;
    }
    const auto s = py_float(get(raw, "start"));
    const auto e = s ? py_float(get(raw, "end")) : std::nullopt;
    if (!s || !e) {
        return std::nullopt;
    }
    if (*e - *s < 0.2) {
        return std::nullopt;
    }
    auto raw_mats = iter_or_empty(get(raw, "materials"), "materials");
    if (!raw_mats) {
        return std::unexpected(raw_mats.error());
    }
    Json mats = Json::array();
    for (const Json& m : *raw_mats) {
        if (auto ref = material_ref(m); ref && mats.size() < 8) {
            mats.push_back(std::move(*ref));
        }
    }
    const Json& raw_mode = get(raw, "mode");
    if (raw_mode.is_array() || raw_mode.is_object()) {
        return crash("mode no hashable"); // `[] in MODES`: TypeError
    }
    Json mode;
    if (raw_mode.is_string() &&
        std::ranges::any_of(modes(), [&](const ModeInfo& m) { return m.key == raw_mode.get<std::string>(); })) {
        mode = raw_mode;
    }
    std::string comp_intent = clean(get(raw, "composition_intent"), 1000);
    const bool no_visual = py_truthy(get(raw, "no_visual"));
    std::string status = in_list(statuses(), get(raw, "status")) ? get(raw, "status").get<std::string>() : "empty";
    if (status == "empty" &&
        (!mode.is_null() || !clean(get(raw, "instruction"), 10).empty() || !comp_intent.empty() || no_visual)) {
        status = "ready";
    }
    const Json& raw_id = get(raw, "id");
    const std::string sid = py_truthy(raw_id) ? py_str(raw_id) : std::string();

    // El orden del dict de Python: el id (uuid4) primero; complexity y components pueden fallar antes
    // de updated_at (time.time()).
    Json out{{"id", valid_id(sid) ? sid : new_id(env.ids)}, {"start", r3(max0(*s))}, {"end", r3(*e)}};
    out["text"] = clean(get(raw, "text"), 2000);
    const Json& ts = get(raw, "text_source");
    out["text_source"] = py_slice(py_truthy(ts) ? py_str(ts) : std::string("none"), 30);
    out["mode"] = mode;
    out["instruction"] = clean(get(raw, "instruction"), 2000);
    out["strict"] = py_truthy(get(raw, "strict"));
    out["materials"] = std::move(mats);
    const Json& ref = get(raw, "reference_id");
    out["reference_id"] = py_truthy(ref) ? Json(py_str(ref)) : Json();
    out["status"] = status;
    const Json& cid = get(raw, "composition_id");
    out["composition_id"] = py_truthy(cid) ? cid : Json();
    const Json& pcid = get(raw, "placed_clip_id");
    out["placed_clip_id"] = py_truthy(pcid) ? pcid : Json();
    out["composition_intent"] = std::move(comp_intent);
    auto cx = complexity(get(raw, "complexity"));
    if (!cx) {
        return std::unexpected(cx.error());
    }
    out["complexity"] = std::move(*cx);
    out["no_visual"] = no_visual;
    auto comps = normalize_components(get(raw, "components"));
    if (!comps) {
        return std::unexpected(comps.error());
    }
    out["components"] = std::move(*comps);
    const Json& upd = get(raw, "updated_at");
    out["updated_at"] = py_truthy(upd) ? upd : Json(env.now());
    return out;
}

Result<Json> normalize_doc(SceneEnv& env, const Json& raw) {
    std::vector<Json> segs;
    if (raw.is_object()) {
        auto items = iter_or_empty(get(raw, "segments"), "segments");
        if (!items) {
            return std::unexpected(items.error());
        }
        for (const Json& s : *items) {
            auto n = normalize_segment(env, s);
            if (!n) {
                return std::unexpected(n.error());
            }
            if (*n && std::ranges::none_of(segs, [&](const Json& x) { return x["id"] == (**n)["id"]; })) {
                segs.push_back(std::move(**n));
            }
        }
    }
    std::ranges::stable_sort(segs, [](const Json& a, const Json& b) {
        const double as = a["start"].get<double>();
        const double bs = b["start"].get<double>();
        if (less_nan_last(as, bs) || less_nan_last(bs, as)) {
            return less_nan_last(as, bs);
        }
        return less_nan_last(a["end"].get<double>(), b["end"].get<double>());
    });
    std::unordered_set<std::string> ids;
    for (const Json& x : segs) {
        ids.insert(x["id"].get<std::string>());
    }
    for (Json& x : segs) { // referencias rotas fuera
        const Json& ref = x["reference_id"];
        if (!ref.is_string() || !ids.contains(ref.get<std::string>()) || ref == x["id"]) {
            x["reference_id"] = nullptr;
        }
    }
    return Json{{"version", kVersion}, {"segments", Json(std::move(segs))}};
}

Result<Json> load(SceneEnv& env, const Project& project) {
    return normalize_doc(env, py_truthy(project.scene_directions) ? project.scene_directions : Json::object());
}

Result<Json> save(SceneEnv& env, ProjectsStore& store, std::string_view pid, const Json& raw) {
    auto doc = normalize_doc(env, raw);
    if (!doc) {
        return doc;
    }
    auto saved = store.save_scene_directions(pid, *doc);
    if (!saved) {
        return std::unexpected(saved.error());
    }
    if (!*saved) {
        return fail(ErrorCode::NotFound, "Proyecto no encontrado.");
    }
    return doc;
}

namespace {

Result<Project> project_of(const ProjectsStore& store, std::string_view pid) {
    auto loaded = store.get_project(pid);
    if (!loaded) {
        return std::unexpected(loaded.error());
    }
    if (!*loaded) {
        return fail(ErrorCode::NotFound, "Proyecto no encontrado.");
    }
    return std::move(**loaded);
}

} // namespace

Result<Json> update_segment(SceneEnv& env, ProjectsStore& store, std::string_view pid, std::string_view segment_id,
                            const Json& patch) {
    auto proj = project_of(store, pid);
    if (!proj) {
        return std::unexpected(proj.error());
    }
    auto doc = load(env, *proj);
    if (!doc) {
        return doc;
    }
    for (Json& s : (*doc)["segments"]) {
        if (!same_id(s, segment_id)) {
            continue;
        }
        if (!patch.is_object()) {
            return crash("patch no es un dict"); // {**s, **patch}: TypeError
        }
        Json merged = s;
        for (const auto& [k, v] : patch.items()) {
            merged[k] = v;
        }
        merged["id"] = std::string(segment_id);
        merged["updated_at"] = env.now();
        s = std::move(merged);
        return save(env, store, pid, *doc);
    }
    return fail(ErrorCode::NotFound, "Tramo no encontrado.");
}

Result<Json> apply_plan_all(SceneEnv& env, ProjectsStore& store, std::string_view pid, const Json& patches) {
    auto proj = project_of(store, pid);
    if (!proj) {
        return std::unexpected(proj.error());
    }
    auto doc = load(env, *proj);
    if (!doc) {
        return doc;
    }
    auto items = iter_or_empty(patches, "patches");
    if (!items) {
        return std::unexpected(items.error());
    }
    std::map<std::string, Json> by_id;
    for (const Json& p : *items) {
        if (p.is_object() && py_truthy(get(p, "id"))) {
            by_id[py_str(p["id"])] = p;
        }
    }
    std::int64_t changed = 0;
    for (Json& s : (*doc)["segments"]) {
        const auto it = by_id.find(s["id"].get<std::string>());
        if (it == by_id.end()) {
            continue;
        }
        bool any = false;
        Json merged = s;
        for (const std::string_view k : kPlanAllKeys) {
            if (it->second.contains(k)) {
                merged[std::string(k)] = it->second[std::string(k)];
                any = true;
            }
        }
        if (any) {
            merged["updated_at"] = env.now();
            s = std::move(merged);
            ++changed;
        }
    }
    auto saved = save(env, store, pid, *doc); // normaliza (descarta mode/complexity inválidos)
    if (!saved) {
        return saved;
    }
    (*saved)["changed"] = changed;
    return saved;
}

Result<std::pair<Json, Json>> get_segment(SceneEnv& env, const Project& project, std::string_view segment_id) {
    auto doc = load(env, project);
    if (!doc) {
        return std::unexpected(doc.error());
    }
    for (const Json& s : (*doc)["segments"]) {
        if (same_id(s, segment_id)) {
            Json seg = s;
            return std::pair<Json, Json>(std::move(seg), std::move(*doc));
        }
    }
    return fail(ErrorCode::NotFound, "Tramo no encontrado.");
}

// --- La dirección visual global ----------------------------------------------------------------

Json normalize_blueprint(SceneEnv& env, const Json& raw) {
    if (!raw.is_object()) {
        return Json::object();
    }
    Json out = Json::object();
    const Json& rd = get(raw, "direction");
    const std::string direction = py_strip(py_truthy(rd) ? py_str(rd) : std::string());
    if (is_direction(direction)) {
        out["direction"] = direction;
    }
    const Json& ov = get(raw, "direction_overrides");
    if (ov.is_object()) {
        Json clean_ov = Json::object();
        for (const char* k : {"accent", "bg", "surface", "ink", "secondary", "notes"}) {
            const Json& v = get(ov, k);
            if (v.is_string()) {
                if (const std::string t = py_strip(v.get<std::string>()); !t.empty()) {
                    clean_ov[k] = py_slice(t, 200);
                }
            }
        }
        const Json& forb = get(ov, "forbidden");
        if (forb.is_array()) {
            Json items = Json::array();
            for (const Json& x : forb) {
                if (!py_strip(py_truthy(x) ? py_str(x) : std::string()).empty() && items.size() < 12) {
                    items.push_back(clean(x, 80));
                }
            }
            if (!items.empty()) {
                clean_ov["forbidden"] = std::move(items);
            }
        }
        if (!clean_ov.empty()) {
            out["direction_overrides"] = std::move(clean_ov);
        }
    }
    if (std::string identity = clean(get(raw, "identity"), 400); !identity.empty()) {
        out["identity"] = std::move(identity);
    }
    // Una lista de textos: el de un texto suelto (vocabulary lo parte por «,» y «;»; rules lo toma entero).
    const auto items_of = [](const Json& v, bool split, std::size_t limit, std::size_t max_items) {
        std::vector<Json> src;
        if (v.is_string()) {
            if (split) {
                std::string cur;
                for (const char c : v.get<std::string>()) {
                    if (c == ',' || c == ';') {
                        src.emplace_back(cur);
                        cur.clear();
                    } else {
                        cur += c;
                    }
                }
                src.emplace_back(cur);
            } else {
                src.push_back(v);
            }
        } else if (v.is_array()) {
            src.assign(v.begin(), v.end());
        } else {
            return Json();
        }
        Json items = Json::array();
        for (const Json& x : src) {
            if (!py_strip(py_truthy(x) ? py_str(x) : std::string()).empty() && items.size() < max_items) {
                items.push_back(clean(x, limit));
            }
        }
        return items.empty() ? Json() : items;
    };
    if (Json vocab = items_of(get(raw, "vocabulary"), true, 40, 12); !vocab.is_null()) {
        out["vocabulary"] = std::move(vocab);
    }
    if (Json rules = items_of(get(raw, "rules"), false, 160, 10); !rules.is_null()) {
        out["rules"] = std::move(rules);
    }
    if (std::string curve = clean(get(raw, "intensity_curve"), 300); !curve.empty()) {
        out["intensity_curve"] = std::move(curve);
    }
    if (out.empty()) {
        return Json::object();
    }
    out["version"] = kBlueprintVersion;
    out["source"] = py_str(get(raw, "source")) == "ai" ? "ai" : "manual";
    const Json& upd = get(raw, "updated_at");
    out["updated_at"] = py_truthy(upd) ? upd : Json(env.now());
    return out;
}

Json load_blueprint(SceneEnv& env, const Project& project) {
    return normalize_blueprint(env, py_truthy(project.visual_blueprint) ? project.visual_blueprint : Json::object());
}

Result<Json> save_blueprint(SceneEnv& env, ProjectsStore& store, std::string_view pid, const Json& raw) {
    Json bp = normalize_blueprint(env, raw);
    auto saved = store.save_visual_blueprint(pid, bp);
    if (!saved) {
        return std::unexpected(saved.error());
    }
    if (!*saved) {
        return fail(ErrorCode::NotFound, "Proyecto no encontrado.");
    }
    return bp;
}

// --- El guion y el estado ------------------------------------------------------------------------

double timeline_duration(const Project& project) {
    if (!project.timeline) {
        return 0.0;
    }
    std::optional<double> best;
    for (const auto& cow : project.timeline->clips) {
        const double v = cow->start + gc::core::clip_timeline_duration(*cow);
        if (!best || v > *best) { // max() de Python: se queda con el primero de los mayores
            best = v;
        }
    }
    return r3(best.value_or(0.0));
}

std::pair<Json, std::string> script_units(const Project& project) {
    if (!project.timeline) {
        return {Json::array(), "none"};
    }
    const Timeline& tl = *project.timeline;
    if (auto words = caption_words(tl); !words.empty()) {
        return {group_words(words), "captions"};
    }
    if (auto words = material_words(project, tl); !words.empty()) {
        return {group_words(words), "transcript"};
    }
    Json units = audio_units(project, tl);
    if (!units.empty()) {
        return {std::move(units), "audio_text_estimate"};
    }
    return {Json::array(), "none"};
}

std::string text_in_range(const Json& units, double start, double end) {
    std::string out;
    bool first = true;
    for (const Json& u : units) {
        const double mid = (u["start"].get<double>() + u["end"].get<double>()) / 2;
        if (start - 1e-3 <= mid && mid < end) {
            out += (first ? "" : " ") + u["text"].get<std::string>();
            first = false;
        }
    }
    return out;
}

Result<Json> auto_segments(SceneEnv& env, const Project& project, const Json& keep_raw) {
    auto [units, source] = script_units(project);
    auto keep_items = iter_or_empty(keep_raw, "keep");
    if (!keep_items) {
        return std::unexpected(keep_items.error());
    }
    std::vector<Json> keep;
    for (const Json& s : *keep_items) {
        if (!s.is_object()) {
            return crash("un tramo de keep no es un dict"); // s.get: AttributeError
        }
        if (py_truthy(get(s, "mode")) || py_truthy(get(s, "instruction")) || py_truthy(get(s, "composition_id"))) {
            keep.push_back(s);
        }
    }
    std::vector<std::pair<double, double>> covered;
    for (const Json& s : keep) {
        covered.emplace_back(s["start"].get<double>(), s["end"].get<double>());
    }
    const auto is_covered = [&covered](const Json& u) {
        const double mid = (u["start"].get<double>() + u["end"].get<double>()) / 2;
        return std::ranges::any_of(covered, [mid](const auto& ab) { return ab.first - 1e-3 <= mid && mid < ab.second; });
    };

    std::vector<Json> out = keep;
    std::vector<const Json*> group;
    const auto flush = [&] {
        if (group.empty()) {
            return;
        }
        std::string text;
        for (std::size_t i = 0; i < group.size(); ++i) {
            text += (i ? " " : "") + (*group[i])["text"].get<std::string>();
        }
        out.push_back(Json{{"id", new_id(env.ids)},
                           {"start", (*group.front())["start"]},
                           {"end", (*group.back())["end"]},
                           {"text", std::move(text)},
                           {"text_source", source},
                           {"mode", nullptr},
                           {"instruction", ""},
                           {"strict", false},
                           {"materials", Json::array()},
                           {"reference_id", nullptr},
                           {"status", "empty"}});
        group.clear();
    };
    for (const Json& u : units) {
        if (is_covered(u)) {
            flush();
            continue;
        }
        const double span = u["end"].get<double>() - (group.empty() ? u : *group.front())["start"].get<double>();
        if (!group.empty() && span > kMaxSeg) {
            flush();
        }
        group.push_back(&u);
        if ((*group.back())["end"].get<double>() - (*group.front())["start"].get<double>() >= kTargetSeg) {
            flush();
        }
    }
    flush();

    // Los tramos demasiado cortos se unen al anterior si es contiguo y sin dirección.
    std::ranges::stable_sort(out, [](const Json& a, const Json& b) {
        return less_nan_last(a["start"].get<double>(), b["start"].get<double>());
    });
    const auto directed = [](const Json& s) { return py_truthy(get(s, "mode")) || py_truthy(get(s, "instruction")); };
    std::vector<Json> merged;
    for (Json& s : out) {
        if (!merged.empty()) {
            Json& prev = merged.back();
            if (s["end"].get<double>() - s["start"].get<double>() < kMinSeg && !directed(s) && !directed(prev) &&
                s["start"].get<double>() - prev["end"].get<double>() < 1.0) {
                prev["end"] = s["end"];
                prev["text"] = py_strip(py_str(prev["text"]) + " " + py_str(s["text"]));
                continue;
            }
        }
        merged.push_back(std::move(s));
    }
    // Sin huecos pequeños: los silencios entre frases son del tramo anterior; el primero empieza en 0
    // si está cerca.
    if (!merged.empty() && merged.front()["start"].get<double>() < 1.0 && !py_truthy(get(merged.front(), "mode"))) {
        merged.front()["start"] = 0.0;
    }
    for (std::size_t i = 0; i + 1 < merged.size(); ++i) {
        Json& a = merged[i];
        const double gap = merged[i + 1]["start"].get<double>() - a["end"].get<double>();
        if (gap > 0 && gap <= 1.5 && !py_truthy(get(a, "composition_id"))) {
            a["end"] = merged[i + 1]["start"];
        }
    }
    auto doc = normalize_doc(env, Json{{"segments", Json(std::move(merged))}});
    if (!doc) {
        return doc;
    }
    return std::move((*doc)["segments"]);
}

Result<Json> status_summary(SceneEnv& env, const Project& project) {
    auto doc = load(env, project);
    if (!doc) {
        return doc;
    }
    const Json& segs = (*doc)["segments"];
    Json counts = Json::object();
    for (const std::string_view k : statuses()) {
        counts[std::string(k)] = 0;
    }
    Json rows = Json::array();
    Json pending = Json::array();
    std::int64_t no_visual = 0;
    for (const Json& s : segs) {
        const std::string status = s["status"].get<std::string>();
        counts[status] = counts.value(status, std::int64_t{0}) + 1;
        const bool nv = py_truthy(get(s, "no_visual"));
        no_visual += nv ? 1 : 0;
        rows.push_back(Json{{"id", s["id"]},
                            {"start", s["start"]},
                            {"end", s["end"]},
                            {"mode", s["mode"]},
                            {"status", s["status"]},
                            {"has_materials", py_truthy(s["materials"])},
                            {"has_components", py_truthy(get(s, "components"))},
                            {"complexity", get(s, "complexity")},
                            {"no_visual", nv},
                            {"composition_id", s["composition_id"]},
                            {"placed_clip_id", s["placed_clip_id"]}});
        // Un tramo marcado «sin visual nuevo» no queda pendiente: ya está decidido.
        if ((status == "empty" || status == "ready") && !nv) {
            pending.push_back(s["id"]);
        }
    }
    const auto total = static_cast<std::int64_t>(segs.size());
    counts["total"] = total;
    counts["no_visual"] = no_visual;
    return Json{{"project_id", project.id},
                {"duration", timeline_duration(project)},
                {"script_source", script_units(project).second},
                {"total", total},
                {"counts", std::move(counts)},
                {"pending", std::move(pending)},
                {"segments", std::move(rows)}};
}

// --- El material -------------------------------------------------------------------------------

namespace {

constexpr std::array<std::string_view, 2> kSemanticListKeys{"subjects", "actions"};
constexpr std::array<std::string_view, 5> kSemanticStrKeys{"visual_content", "environment", "mood", "composition",
                                                           "suggested_usage"};
constexpr std::array<std::string_view, 3> kVisualPriorities{"protagonista", "apoyo", "fondo"}; // VISUAL_PRIORITIES

// re.split(r"[,;]", v): los trozos, también los vacíos.
std::vector<Json> split_list(const std::string& v) {
    std::vector<Json> out;
    std::size_t from = 0;
    for (std::size_t i = 0; i <= v.size(); ++i) {
        if (i == v.size() || v[i] == ',' || v[i] == ';') {
            out.emplace_back(v.substr(from, i - from));
            from = i + 1;
        }
    }
    return out;
}

// `a or b` de Python: a si es verdadero; si no, b.
const Json& either(const Json& a, const Json& b) { return py_truthy(a) ? a : b; }

} // namespace

Json normalize_semantic(const Json& raw) {
    Json out = Json::object();
    if (!raw.is_object()) {
        return out;
    }
    for (const std::string_view k : kSemanticListKeys) {
        const Json& v = get(raw, k);
        std::vector<Json> src;
        if (v.is_string()) {
            src = split_list(v.get_ref<const std::string&>());
        } else if (v.is_array()) {
            src.assign(v.begin(), v.end());
        } else {
            continue;
        }
        Json items = Json::array();
        for (const Json& x : src) {
            if (py_truthy(x) && !py_strip(py_str(x)).empty() && items.size() < 8) {
                items.push_back(clean(x, 60));
            }
        }
        if (!items.empty()) {
            out[std::string(k)] = std::move(items);
        }
    }
    for (const std::string_view k : kSemanticStrKeys) {
        if (std::string v = clean(get(raw, k), 200); !v.empty()) {
            out[std::string(k)] = std::move(v);
        }
    }
    const Json& vp = get(raw, "visual_priority");
    if (vp.is_string() &&
        std::ranges::find(kVisualPriorities, vp.get_ref<const std::string&>()) != kVisualPriorities.end()) {
        out["visual_priority"] = vp;
    }
    return out;
}

Result<std::string> semantic_blob(const Json& semantic) {
    if (!semantic.is_object() || semantic.empty()) {
        return std::string();
    }
    std::vector<Json> parts;
    for (const std::string_view k : kSemanticListKeys) {
        // parts += sem.get(k) or []: un texto suma sus caracteres y un dict sus claves.
        auto items = iter_or_empty(get(semantic, k), "la metadata semántica");
        if (!items) {
            return std::unexpected(items.error());
        }
        parts.insert(parts.end(), items->begin(), items->end());
    }
    for (const std::string_view k : kSemanticStrKeys) {
        if (const Json& v = get(semantic, k); py_truthy(v)) {
            parts.push_back(v);
        }
    }
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (!parts[i].is_string()) {
            return crash("join() de un valor que no es texto en la metadata semántica");
        }
        if (i > 0) {
            out += ' ';
        }
        out += parts[i].get_ref<const std::string&>();
    }
    return out;
}

Result<Json> set_material_meta(ProjectsStore& store, LibraryFile& library, std::string_view pid, const Json& kind,
                               std::string_view ident, const Json& meta, std::string_view scope) {
    if (!(kind == "clips" || kind == "images")) {
        return fail(ErrorCode::InvalidArgument, "kind debe ser 'clips' o 'images'.");
    }
    Json semantic = normalize_semantic(meta);
    if (scope == "library") {
        if (auto r = library.set_semantic(ident, semantic); !r) {
            return std::unexpected(r.error()); // LookupError «Recurso no encontrado.»
        }
        return Json{{"kind", kind}, {"id", ident}, {"scope", "library"}, {"semantic", std::move(semantic)}};
    }
    auto item = store.update_material(pid, kind.get_ref<const std::string&>(), ident, Json{{"semantic", semantic}});
    if (!item) {
        return std::unexpected(item.error());
    }
    if (!*item) {
        return fail(ErrorCode::NotFound, "Material no encontrado.");
    }
    return Json{{"kind", kind}, {"id", ident}, {"scope", "project"}, {"semantic", get(**item, "semantic")}};
}

Result<Json> material_catalog(const Project& project, const LibraryFile& library) {
    Json out = Json::array();
    const auto sem = [](const std::optional<Json>& s) { return s && py_truthy(*s) ? *s : Json(); };
    const auto text_or = [](const std::optional<std::string>& a, const std::string& b) {
        return a && !a->empty() ? *a : b;
    };
    const std::string none;
    for (const ClipInfo& c : project.clips) {
        const double dur = c.in_point && c.out_point ? *c.out_point - *c.in_point : c.end - c.start;
        out.push_back(Json{{"kind", "clips"},
                           {"id", std::to_string(c.index)},
                           {"scope", "project"},
                           {"title", text_or(c.label, c.filename)},
                           {"description", text_or(c.description, text_or(c.description_ai, none))},
                           {"duration", r3(dur)},
                           {"url", c.url},
                           {"semantic", sem(c.semantic)}});
    }
    for (const ImageInfo& im : project.images) {
        out.push_back(Json{{"kind", "images"},
                           {"id", im.id},
                           {"scope", "project"},
                           {"title", text_or(im.label, im.filename)},
                           {"description", text_or(im.description, text_or(im.description_ai, none))},
                           {"duration", nullptr},
                           {"url", im.url},
                           {"semantic", sem(im.semantic)}});
    }
    auto listed = library.list(); // except Exception: lib = {}
    const Json lib = listed ? std::move(*listed) : Json::object();
    static const Json empty_text = "";
    for (const std::string_view kind : {std::string_view("clips"), std::string_view("images")}) {
        auto items = iter_or_empty(get(lib, kind), "la biblioteca");
        if (!items) {
            return std::unexpected(items.error());
        }
        for (const Json& c : *items) {
            if (!c.is_object() || !c.contains("id")) {
                return crash("material de la biblioteca sin id");
            }
            const Json& semantic = get(c, "semantic");
            out.push_back(Json{{"kind", kind},
                               {"id", py_str(c["id"])},
                               {"scope", "library"},
                               {"title", either(get(c, "label"), get(c, "filename"))},
                               {"description", either(get(c, "description"), empty_text)},
                               {"duration", kind == "clips" ? get(c, "duration") : Json()},
                               {"url", get(c, "url")},
                               {"semantic", py_truthy(semantic) ? semantic : Json()}});
        }
    }
    return out;
}

Result<Json> rank_materials(const Json& catalog, std::string_view text, std::size_t limit) {
    std::vector<std::string> terms;
    for (const std::string& t : key_terms(text, 10)) {
        terms.push_back(py_strip_accents(py_lower(t)));
    }
    struct Row {
        std::int64_t score;
        int bare; // 0 con descripción o metadata, 1 sin
        const Json* m;
    };
    std::vector<Row> rows;
    for (const Json& m : catalog) {
        auto blob_sem = semantic_blob(get(m, "semantic"));
        if (!blob_sem) {
            return std::unexpected(blob_sem.error());
        }
        const std::string blob = py_strip_accents(
            py_lower(py_str(get(m, "title")) + " " + py_str(get(m, "description")) + " " + *blob_sem));
        std::int64_t score = 0;
        for (const std::string& t : terms) {
            score += !t.empty() && blob.find(t) != std::string::npos ? 1 : 0;
        }
        rows.push_back({score, py_truthy(get(m, "description")) || py_truthy(get(m, "semantic")) ? 0 : 1, &m});
    }
    // rows.sort() de (-score, bare, i, …): i desempata, así que es un orden estable.
    std::ranges::stable_sort(
        rows, [](const Row& a, const Row& b) { return a.score != b.score ? a.score > b.score : a.bare < b.bare; });
    Json out = Json::array();
    for (std::size_t i = 0; i < rows.size() && i < limit; ++i) {
        Json m = *rows[i].m;
        m["match"] = rows[i].score;
        out.push_back(std::move(m));
    }
    return out;
}

// --- El paquete de contexto de un tramo (lo único que ve la IA) --------------------------------

namespace {

// BUDGET: los caracteres de cada parte del pack.
constexpr std::size_t kBudgetCurrent = 900;
constexpr std::size_t kBudgetSide = 220;
constexpr std::size_t kBudgetInstruction = 600;
constexpr std::size_t kBudgetMaterialDesc = 220;
constexpr std::size_t kBudgetNeighbor = 120;
constexpr std::size_t kBudgetCompositionIntent = 700;

// sorted(tl.clips, key=lambda c: float(c.start or 0.0)): estable.
std::vector<const TimelineClip*> clips_by_start(const Timeline& tl) {
    std::vector<const TimelineClip*> out;
    out.reserve(tl.clips.size());
    for (const auto& cow : tl.clips) {
        out.push_back(&*cow);
    }
    std::ranges::stable_sort(out, less_nan_last, [](const TimelineClip* c) { return c->start; });
    return out;
}

// _find_material: el del catálogo con el mismo kind, id y scope («project» si la referencia no lo trae).
const Json* find_material(const Json& catalog, const Json& ref) {
    static const Json project_scope = "project";
    const Json& scope = ref.is_object() && ref.contains("scope") ? ref["scope"] : project_scope;
    for (const Json& m : catalog) {
        if (get(m, "kind") == get(ref, "kind") && get(m, "id") == get(ref, "id") && get(m, "scope") == scope) {
            return &m;
        }
    }
    return nullptr;
}

// f"{m['kind']}:{m['id']}": la clave de material_usage.
std::string usage_key(const Json& m) { return py_str(get(m, "kind")) + ":" + py_str(get(m, "id")); }

// _annotate_usage: `used` (los rangos de la timeline) en cada material ya colocado.
Json annotate_usage(const Json& materials, const Json& usage) {
    Json out = Json::array();
    for (const Json& m : materials) {
        const std::string key = usage_key(m);
        if (const Json& used = get(usage, key); py_truthy(used)) {
            Json with = m;
            with["used"] = used;
            out.push_back(std::move(with));
        } else {
            out.push_back(m);
        }
    }
    return out;
}

// neighbor(s) de build_pack: lo que se ve de un tramo vecino o del de referencia.
Json neighbor(const Json* s) {
    if (s == nullptr) {
        return Json();
    }
    return Json{{"start", get(*s, "start")},
                {"end", get(*s, "end")},
                {"mode", get(*s, "mode")},
                {"text", clean(get(*s, "text"), kBudgetNeighbor)},
                {"instruction", clean(get(*s, "instruction"), kBudgetNeighbor)},
                {"composition_id", get(*s, "composition_id")}};
}

// enrich_component de build_pack: el componente con el título y la descripción de su material.
Json enrich_component(const Json& c, const Json& catalog) {
    Json out{{"source", get(c, "source")}, {"role", get(c, "role")}, {"note", get(c, "note")}};
    if (const Json& mat = get(c, "material"); py_truthy(mat)) {
        out["material"] = mat;
        if (const Json* m = find_material(catalog, mat)) {
            out["title"] = get(*m, "title");
            out["description"] = clean(get(*m, "description"), 90);
        }
    }
    if (const Json& rng = get(c, "source_range"); py_truthy(rng)) {
        out["source_range"] = rng;
    }
    return out;
}

// " ".join(textos) de una lista de textos.
std::string join(const std::vector<std::string>& parts, std::string_view sep) {
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        if (i > 0) {
            out += sep;
        }
        out += parts[i];
    }
    return out;
}

} // namespace

Json caption_lines(const Project& project, double start, double end) {
    Json out = Json::array();
    if (!project.timeline) {
        return out;
    }
    for (const TimelineClip* cp : clips_by_start(*project.timeline)) {
        const TimelineClip& c = *cp;
        if (!is_caption(c)) {
            continue;
        }
        const double c0 = or0(c.start);
        const double c1 = c0 + gc::core::clip_timeline_duration(c);
        if (py_min(c1, end) - py_max(c0, start) <= 0.05) {
            continue;
        }
        if (c.words.empty()) {
            out.push_back(Json{{"start", r3(py_max(c0, start) - start)},
                               {"end", r3(py_min(c1, end) - start)},
                               {"text", clean_text(c.text.value_or(""), 200)}});
            continue;
        }
        std::vector<WordT> inside;
        for (const Word& w : c.words) {
            const double mid = c0 + (w.start + w.end) / 2;
            if (start - 1e-3 <= mid && mid < end) {
                inside.push_back({w.text, c0 + w.start, c0 + w.end});
            }
        }
        if (inside.empty()) {
            continue;
        }
        std::vector<std::string> texts;
        for (const WordT& w : inside) {
            texts.push_back(w.text);
        }
        out.push_back(Json{{"start", r3(max0(inside.front().start - start))},
                           {"end", r3(py_min(end, inside.back().end) - start)},
                           {"text", clean_text(join(texts, " "), 200)}});
    }
    return out;
}

Json range_words(const Project& project, double start, double end) {
    if (!project.timeline) {
        return Json::array();
    }
    const Timeline& tl = *project.timeline;
    for (const bool captions : {true, false}) {
        const std::vector<WordT> words = captions ? caption_words(tl) : material_words(project, tl);
        Json inside = Json::array();
        for (const WordT& w : words) {
            const double mid = (w.start + w.end) / 2;
            if (start - 1e-3 <= mid && mid < end) {
                inside.push_back(Json{{"text", w.text},
                                      {"start", r3(max0(w.start - start))},
                                      {"end", r3(py_min(end, w.end) - start)}});
            }
        }
        if (!inside.empty()) {
            return inside;
        }
    }
    return Json::array();
}

Json material_usage(const Project& project) {
    Json out = Json::object();
    if (!project.timeline) {
        return out;
    }
    for (const TimelineClip* cp : clips_by_start(*project.timeline)) {
        const TimelineClip& c = *cp;
        std::string key;
        if (c.kind == "video" && c.asset_kind == "clips") {
            key = "clips:" + c.asset_id;
        } else if (c.kind == "image" && c.asset_kind == "images") {
            key = "images:" + c.asset_id;
        } else {
            continue;
        }
        const double c0 = or0(c.start);
        out[key].push_back(Json{{"start", r3(c0)}, {"end", r3(c0 + gc::core::clip_timeline_duration(c))}});
    }
    return out;
}

Json recent_materials(const Json& catalog, const Json& usage, double start) {
    std::unordered_map<std::string, const Json*> by_key; // con la misma clave, gana el último
    for (const Json& m : catalog) {
        by_key[usage_key(m)] = &m;
    }
    std::vector<Json> rows;
    for (const auto& [key, ranges] : usage.items()) {
        // max(r["end"] for r in ranges if r["end"] <= start + 1e-3), o None si no hay ninguno.
        std::optional<double> last;
        for (const Json& r : ranges) {
            const double e = r["end"].get<double>();
            if (e <= start + 1e-3 && (!last || e > *last)) {
                last = e;
            }
        }
        if (!last || start - *last > kRecentWindow) {
            continue;
        }
        const std::size_t colon = key.find(':'); // key.partition(":")
        const std::string kind = colon == std::string::npos ? key : key.substr(0, colon);
        const std::string ident = colon == std::string::npos ? std::string() : key.substr(colon + 1);
        Json title = ident; // (by_key.get(key) or {}).get("title") or ident
        if (const auto it = by_key.find(key); it != by_key.end()) {
            if (const Json& t = get(*it->second, "title"); py_truthy(t)) {
                title = t;
            }
        }
        rows.push_back(Json{{"kind", kind}, {"id", ident}, {"ago", r3(start - *last)}, {"title", std::move(title)}});
    }
    std::ranges::stable_sort(rows, less_nan_last, [](const Json& x) { return x["ago"].get<double>(); });
    Json out = Json::array();
    for (std::size_t i = 0; i < rows.size() && i < 5; ++i) {
        out.push_back(std::move(rows[i]));
    }
    return out;
}

Result<Json> build_pack(SceneEnv& env, const Project& project, const LibraryFile& library, const Json& segment,
                        const Json& doc) {
    const auto start_v = py_float(get(segment, "start"));
    const auto end_v = py_float(get(segment, "end"));
    if (!start_v || !end_v) {
        return crash("float() del inicio o del fin del tramo");
    }
    const double start = *start_v;
    const double end = *end_v;
    auto [units, source] = script_units(project);
    // text_in_range(...) or segment.get("text") or ""
    std::string current = text_in_range(units, start, end);
    if (current.empty()) {
        const Json& t = get(segment, "text");
        current = py_truthy(t) ? py_str(t) : std::string();
    }
    std::string before; // la última frase con el centro antes del tramo
    std::string after;  // la primera con el centro en el fin del tramo o después
    bool has_after = false;
    for (const Json& u : units) {
        const double mid = (u["start"].get<double>() + u["end"].get<double>()) / 2;
        if (mid < start) {
            before = u["text"].get<std::string>();
        }
        if (mid >= end && !has_after) {
            after = u["text"].get<std::string>();
            has_after = true;
        }
    }
    Json lines = caption_lines(project, start, end);
    Json words = range_words(project, start, end);
    auto catalog = material_catalog(project, library);
    if (!catalog) {
        return catalog;
    }
    auto refs = iter_or_empty(get(segment, "materials"), "los materiales del tramo");
    if (!refs) {
        return std::unexpected(refs.error());
    }
    Json chosen = Json::array();
    for (const Json& r : *refs) {
        if (const Json* m = find_material(*catalog, r)) {
            chosen.push_back(*m);
        }
    }
    const Json usage = material_usage(project);
    const Json occ = timeline_context(project, start, end);
    const Json& segs = get(doc, "segments");
    std::ptrdiff_t idx = -1;
    const Json* ref = nullptr;
    if (segs.is_array()) {
        for (std::size_t i = 0; i < segs.size(); ++i) {
            if (idx < 0 && get(segs[i], "id") == get(segment, "id")) {
                idx = static_cast<std::ptrdiff_t>(i);
            }
        }
        for (const Json& s : segs) {
            if (get(s, "id") == get(segment, "reference_id")) {
                ref = &s;
                break;
            }
        }
    }
    Json style = style_context(project, start, end);
    Json blueprint = load_blueprint(env, project);
    auto cx = complexity(get(segment, "complexity"));
    if (!cx) {
        return std::unexpected(cx.error());
    }
    const Json& mode = get(segment, "mode");
    Json direction{{"mode", py_truthy(mode) ? mode : Json("propose")},
                   {"instruction", clean(get(segment, "instruction"), kBudgetInstruction)},
                   {"strict", py_truthy(get(segment, "strict"))},
                   {"composition_intent", clean(get(segment, "composition_intent"), kBudgetCompositionIntent)},
                   {"complexity", std::move(*cx)},
                   {"no_visual", py_truthy(get(segment, "no_visual"))}};
    Json candidates = Json::array();
    if (chosen.empty()) {
        const Json& instruction = get(segment, "instruction");
        auto ranked =
            rank_materials(*catalog, current + " " + (py_truthy(instruction) ? py_str(instruction) : std::string()));
        if (!ranked) {
            return ranked;
        }
        candidates = annotate_usage(*ranked, usage);
    }
    auto comps = iter_or_empty(get(segment, "components"), "los componentes del tramo");
    if (!comps) {
        return std::unexpected(comps.error());
    }
    Json components = Json::array();
    for (const Json& c : *comps) {
        components.push_back(enrich_component(c, *catalog));
    }
    const std::size_t n = segs.is_array() ? segs.size() : 0;
    const Json* previous = idx > 0 ? &segs[static_cast<std::size_t>(idx - 1)] : nullptr;
    const Json* next = idx >= 0 && static_cast<std::size_t>(idx) + 1 < n ? &segs[static_cast<std::size_t>(idx + 1)]
                                                                         : nullptr;
    const bool has_captions = !lines.empty();
    return Json{{"segment_id", get(segment, "id")},
                {"blueprint", std::move(blueprint)},
                {"range", Json{{"start", r3(start)}, {"end", r3(end)}, {"duration", r3(end - start)}}},
                {"script", Json{{"source", source},
                                {"has_captions", has_captions},
                                {"current", clean_text(current, kBudgetCurrent)},
                                {"before", clean_text(before, kBudgetSide)},
                                {"after", clean_text(after, kBudgetSide)}}},
                {"captions", std::move(lines)},
                {"words", std::move(words)},
                {"direction", std::move(direction)},
                {"materials", annotate_usage(chosen, usage)},
                {"candidates", std::move(candidates)},
                {"reference", neighbor(ref)},
                {"previous", neighbor(previous)},
                {"next", neighbor(next)},
                {"timeline", Json{{"existing", occ["existingElements"]}, {"motion_in_range", occ["motionInRange"]}}},
                {"recent_materials", recent_materials(*catalog, usage, start)},
                {"components", std::move(components)},
                {"style", std::move(style)}};
}

namespace {

// f"{x:.Nf}" de un número del pack.
std::string fx(const Json& v, int precision) { return py_fixed(v.is_number() ? v.get<double>() : 0.0, precision); }

// MODES.get(mode, MODES["propose"]).
const ModeInfo& mode_info(const Json& mode) {
    const std::vector<ModeInfo>& all = modes();
    const std::string_view key = mode.is_string() ? std::string_view(mode.get_ref<const std::string&>()) : "propose";
    for (const ModeInfo& m : all) {
        if (m.key == key) {
            return m;
        }
    }
    return all.front(); // «propose»
}

// _SOURCE_TXT.get(source, source).
std::string source_text(const std::string& source) {
    if (source == "captions") {
        return "subtítulos";
    }
    if (source == "transcript") {
        return "transcripción del material";
    }
    if (source == "audio_text_estimate") {
        return "texto del audio (tiempos estimados)";
    }
    if (source == "none") {
        return "sin guion";
    }
    return source;
}

// COMPLEXITY_LABELS.get(lvl, "").
std::string_view complexity_label(const Json& lvl) {
    static constexpr std::array<std::string_view, 5> kLabels{"simple", "simple + apoyo", "composición",
                                                             "explicación compleja", "clímax visual"};
    const auto n = lvl.is_number() || lvl.is_boolean() ? py_float(lvl) : std::nullopt;
    if (!n || std::trunc(*n) != *n || *n < 1.0 || *n > 5.0) {
        return "";
    }
    return kLabels[static_cast<std::size_t>(*n) - 1];
}

// directions.get(key).get("label") or key: el nombre de la dirección creativa (o el de la de por defecto).
std::string direction_label(const Json& key) {
    const std::string k = py_strip(key.is_string() ? key.get<std::string>() : std::string());
    const DirectionInfo* found = nullptr;
    const DirectionInfo* fallback = nullptr;
    for (const DirectionInfo& d : directions()) {
        if (d.key == k) {
            found = &d;
        }
        if (d.key == kDefaultDirection) {
            fallback = &d;
        }
    }
    const DirectionInfo* d = found != nullptr ? found : fallback;
    return d != nullptr && !d->label.empty() ? std::string(d->label) : py_str(key);
}

// " · ".join(lista) de una lista de textos de la dirección global.
std::string join_texts(const Json& list, std::string_view sep) {
    std::vector<std::string> parts;
    if (list.is_array()) {
        for (const Json& x : list) {
            parts.push_back(py_str(x));
        }
    }
    return join(parts, sep);
}

// _blueprint_lines: la dirección GLOBAL del proyecto, en un solo bloque.
std::vector<std::string> blueprint_lines(const Json& bp) {
    if (!bp.is_object() || bp.empty()) {
        return {};
    }
    std::string rows = "DIRECCIÓN GLOBAL (vale para todo el vídeo; manténla coherente en este tramo):";
    if (const Json& d = get(bp, "direction"); py_truthy(d)) {
        rows += "\n  dirección creativa: " + direction_label(d);
    }
    if (const Json& v = get(bp, "identity"); py_truthy(v)) {
        rows += "\n  identidad: " + py_str(v);
    }
    if (const Json& v = get(bp, "vocabulary"); py_truthy(v)) {
        rows += "\n  vocabulario permitido: " + join_texts(v, " · ");
    }
    if (const Json& v = get(bp, "rules"); py_truthy(v)) {
        rows += "\n  reglas: " + join_texts(v, " · ");
    }
    if (const Json& v = get(bp, "intensity_curve"); py_truthy(v)) {
        rows += "\n  curva de intensidad del vídeo: " + py_str(v);
    }
    return {rows};
}

// _safe_area_lines: el lienzo y la zona de los subtítulos, para que la IA no tape lo esencial.
std::vector<std::string> safe_area_lines(const Json& style) {
    if (!style.is_object()) {
        return {};
    }
    std::vector<std::string> out;
    const Json& fmt = get(style, "format");
    const Json& w = get(fmt, "width");
    const Json& h = get(fmt, "height");
    if (py_truthy(w) && py_truthy(h)) {
        out.push_back("LIENZO: " + py_str(w) + "x" + py_str(h) +
                      " (vertical 9:16; coordenadas en fracción 0–1, 0=arriba/izq)");
    }
    if (const Json& avoid = get(style, "avoidY"); avoid.is_array() && avoid.size() == 2) {
        out.push_back("ZONA DE SUBTÍTULOS (déjala libre de texto/gráficos/caras/sujeto principal/stickman): y " +
                      fx(avoid[0], 2) + "–" + fx(avoid[1], 2) +
                      ". Compón el contenido esencial FUERA de esa banda; si algo importante cae dentro, "
                      "reubícalo, redúcelo o recórtalo.");
    }
    return out;
}

// _occupancy_lines: lo que ya ocupa la timeline en el tramo (los 6 primeros).
std::string occupancy_line(const Json& pack) {
    const Json& existing = get(get(pack, "timeline"), "existing");
    if (!py_truthy(existing)) {
        return "YA EN LA TIMELINE EN ESTE TRAMO: nada (lienzo libre)";
    }
    std::string out = "YA EN LA TIMELINE EN ESTE TRAMO (compón encima o deja hueco; no lo dupliques):";
    for (std::size_t i = 0; i < existing.size() && i < 6; ++i) {
        const Json& e = existing[i];
        const Json& name = get(e, "name");
        out += "\n  · " + py_str(get(e, "kind")) + " «" + (py_truthy(name) ? py_str(name) : std::string()) + "» " +
               fx(get(e, "start"), 1) + "–" + fx(get(e, "end"), 1) + "s";
        if (const Json& d = get(e, "description"); py_truthy(d)) {
            out += " · " + py_str(d);
        }
    }
    return out;
}

// _used_note: si el material ya está colocado en la timeline (los 3 primeros rangos).
std::string used_note(const Json& m) {
    const Json& u = get(m, "used");
    if (!py_truthy(u) || !u.is_array()) {
        return {};
    }
    std::vector<std::string> spans;
    for (std::size_t i = 0; i < u.size() && i < 3; ++i) {
        spans.push_back(fx(get(u[i], "start"), 1) + "–" + fx(get(u[i], "end"), 1) + "s");
    }
    return "  [YA USADO en " + join(spans, ", ") + "]";
}

// El valor de un número o un bool para sumar como Python (bool es un int).
double as_double(const Json& v) { return v.is_boolean() ? (v.get<bool>() ? 1.0 : 0.0) : v.get<double>(); }
std::int64_t as_int(const Json& v) { return v.is_boolean() ? (v.get<bool>() ? 1 : 0) : v.get<std::int64_t>(); }

// tags = (sem.get("subjects") or []) + (sem.get("actions") or []) y, si hay, ", ".join(tags[:4]). Dos
// listas se juntan y dos textos se concatenan (tags[:4] son sus 4 primeros caracteres); dos números se
// suman (si dan 0 no hay etiquetas, y si no, tags[:4] es un TypeError); lo demás no se puede sumar.
Result<std::optional<std::string>> semantic_tags(const Json& subjects, const Json& actions) {
    static const Json empty = Json::array();
    const Json& a = py_truthy(subjects) ? subjects : empty;
    const Json& b = py_truthy(actions) ? actions : empty;
    if (a.is_array() && b.is_array()) {
        std::vector<const Json*> tags;
        for (const Json* list : {&a, &b}) {
            for (const Json& x : *list) {
                tags.push_back(&x);
            }
        }
        if (tags.empty()) {
            return std::optional<std::string>();
        }
        std::vector<std::string> parts;
        for (std::size_t i = 0; i < tags.size() && i < 4; ++i) {
            if (!tags[i]->is_string()) {
                return crash("join() de una etiqueta semántica que no es texto");
            }
            parts.push_back(tags[i]->get<std::string>());
        }
        return std::optional<std::string>(join(parts, ", "));
    }
    if (a.is_string() && b.is_string()) {
        const std::string text = a.get<std::string>() + b.get<std::string>();
        std::vector<std::string> parts;
        for (std::size_t i = 0; i < text.size() && parts.size() < 4;) {
            const std::size_t from = i;
            static_cast<void>(utf8_next(text, i));
            parts.push_back(text.substr(from, i - from));
        }
        return std::optional<std::string>(join(parts, ", "));
    }
    const auto numeric = [](const Json& v) { return v.is_number() || v.is_boolean(); };
    if (numeric(a) && numeric(b)) {
        bool zero = false;
        if (a.is_number_float() || b.is_number_float()) {
            zero = as_double(a) + as_double(b) == 0.0;
        } else {
            const std::int64_t x = as_int(a);
            const std::int64_t y = as_int(b);
            zero = y != std::numeric_limits<std::int64_t>::min() && x == -y;
        }
        if (zero) {
            return std::optional<std::string>();
        }
        return crash("tags[:4] de un número en la metadata semántica");
    }
    return crash("no se pueden sumar los subjects y las actions de la metadata semántica");
}

// _sem_note: la pista semántica compacta de un material (uso sugerido, prioridad y sujetos/acciones).
Result<std::string> sem_note(const Json& m) {
    const Json& sem = get(m, "semantic");
    if (!sem.is_object() || sem.empty()) {
        return std::string();
    }
    std::vector<Json> bits;
    if (const Json& u = get(sem, "suggested_usage"); py_truthy(u)) {
        bits.emplace_back("uso: " + py_str(u));
    }
    if (const Json& vp = get(sem, "visual_priority"); py_truthy(vp)) {
        bits.push_back(vp);
    }
    auto tags = semantic_tags(get(sem, "subjects"), get(sem, "actions"));
    if (!tags) {
        return std::unexpected(tags.error());
    }
    if (*tags) {
        bits.emplace_back(**tags);
    }
    if (bits.empty()) {
        return std::string();
    }
    std::vector<std::string> parts;
    for (const Json& b : bits) {
        if (!b.is_string()) {
            return crash("join() de una prioridad visual que no es texto");
        }
        parts.push_back(b.get<std::string>());
    }
    return "  (" + join(parts, " · ") + ")";
}

// _mat_kind.
std::string_view mat_kind(const Json& m) { return get(m, "kind") == "clips" ? "vídeo" : "imagen"; }

// Una línea de MATERIAL ELEGIDO o de MATERIALES QUE PODRÍAN SERVIR.
Result<std::string> material_line(const Json& m) {
    std::string desc = clean(get(m, "description"), kBudgetMaterialDesc);
    if (desc.empty()) {
        desc = "(sin descripción)";
    }
    auto sem = sem_note(m);
    if (!sem) {
        return sem;
    }
    return "  · [" + std::string(mat_kind(m)) + " " + py_str(get(m, "id")) + "] " + py_str(get(m, "title")) + ": " +
           desc + *sem + used_note(m);
}

// _component_lines: la composición híbrida planificada del tramo.
std::vector<std::string> component_lines(const Json& components) {
    if (!py_truthy(components) || !components.is_array()) {
        return {};
    }
    std::vector<std::string> rows;
    for (const Json& c : components) {
        std::string what;
        if (get(c, "source") == "material") {
            const Json& title = get(c, "title");
            what = "material [" + py_str(py_truthy(title) ? title : get(get(c, "material"), "id")) + "]";
            if (const Json& rng = get(c, "source_range"); py_truthy(rng)) {
                what += " (origen " + fx(get(rng, "in"), 1) + "–" + fx(get(rng, "out"), 1) + "s)";
            }
        } else {
            what = py_str(get(c, "source"));
        }
        const Json& role = get(c, "role");
        const Json& note = get(c, "note");
        rows.push_back("  · " + what + (py_truthy(role) ? " · rol: " + py_str(role) : std::string()) +
                       (py_truthy(note) ? " — " + py_str(note) : std::string()));
    }
    return {"COMPOSICIÓN PLANIFICADA (combina lo que haga falta; cada componente con su rol):\n" + join(rows, "\n")};
}

} // namespace

Result<std::string> pack_text(const Json& pack) {
    const Json& r = get(pack, "range");
    const Json& s = get(pack, "script");
    const Json& d = get(pack, "direction");
    const ModeInfo& mode = mode_info(get(d, "mode"));
    std::vector<std::string> lines{"TRAMO: " + fx(get(r, "start"), 2) + "s – " + fx(get(r, "end"), 2) + "s (dura " +
                                   fx(get(r, "duration"), 2) + "s)"};
    for (std::string& l : blueprint_lines(get(pack, "blueprint"))) {
        lines.push_back(std::move(l));
    }
    for (std::string& l : safe_area_lines(get(pack, "style"))) {
        lines.push_back(std::move(l));
    }
    if (const Json& current = get(s, "current"); py_truthy(current)) {
        lines.push_back("LO QUE DICE LA VOZ (" + source_text(py_str(get(s, "source"))) + "):\n«" + py_str(current) +
                        "»");
    } else {
        lines.emplace_back("LO QUE DICE LA VOZ: (nadie habla en este tramo; guíate por la dirección)");
    }
    if (const Json& caps = get(pack, "captions"); py_truthy(caps) && caps.is_array()) {
        std::string block = "SUBTÍTULOS DEL TRAMO (segundos desde el inicio del tramo):";
        for (const Json& c : caps) {
            block += "\n  " + fx(get(c, "start"), 1) + "–" + fx(get(c, "end"), 1) + "  " + py_str(get(c, "text"));
        }
        lines.push_back(std::move(block));
    } else {
        lines.emplace_back("SUBTÍTULOS DEL TRAMO: ninguno");
    }
    if (const Json& before = get(s, "before"); py_truthy(before)) {
        lines.push_back("Frase anterior: «" + py_str(before) + "»");
    }
    if (const Json& after = get(s, "after"); py_truthy(after)) {
        lines.push_back("Frase siguiente: «" + py_str(after) + "»");
    }
    lines.push_back(occupancy_line(pack));
    lines.push_back("DIRECCIÓN: " + std::string(mode.label) + " → " + std::string(mode.ai));
    if (const Json& v = get(d, "instruction"); py_truthy(v)) {
        lines.push_back("INSTRUCCIÓN DEL DIRECTOR: " + py_str(v));
    }
    if (const Json& v = get(d, "composition_intent"); py_truthy(v)) {
        lines.push_back("INTENCIÓN DE COMPOSICIÓN: " + py_str(v));
    }
    if (const Json& lvl = get(d, "complexity"); py_truthy(lvl)) {
        lines.push_back("COMPLEJIDAD OBJETIVO: " + py_str(lvl) + "/5 (" + std::string(complexity_label(lvl)) +
                        "); no te pases de este presupuesto visual.");
    }
    if (py_truthy(get(d, "no_visual"))) {
        lines.emplace_back("SIN VISUAL NUEVO: este tramo no necesita nada extra; basta mantener el plano "
                           "(pequeño zoom/transición) y los subtítulos. NO añadas elementos.");
    }
    for (std::string& l : component_lines(get(pack, "components"))) {
        lines.push_back(std::move(l));
    }
    if (py_truthy(get(d, "strict"))) {
        lines.emplace_back(
            "SEGUIR EL GUION ESTRICTAMENTE: lo que se ve acompaña cada frase en orden; no añadas ideas nuevas.");
    }
    if (const Json& ref = get(pack, "reference"); py_truthy(ref)) {
        const Json& instruction = get(ref, "instruction");
        lines.push_back("TRAMO DE REFERENCIA (" + fx(get(ref, "start"), 1) + "–" + fx(get(ref, "end"), 1) + "s): «" +
                        py_str(get(ref, "text")) + "»" +
                        (py_truthy(instruction) ? " · dirección: " + py_str(instruction) : std::string()));
    }
    const Json& materials = get(pack, "materials");
    const Json& candidates = get(pack, "candidates");
    const bool chosen = py_truthy(materials);
    if (chosen || py_truthy(candidates)) {
        std::string block = chosen ? "MATERIAL ELEGIDO (los vídeos van como clip real en la timeline; las imágenes "
                                     "pueden ser beats):"
                                   : "MATERIALES QUE PODRÍAN SERVIR (opcional):";
        for (const Json& m : chosen ? materials : candidates) {
            auto line = material_line(m);
            if (!line) {
                return line;
            }
            block += "\n" + *line;
        }
        lines.push_back(std::move(block));
    }
    if (const Json& recent = get(pack, "recent_materials"); py_truthy(recent) && recent.is_array()) {
        std::vector<std::string> parts;
        for (std::size_t i = 0; i < recent.size() && i < 4; ++i) {
            parts.push_back(py_str(get(recent[i], "title")) + " (hace " + fx(get(recent[i], "ago"), 1) + "s)");
        }
        lines.push_back("USADO HACE POCO (evita repetir el mismo plano seguido; varía crop/zoom/duración o usa otro): " +
                        join(parts, "; "));
    }
    for (const auto& [key, label] : {std::pair<const char*, const char*>{"previous", "Tramo anterior"},
                                     std::pair<const char*, const char*>{"next", "Tramo siguiente"}}) {
        const Json& n = get(pack, key);
        const Json& instruction = get(n, "instruction");
        if (py_truthy(n) && (py_truthy(get(n, "mode")) || py_truthy(instruction))) {
            lines.push_back(std::string(label) + ": " + std::string(mode_info(get(n, "mode")).label) +
                            (py_truthy(instruction) ? " — " + py_str(instruction) : std::string()));
        }
    }
    return join(lines, "\n");
}

std::int64_t estimate_tokens(std::string_view text) {
    // round() de Python: al par en los empates, como nearbyint con el redondeo por defecto.
    const auto n = static_cast<std::int64_t>(std::nearbyint(static_cast<double>(py_len(text)) / 3.6));
    return std::max<std::int64_t>(1, n);
}

namespace {

// _INTENT_BY_MODE.get(mode, "explicativa").
std::string_view intent_of(const Json& mode) {
    if (mode == "represent") {
        return "grafica";
    }
    if (mode == "reinforce") {
        return "narrativa";
    }
    if (mode == "material") {
        return "cinematografica";
    }
    return "explicativa";
}

// _material_name: el título con la descripción (si la descripción no está ya en el título). Puede no
// ser un texto: un título que no es texto sin descripción sale tal cual. Con descripción, un título
// verdadero que no es texto no tiene lower(): un crash.
Result<Json> material_name(const Json& m) {
    const std::string desc = clean(get(m, "description"), 90);
    const Json& title = get(m, "title");
    if (!desc.empty()) {
        if (py_truthy(title) && !title.is_string()) {
            return crash("lower() de un título que no es texto");
        }
        const std::string low_title = py_truthy(title) ? py_lower(title.get_ref<const std::string&>()) : std::string();
        if (low_title.find(py_lower(desc)) == std::string::npos) {
            return Json(py_str(title) + " — " + desc);
        }
    }
    return py_truthy(title) ? title : Json(desc);
}

} // namespace

Result<Json> brief_defaults(const Json& pack) {
    const Json& d = get(pack, "direction");
    std::vector<const Json*> images;
    std::vector<const Json*> videos;
    if (const Json& materials = get(pack, "materials"); materials.is_array()) {
        for (const Json& m : materials) {
            if (get(m, "kind") == "images") {
                images.push_back(&m);
            } else if (get(m, "kind") == "clips") {
                videos.push_back(&m);
            }
        }
    }
    const bool strict = py_truthy(get(d, "strict"));
    std::vector<std::string> notes;
    if (strict) {
        notes.emplace_back("Sigue el guion estrictamente, frase a frase.");
    }
    if (!videos.empty()) {
        // Un clip de vídeo no es un beat de la escena: se coloca aparte en la timeline.
        std::vector<std::string> names;
        for (const Json* m : videos) {
            auto name = material_name(*m);
            if (!name) {
                return name;
            }
            if (!name->is_string()) {
                return crash("join() de un nombre de material que no es texto");
            }
            names.push_back(name->get<std::string>());
        }
        notes.push_back("Vídeo elegido para este tramo (irá como clip real en la timeline; la escena lo acompaña): " +
                        join(names, "; ") + ".");
    }
    const Json& current = get(get(pack, "script"), "current");
    const std::string script = current.is_string() ? current.get<std::string>() : std::string();
    const Json& instruction = get(d, "instruction");
    Json must = Json::array();
    for (const Json* m : images) {
        auto name = material_name(*m);
        if (!name) {
            return name;
        }
        if (must.size() < 6) {
            must.push_back(std::move(*name));
        }
    }
    return Json{{"idea", py_truthy(instruction) ? instruction : Json(py_slice(script, 200))},
                {"script", script},
                {"intent", intent_of(get(d, "mode"))},
                {"must_include", std::move(must)},
                {"resources", images.empty() ? Json::object() : Json{{"image", "required"}}},
                {"structure", strict || py_truthy(get(pack, "captions")) ? "script" : "free"},
                {"notes", join(notes, " ")}};
}

Result<Json> skeleton_beats(const Json& pack, double pace) {
    const auto dur_v = py_float(get(get(pack, "range"), "duration")); // float(pack["range"]["duration"])
    if (!dur_v) {
        return crash("float() de la duración del tramo");
    }
    const double dur = *dur_v;
    static const Json empty = Json::array();
    const Json& words = get(pack, "words");
    const Json& captions = get(pack, "captions");
    const Json& items = py_truthy(words) ? words : (py_truthy(captions) ? captions : empty);
    Json out = Json::array();
    if (items.empty()) {
        // n = max(1, min(6, round(dur / max(0.8, pace)))): round() de un NaN es un ValueError y el de un
        // infinito, un OverflowError.
        const double q = dur / py_max(0.8, pace);
        if (std::isnan(q)) {
            return fail(ErrorCode::InvalidArgument, "cannot convert float NaN to integer");
        }
        if (std::isinf(q)) {
            return crash("cannot convert float infinity to integer");
        }
        const auto n = static_cast<std::int64_t>(std::clamp(std::nearbyint(q), 1.0, 6.0));
        const double step = dur / static_cast<double>(n);
        for (std::int64_t i = 0; i < n; ++i) {
            out.push_back(Json{{"text", ""},
                               {"start", r3(static_cast<double>(i) * step)},
                               {"end", r3(i + 1 == n ? dur : static_cast<double>(i + 1) * step)}});
        }
        return out;
    }
    std::vector<std::vector<const Json*>> groups;
    std::vector<const Json*> cur;
    for (std::size_t i = 0; i < items.size(); ++i) {
        const Json& it = items[i];
        cur.push_back(&it);
        const double span = get(*cur.back(), "end").get<double>() - get(*cur.front(), "start").get<double>();
        const Json& text = get(it, "text");
        if (!text.is_string()) {
            return crash("strip() de un texto que no es str en skeleton_beats");
        }
        const std::string txt = py_strip(text.get_ref<const std::string&>());
        if (i + 1 == items.size()) {
            break;
        }
        if ((sentence_end(txt) && span >= 0.8) || (txt.ends_with(',') && span >= pace * 0.8) || span >= pace * 1.4) {
            groups.push_back(std::move(cur));
            cur.clear();
        }
    }
    if (!cur.empty()) {
        groups.push_back(std::move(cur));
    }
    struct Beat {
        std::string text;
        double start;
        double end;
    };
    std::vector<Beat> beats;
    for (std::size_t i = 0; i < groups.size(); ++i) {
        const auto& g = groups[i];
        const double start = i == 0 ? 0.0 : get(*g.front(), "start").get<double>();
        const double end = i + 1 == groups.size() ? dur : get(*groups[i + 1].front(), "start").get<double>();
        std::vector<std::string> texts;
        for (const Json* x : g) {
            texts.push_back((*x)["text"].get<std::string>());
        }
        beats.push_back({join(texts, " "), r3(start), r3(end)});
    }
    // Ningún beat por debajo de 0,8 s: se funde con el anterior.
    std::vector<Beat> merged;
    for (Beat& bt : beats) {
        if (!merged.empty() && (bt.end - bt.start < 0.8 || merged.back().end - merged.back().start < 0.8)) {
            merged.back().end = bt.end;
            merged.back().text = py_strip(merged.back().text + " " + bt.text);
        } else {
            merged.push_back(std::move(bt));
        }
    }
    for (Beat& b : merged) {
        out.push_back(Json{{"text", std::move(b.text)}, {"start", b.start}, {"end", b.end}});
    }
    return out;
}

} // namespace gc::core::scene
