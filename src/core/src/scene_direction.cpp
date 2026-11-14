#include "gc/core/scene_direction.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "gc/core/segment_context.hpp"
#include "gc/core/timeline_rules.hpp"
#include "json_reader.hpp"
#include "py.hpp"

namespace gc::core::scene {

namespace {

using detail::py_float;
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

constexpr std::string_view kEllipsis = "\xE2\x80\xA6"; // «…»

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

// _r: round(float(x), 3).
double r3(double x) { return py_round(x, 3); }

// max(0.0, x) de Python: 0.0 salvo que x sea mayor (un NaN da 0.0).
double max0(double x) { return x > 0.0 ? x : 0.0; }

// _clean de un texto ya convertido: los espacios juntados en uno y, si pasa de `limit`
// caracteres, cortado con «…».
std::string clean_text(std::string_view text, std::size_t limit) {
    std::string t;
    for (const std::string& part : py_split(text)) {
        if (!t.empty()) {
            t += ' ';
        }
        t += part;
    }
    if (py_len(t) <= limit) {
        return t;
    }
    return py_rstrip(py_slice(t, limit - 1)) + std::string(kEllipsis);
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

// a < b para ordenar con std::stable_sort: con un NaN, `<` deja de ser un orden débil estricto (y en C++
// eso es indefinido). Los NaN van al final, como iguales entre sí. Divergencia: el sort de Python con un
// NaN no falla, pero el orden que da depende de las comparaciones que haga timsort.
bool less_nan_last(double a, double b) {
    if (std::isnan(a)) {
        return false;
    }
    return std::isnan(b) || a < b;
}

struct WordT {
    std::string text;
    double start = 0.0;
    double end = 0.0;
};

bool is_caption(const TimelineClip& c) {
    if (c.kind != "text") {
        return false;
    }
    if (c.text_role == "caption" || c.text_role == "free") {
        return *c.text_role == "caption";
    }
    return (c.origin && py_truthy(*c.origin)) || !c.words.empty();
}

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

} // namespace gc::core::scene
