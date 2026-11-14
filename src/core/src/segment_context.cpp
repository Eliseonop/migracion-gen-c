#include "gc/core/segment_context.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>

#include "gc/core/timeline_rules.hpp"
#include "json_reader.hpp"
#include "py.hpp"
#include "scene_util.hpp"

namespace gc::core {

namespace {

using detail::clean_text;
using detail::less_nan_last;
using detail::max0;
using detail::or0;
using detail::py_isdigit;
using detail::py_max;
using detail::py_min;
using detail::py_truthy;
using detail::r3;
using detail::py_isupper;
using detail::py_len;
using detail::py_lower;
using detail::py_strip_accents;
using detail::py_word_char;
using detail::utf8_next;

// _STOPWORDS.
constexpr std::array<std::string_view, 72> kStopwords{
    // es
    "para", "porque", "cuando", "donde", "desde", "hasta", "sobre", "entre", "durante", "tiene", "tienen", "puede",
    "pueden", "estos", "estas", "aquel", "aquella", "otras", "otros", "mucho", "muchos", "muchas", "siempre", "nunca",
    "también", "tambien", "ahora", "después", "despues", "antes", "quedó", "quedo", "haber", "haberlo", "forma",
    "manera", "cosas", "hacer", "había", "habia", "sería", "seria", "mismo", "misma", "todos", "todas", "nuestra",
    "nuestro", "vuelve", "acaba", "afirma", "pero", "este", "esta", "esto", "cada", "solo", "sólo", "nada", "algo",
    // en
    "about", "their", "there", "which", "would", "could", "should", "these", "those", "because", "through",
    "between"};

bool stopword(std::string_view w) { return std::ranges::find(kStopwords, w) != kStopwords.end(); }

// str.strip("-").
std::string_view strip_dashes(std::string_view s) {
    while (!s.empty() && s.front() == '-') {
        s.remove_prefix(1);
    }
    while (!s.empty() && s.back() == '-') {
        s.remove_suffix(1);
    }
    return s;
}

// re.findall(r"[\wÁÉÍÓÚÜÑáéíóúüñ][\wÁÉÍÓÚÜÑáéíóúüñ\-]*", text): las letras con tilde ya son \w.
std::vector<std::string_view> tokens(std::string_view text) {
    std::vector<std::string_view> out;
    for (std::size_t i = 0; i < text.size();) {
        const std::size_t start = i;
        if (!py_word_char(utf8_next(text, i))) {
            continue;
        }
        std::size_t end = i;
        while (end < text.size()) {
            std::size_t j = end;
            const char32_t c = utf8_next(text, j);
            if (!py_word_char(c) && c != U'-') {
                break;
            }
            end = j;
        }
        out.push_back(text.substr(start, end - start));
        i = end;
    }
    return out;
}

bool has_digit(std::string_view tok) {
    for (std::size_t i = 0; i < tok.size();) {
        if (py_isdigit(utf8_next(tok, i))) {
            return true;
        }
    }
    return false;
}

} // namespace

std::vector<std::string> key_terms(std::string_view text, std::size_t limit) {
    std::vector<std::string> out;
    std::unordered_set<std::string> seen;
    const std::vector<std::string_view> toks = tokens(text);
    for (std::size_t i = 0; i < toks.size(); ++i) {
        const std::string_view tok = toks[i];
        const std::string lowered = py_lower(tok);
        const std::string_view low = strip_dashes(lowered);
        std::string norm = py_strip_accents(low);
        if (seen.contains(norm) || stopword(low) || stopword(norm)) {
            continue;
        }
        // Mayúscula a mitad de frase (OpenAI, Navier-Stokes); ≥4 letras evita «Una», «Pero»…
        std::size_t at = 0;
        const bool proper = py_isupper(utf8_next(tok, at)) && i > 0 && py_len(tok) >= 4;
        if (py_len(low) >= 6 || proper || has_digit(tok)) {
            seen.insert(std::move(norm));
            out.emplace_back(strip_dashes(tok));
        }
        if (out.size() >= limit) {
            break;
        }
    }
    return out;
}

// --- La timeline y el estilo de un tramo ---------------------------------------------------------

bool is_caption_clip(const TimelineClip& c) {
    if (c.kind != "text") {
        return false;
    }
    if (c.text_role == "caption" || c.text_role == "free") {
        return *c.text_role == "caption";
    }
    return (c.origin && py_truthy(*c.origin)) || !c.words.empty();
}

namespace {

// VISUAL_KINDS.
constexpr std::array<std::string_view, 4> kVisualKinds{"video", "image", "shape", "motion"};

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

// _clip_end: float(c.start or 0.0) + clip_timeline_duration(c).
double end_of(const TimelineClip& c) { return or0(c.start) + clip_timeline_duration(c); }

// _overlap: max(0.0, min(a1, b1) - max(a0, b0)).
double overlap(double a0, double a1, double b0, double b1) { return max0(py_min(a1, b1) - py_max(a0, b0)); }

// `a or b or c or ""` con tres textos opcionales.
std::string first_text(const std::optional<std::string>& a, const std::optional<std::string>& b,
                       const std::optional<std::string>& c) {
    for (const std::optional<std::string>* v : {&a, &b, &c}) {
        if (*v && !(*v)->empty()) {
            return **v;
        }
    }
    return {};
}

// _material_desc: la descripción del vídeo o la imagen del clip (la tuya, la de la IA o el título); si no
// lo encuentra, o el clip es de otra cosa, la del propio clip.
std::string material_desc(const Project& proj, const TimelineClip& c) {
    if (c.kind == "video") {
        for (const ClipInfo& m : proj.clips) {
            // str(getattr(m, "index", …)) == key or str(getattr(m, "id", "")) == key: un id que falta es «None».
            if (std::to_string(m.index) == c.asset_id || m.id.value_or("None") == c.asset_id) {
                return clean_text(first_text(m.description, m.description_ai, m.label), 120);
            }
        }
    } else if (c.kind == "image") {
        for (const ImageInfo& m : proj.images) { // una imagen no tiene `index`: getattr da su id
            if (m.id == c.asset_id) {
                return clean_text(first_text(m.description, m.description_ai, m.label), 120);
            }
        }
    }
    return clean_text(c.description.value_or(""), 120);
}

using TrackNames = std::unordered_map<std::string, std::string>;

// _element: lo que ve la IA de un clip de la timeline.
Json element(const Project& proj, const TimelineClip& c, const TrackNames& names) {
    const auto track = names.find(c.track_id);
    std::string name =
        clean_text(c.kind == "text" ? c.text.value_or("") : (c.name && !c.name->empty() ? *c.name : c.filename), 60);
    Json el{{"id", c.id},
            {"kind", c.kind},
            {"track", track == names.end() ? c.track_id : track->second},
            {"name", name},
            {"start", r3(or0(c.start))},
            {"end", r3(end_of(c))}};
    if (c.kind == "motion" && c.composition_id && !c.composition_id->empty()) {
        el["composition_id"] = *c.composition_id;
    }
    if (std::string desc = material_desc(proj, c); !desc.empty() && desc != name) {
        el["description"] = std::move(desc);
    }
    // La nota dice qué SIGNIFICA el fragmento en la historia; la descripción solo qué es el archivo.
    if (std::string note = clean_text(c.note.value_or(""), 200); !note.empty()) {
        el["note"] = std::move(note);
    }
    return el;
}

} // namespace

Json timeline_context(const Project& project, double start, double end, std::optional<double> playhead,
                      std::string_view clip_id) {
    if (!project.timeline) {
        return Json{{"activeClip", nullptr}, {"existingElements", Json::array()}, {"motionInRange", Json::array()}};
    }
    const Timeline& tl = *project.timeline;
    std::unordered_set<std::string> hidden;
    TrackNames names;
    std::unordered_map<std::string, std::int64_t> order; // _video_track_order: el último id repetido gana
    std::int64_t video_index = 0;
    for (const TimelineTrack& t : tl.tracks) {
        if (t.hidden) {
            hidden.insert(t.id);
        }
        names[t.id] = t.name;
        if (t.kind == "video") {
            order[t.id] = video_index++;
        }
    }

    const TimelineClip* active = nullptr;
    if (!clip_id.empty()) {
        for (const auto& cow : tl.clips) {
            if (cow->id == clip_id) {
                active = &*cow;
                break;
            }
        }
    }
    if (active == nullptr) {
        const double head = playhead.value_or(start);
        std::vector<const TimelineClip*> on_screen;
        for (const auto& cow : tl.clips) {
            const TimelineClip& c = *cow;
            if (std::ranges::find(kVisualKinds, c.kind) != kVisualKinds.end() && !hidden.contains(c.track_id) &&
                c.start - 1e-3 <= head && head < end_of(c)) {
                on_screen.push_back(&c);
            }
        }
        std::ranges::stable_sort(on_screen, std::ranges::less{}, [&order](const TimelineClip* c) {
            const auto it = order.find(c->track_id);
            return it == order.end() ? std::int64_t{-1} : it->second;
        });
        active = on_screen.empty() ? nullptr : on_screen.back();
    }

    std::vector<const TimelineClip*> by_start;
    for (const auto& cow : tl.clips) {
        by_start.push_back(&*cow);
    }
    std::ranges::stable_sort(by_start, less_nan_last, [](const TimelineClip* c) { return c->start; });
    Json elements = Json::array();
    Json motion = Json::array();
    for (const TimelineClip* cp : by_start) {
        const TimelineClip& c = *cp;
        if (c.kind == "audio" || is_caption_clip(c)) {
            continue;
        }
        const double c0 = or0(c.start);
        const double c1 = end_of(c);
        const double ov = overlap(start, end, c0, c1);
        if (ov <= 0) {
            continue;
        }
        Json el = element(project, c, names);
        if (c.kind == "motion") {
            const double shorter = py_max(1e-3, py_min(end - start, c1 - c0));
            Json m = el;
            m["overlap"] = r3(ov / shorter);
            motion.push_back(std::move(m));
        }
        if (elements.size() < kMaxElements) { // existingElements[:MAX_ELEMENTS]
            elements.push_back(std::move(el));
        }
    }
    return Json{{"activeClip", active != nullptr ? element(project, *active, names) : Json()},
                {"existingElements", std::move(elements)},
                {"motionInRange", std::move(motion)}};
}

Json style_context(const Project& project, double start, double /*end*/) {
    if (!project.timeline) {
        return Json{{"format", Json{{"width", 720}, {"height", 1280}, {"fps", 30}}}};
    }
    const Timeline& tl = *project.timeline;
    Json style{{"format", Json{{"width", tl.width}, {"height", tl.height}, {"fps", tl.fps}}}};
    // min(captions, key=|inicio del subtítulo − start|): el primero de los más cercanos.
    const TimelineClip* near = nullptr;
    double best = 0.0;
    for (const auto& cow : tl.clips) {
        if (!is_caption_clip(*cow)) {
            continue;
        }
        const double key = std::abs(cow->start - start);
        if (near == nullptr || key < best) {
            near = &*cow;
            best = key;
        }
    }
    if (near == nullptr) {
        return style;
    }
    const TimelineTrack* track = nullptr;
    for (const TimelineTrack& t : tl.tracks) {
        if (t.id == near->track_id) {
            track = &t;
            break;
        }
    }
    // {**(estilo de la pista or {}), **(estilo del clip or {})}
    Json s = Json::object();
    const auto merge = [&s](const std::optional<Json>& v) {
        if (v && v->is_object()) {
            for (const auto& [k, x] : v->items()) {
                s[k] = x;
            }
        }
    };
    if (track != nullptr) {
        merge(track->style);
    }
    merge(near->style);
    if (py_truthy(get(s, "font"))) {
        style["captionFont"] = s["font"];
    }
    if (py_truthy(get(s, "color"))) {
        style["captionColor"] = s["color"];
    }
    if (py_truthy(get(s, "highlight_color"))) {
        style["accent"] = s["highlight_color"];
    }
    // isinstance(y, (int, float)): un bool también es un int.
    if (const Json& y = get(s, "y"); y.is_number() || y.is_boolean()) {
        const double v = y.is_boolean() ? (y.get<bool>() ? 1.0 : 0.0) : y.get<double>();
        style["captionZoneY"] = r3(v);
        style["avoidY"] = Json::array({r3(max0(v - 0.07)), r3(py_min(1.0, v + 0.07))});
    }
    return style;
}

} // namespace gc::core
