#include "gc/core/captions_ui.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>

#include "gc/core/captions.hpp"
#include "gc/core/serialize.hpp"
#include "gc/core/text_clip_ui.hpp"
#include "gc/core/timeline_ui.hpp"

namespace gc::core {

namespace {

// Los espacios que quita String.prototype.trim: los de ASCII, los saltos de línea y los de Unicode.
bool js_space(char32_t c) {
    return c == 0x09 || c == 0x0A || c == 0x0B || c == 0x0C || c == 0x0D || c == 0x20 || c == 0xA0 || c == 0x1680 ||
           (c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F || c == 0x3000 ||
           c == 0xFEFF;
}

// El punto de código UTF-8 que empieza en `i` (uno mal formado cuenta como un byte que no es espacio).
std::pair<char32_t, std::size_t> code_point_at(std::string_view s, std::size_t i) {
    const auto b = static_cast<unsigned char>(s[i]);
    const std::size_t n = b < 0x80 ? 1 : (b >> 5) == 0x6 ? 2 : (b >> 4) == 0xE ? 3 : (b >> 3) == 0x1E ? 4 : 0;
    if (n == 0 || i + n > s.size()) {
        return {0xFFFD, 1};
    }
    char32_t c = n == 1 ? b : b & (0x7F >> n);
    for (std::size_t k = 1; k < n; ++k) {
        const auto cont = static_cast<unsigned char>(s[i + k]);
        if ((cont & 0xC0) != 0x80) {
            return {0xFFFD, 1};
        }
        c = (c << 6) | (cont & 0x3F);
    }
    return {c, n};
}

std::string js_trim(std::string_view s) {
    std::size_t b = 0;
    while (b < s.size()) {
        const auto [c, n] = code_point_at(s, b);
        if (!js_space(c)) {
            break;
        }
        b += n;
    }
    std::size_t e = s.size();
    while (e > b) {
        std::size_t k = e - 1;
        while (k > b && (static_cast<unsigned char>(s[k]) & 0xC0) == 0x80) {
            --k;
        }
        const auto [c, n] = code_point_at(s, k);
        if (k + n != e || !js_space(c)) {
            break;
        }
        e = k;
    }
    return std::string(s.substr(b, e - b));
}

// `o.key ?? def` con un número (lo que no lo es cuenta como ausente).
const Json* present(const Json& o, const char* key) {
    if (!o.is_object()) {
        return nullptr;
    }
    const auto it = o.find(key);
    return it != o.end() && !it->is_null() ? &*it : nullptr;
}

double number_or(const Json& o, const char* key, double def) {
    const Json* v = present(o, key);
    return v != nullptr && v->is_number() ? v->get<double>() : def;
}

std::string text_of(const Json& o) {
    const Json* v = present(o, "text");
    return v != nullptr && v->is_string() ? js_trim(v->get<std::string>()) : std::string();
}

// relSegmentWords: las palabras del segmento en tiempos relativos al clip de texto, todas (para que el
// conteo cuadre con el texto) y recortadas a [0, dur].
Json rel_segment_words(const Json& segment, const TimelineClip& src, double clip_start, double dur) {
    Json out = Json::array();
    const Json* ws = present(segment, "words");
    if (ws == nullptr || !ws->is_array()) {
        return out;
    }
    for (const Json& w : *ws) {
        const double from = number_or(w, "start", 0);
        const double to = number_or(w, "end", from); // w.end ?? w.start ?? 0
        const double tl_start = src.start + (from - src.in_point);
        const double tl_end = src.start + (to - src.in_point);
        const double rs = std::min(std::max(0.0, tl_start - clip_start), dur);
        const double re = std::min(std::max(0.0, tl_end - clip_start), dur);
        Json item = {{"text", text_of(w)}, {"start", js_fixed(rs, 3)}, {"end", js_fixed(std::max(rs, re), 3)}};
        if (const Json* prob = present(w, "prob")) {
            item["prob"] = *prob;
        }
        out.push_back(std::move(item));
    }
    return out;
}

} // namespace

bool can_caption_clip(const TimelineClip& clip) {
    if (clip.kind == "text") {
        return false;
    }
    if (clip.asset_kind == "audios" || clip.asset_kind == "sfx") {
        return true;
    }
    if (clip.kind != "video") {
        return false;
    }
    const std::string_view scope = clip.asset_scope.empty() ? std::string_view("project") : clip.asset_scope;
    if (scope == "library" || scope == "collection") {
        return false;
    }
    return !clip.asset_id.empty();
}

Json source_ref_of(const TimelineClip& clip) {
    Json ref = {{"kind", clip.kind.empty() ? Json() : Json(clip.kind)},
                {"asset_id", clip.asset_id},
                {"filename", clip.filename.empty() ? Json() : Json(clip.filename)}};
    if (clip.name && !clip.name->empty()) {
        ref["name"] = *clip.name;
    }
    return ref; // el asset_id de un clip guardado es una cadena: nunca falta
}

std::vector<TimelineClip> caption_text_clips(const TimelineClip& src, const Json& segments, std::string_view track_id,
                                             const Json& style, const Json& transcript, const Json& source_ref,
                                             IdSource& ids) {
    std::vector<TimelineClip> news;
    if (!segments.is_array()) {
        return news;
    }
    const double clip_len = src.out_point - src.in_point;
    const Json* given = present(style, "max_words");
    const Json max_words = given != nullptr ? *given : Json(8); // style?.max_words ?? 8
    const Json* transcript_id = present(transcript, "id");
    for (std::size_t seg_index = 0; seg_index < segments.size(); ++seg_index) {
        const Json& s = segments[seg_index];
        const double ls = number_or(s, "start", 0) - src.in_point;
        const double le = number_or(s, "end", 0) - src.in_point;
        if (le <= 0 || ls >= clip_len) {
            continue;
        }
        const double start = src.start + std::max(0.0, ls);
        const double end = src.start + std::min(clip_len, le);
        const std::string text = text_of(s);
        if (text.empty()) {
            continue;
        }
        Json made = make_text_clip_json(track_id, start, std::max(0.4, end - start), text, style, ids);
        made["text_role"] = "caption";
        const double made_start = made["start"].get<double>();
        const double made_dur = std::max(0.0, made["out_point"].get<double>() - made["in_point"].get<double>());
        made["words"] = rel_segment_words(s, src, made_start, made_dur);
        const Json* index = present(s, "index");
        Json origin = {{"transcript_id", transcript_id != nullptr ? *transcript_id : Json()},
                       {"segment_index", index != nullptr ? *index : Json(static_cast<std::int64_t>(seg_index))},
                       {"source_range", {{"start", s.value("start", Json())}, {"end", s.value("end", Json())}}},
                       {"word_range", Json::array({0, static_cast<std::int64_t>(split_caption_words(text).size())})}};
        if (source_ref.is_object()) {
            origin["source_ref"] = source_ref;
        }
        made["origin"] = std::move(origin);
        auto clip = clip_from_json(made);
        if (!clip) {
            continue; // lo que hace make_text_clip_json siempre es un clip válido
        }
        for (const Json& part : split_clip_by_max_words(*clip, max_words, ids)) {
            if (auto piece = clip_from_json(part)) {
                news.push_back(std::move(*piece));
            }
        }
    }
    return news;
}

} // namespace gc::core
