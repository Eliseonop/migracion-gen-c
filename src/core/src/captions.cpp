#include "gc/core/captions.hpp"

#include <algorithm>

#include "gc/core/migrations.hpp"
#include "json_reader.hpp"
#include "py.hpp"

namespace gc::core {

using detail::py_float;
using detail::py_round;
using detail::py_truthy;

namespace {

double r3(double v) { return py_round(v, 3); }

std::unexpected<Error> invalid(std::string message) { return fail(ErrorCode::InvalidArgument, std::move(message)); }

// `_get(obj, key, default)` de fragment.py: dict.get o el defecto (los objetos que no
// son dict no tienen esos atributos).
const Json& get(const Json& obj, std::string_view key, const Json& fallback) {
    if (!obj.is_object()) {
        return fallback;
    }
    const auto it = obj.find(key);
    return it == obj.end() ? fallback : *it;
}

Result<double> to_float(const Json& v, std::string_view what) {
    if (const auto f = py_float(v)) {
        return *f;
    }
    return invalid("subtítulos: '" + std::string(what) + "' no es un número");
}

// `(x or "").strip()`
Result<std::string> stripped_text(const Json& v) {
    if (!py_truthy(v)) {
        return std::string();
    }
    if (!v.is_string()) {
        return invalid("subtítulos: el texto debe ser texto");
    }
    return detail::py_strip(v.get<std::string>());
}

std::string join(const std::vector<std::string>& words, std::size_t from, std::size_t to) {
    std::string out;
    for (std::size_t i = from; i < to; ++i) {
        out += (i > from ? " " : "") + words[i];
    }
    return out;
}

Json make_text_clip(const std::string& track_id, double start, double dur, const std::string& text,
                    const Json& style, IdSource& ids) {
    const std::string body = text.empty() ? "Texto" : text;
    Json c = Json::object();
    c["id"] = make_uid(ids, "c");
    c["track_id"] = track_id;
    c["kind"] = "text";
    c["asset_kind"] = "text";
    c["asset_id"] = make_uid(ids, "t");
    c["filename"] = "";
    c["name"] = detail::py_slice(body, 22);
    c["start"] = r3(std::max(0.0, start));
    c["in_point"] = 0.0;
    c["out_point"] = r3(std::max(0.5, dur));
    c["source_duration"] = r3(std::max(0.5, dur));
    c["volume"] = 1;
    c["muted"] = false;
    c["reframe"] = nullptr;
    c["text"] = body;
    c["style"] = style.is_object() ? style : Json::object();
    c["words"] = Json::array();
    c["text_role"] = "caption";
    return c;
}

Result<Json> rel_segment_words(const Json& segment, double s_start, double s_in, double clip_start, double dur) {
    static const Json kNull;
    static const Json kZero = 0;
    static const Json kEmpty = "";
    Json out = Json::array();
    const Json& ws = get(segment, "words", kNull);
    if (!py_truthy(ws)) {
        return out;
    }
    const auto words = detail::py_iter(ws);
    if (!words) {
        return invalid("subtítulos: words debe ser una lista");
    }
    for (const Json& w : *words) {
        const Json& raw_start = get(w, "start", kZero);
        const Json& w_start = py_truthy(raw_start) ? raw_start : kZero;
        const Json& raw_end = get(w, "end", w_start);
        const Json& w_end = py_truthy(raw_end) ? raw_end : w_start;
        auto ws_f = to_float(w_start, "words[].start");
        auto we_f = to_float(w_end, "words[].end");
        if (!ws_f || !we_f) {
            return std::unexpected(!ws_f ? ws_f.error() : we_f.error());
        }
        const double tl_start = s_start + (*ws_f - s_in);
        const double tl_end = s_start + (*we_f - s_in);
        const double rs = std::min(std::max(0.0, tl_start - clip_start), dur);
        const double re = std::min(std::max(0.0, tl_end - clip_start), dur);
        auto text = stripped_text(get(w, "text", kEmpty));
        if (!text) {
            return std::unexpected(text.error());
        }
        Json item = {{"text", *text}, {"start", r3(rs)}, {"end", r3(std::max(rs, re))}};
        const Json& prob = get(w, "prob", kNull);
        if (!prob.is_null()) {
            item["prob"] = prob;
        }
        out.push_back(std::move(item));
    }
    return out;
}

Result<std::vector<Json>> split_clip_by_max_words(Json clip, const Json& max_words, IdSource& ids) {
    if (resolve_text_role(clip) == "free") {
        return std::vector<Json>{std::move(clip)};
    }
    const std::string text = clip["text"].is_string() ? clip["text"].get<std::string>() : std::string();
    const auto chunks = chunk_caption_text(text, max_words);
    if (chunks.size() <= 1) {
        return std::vector<Json>{std::move(clip)};
    }
    const auto all_words = split_caption_words(text);
    const double total = all_words.empty() ? 1.0 : static_cast<double>(all_words.size());
    const double dur = std::max(0.0, clip["out_point"].get<double>() - clip["in_point"].get<double>());
    const double base = clip["start"].get<double>();
    const Json& words = clip["words"];
    const bool has_words = words.is_array() && static_cast<double>(words.size()) == total;
    const Json origin = clip.contains("origin") ? clip["origin"] : Json();
    std::int64_t origin_start = 0;
    if (py_truthy(origin)) {
        static const Json kNone;
        const Json& range = get(origin, "word_range", kNone);
        origin_start = py_truthy(range) ? range[0].get<std::int64_t>() : 0;
    }

    std::vector<Json> out;
    std::size_t acc = 0;
    for (std::size_t i = 0; i < chunks.size(); ++i) {
        const std::size_t n = split_caption_words(chunks[i]).size();
        const std::size_t frm = acc;
        const std::size_t to = acc + n;
        acc = to;
        Json frag_words = Json::array();
        double start = 0.0;
        double d = 0.0;
        if (has_words) {
            const bool any = to > frm && frm < words.size();
            const double w0 = any ? words[frm]["start"].get<double>() : dur * (static_cast<double>(frm) / total);
            const double wn = any ? words[std::min(to, words.size()) - 1]["end"].get<double>()
                                  : dur * (static_cast<double>(to) / total);
            start = base + w0;
            d = std::max(0.05, wn - w0);
            for (std::size_t k = frm; k < std::min(to, words.size()); ++k) {
                const Json& w = words[k];
                Json item = {{"text", w["text"]},
                             {"start", r3(std::max(0.0, w["start"].get<double>() - w0))},
                             {"end", r3(std::max(0.0, w["end"].get<double>() - w0))}};
                if (w.contains("prob") && !w["prob"].is_null()) {
                    item["prob"] = w["prob"];
                }
                frag_words.push_back(std::move(item));
            }
        } else {
            start = base + dur * (static_cast<double>(frm) / total);
            const double end = i == chunks.size() - 1 ? base + dur : base + dur * (static_cast<double>(to) / total);
            d = std::max(0.05, end - start);
        }
        Json frag = clip;
        frag["id"] = make_uid(ids, "c");
        frag["asset_id"] = make_uid(ids, "t");
        frag["text"] = chunks[i];
        frag["name"] = detail::py_slice(chunks[i], 22);
        frag["start"] = r3(start);
        frag["in_point"] = 0.0;
        frag["out_point"] = r3(d);
        frag["source_duration"] = r3(d);
        frag["style"] = clip["style"].is_object() ? clip["style"] : Json::object();
        frag["words"] = std::move(frag_words);
        if (py_truthy(origin)) {
            Json o = origin;
            o["fragment_index"] = static_cast<std::int64_t>(i);
            o["word_range"] = Json::array({origin_start + static_cast<std::int64_t>(frm),
                                           origin_start + static_cast<std::int64_t>(to)});
            frag["origin"] = std::move(o);
        }
        out.push_back(std::move(frag));
    }
    return out;
}

} // namespace

std::vector<std::string> split_caption_words(const std::string& text) { return detail::py_split(text); }

std::vector<std::string> chunk_caption_text(const std::string& text, const Json& max_words) {
    const auto words = split_caption_words(text);
    if (words.empty()) {
        return {};
    }
    const auto n = detail::py_int(max_words); // int(max_words); si falla, un solo trozo
    if (!n || *n < 1) {
        return {join(words, 0, words.size())};
    }
    std::vector<std::string> out;
    const auto step = static_cast<std::size_t>(*n);
    for (std::size_t i = 0; i < words.size(); i += step) {
        out.push_back(join(words, i, std::min(words.size(), i + step)));
    }
    return out;
}

Result<std::vector<Json>> text_clips_from_transcript(double src_start, double src_in, double src_out,
                                                     const Json& segments, const std::string& track_id,
                                                     const Json& style, const Json& transcript_id, IdSource& ids) {
    static const Json kZero = 0;
    static const Json kEmpty = "";
    if (py_truthy(style) && !style.is_object()) {
        return invalid("style debe ser un objeto");
    }
    const double clip_len = src_out - src_in;
    const Json max_words = get(style, "max_words", Json(8));
    std::vector<Json> news;
    const auto segs = py_truthy(segments) ? detail::py_iter(segments) : std::optional<std::vector<Json>>(std::vector<Json>{});
    if (!segs) {
        return invalid("segments debe ser una lista");
    }
    for (std::size_t seg_index = 0; seg_index < segs->size(); ++seg_index) {
        const Json& s = (*segs)[seg_index];
        auto s_start = to_float(get(s, "start", kZero), "start");
        auto s_end = to_float(get(s, "end", kZero), "end");
        if (!s_start || !s_end) {
            return std::unexpected(!s_start ? s_start.error() : s_end.error());
        }
        const double ls = *s_start - src_in;
        const double le = *s_end - src_in;
        if (le <= 0 || ls >= clip_len) {
            continue;
        }
        const double start = src_start + std::max(0.0, ls);
        const double end = src_start + std::min(clip_len, le);
        auto text = stripped_text(get(s, "text", kEmpty));
        if (!text) {
            return std::unexpected(text.error());
        }
        if (text->empty()) {
            continue;
        }
        Json made = make_text_clip(track_id, start, std::max(0.4, end - start), *text, style, ids);
        const double made_dur = std::max(0.0, made["out_point"].get<double>() - made["in_point"].get<double>());
        auto words = rel_segment_words(s, src_start, src_in, made["start"].get<double>(), made_dur);
        if (!words) {
            return std::unexpected(words.error());
        }
        made["words"] = std::move(*words);
        made["origin"] = {{"transcript_id", transcript_id},
                          {"segment_index", get(s, "index", Json(static_cast<std::int64_t>(seg_index)))},
                          {"source_range", {{"start", get(s, "start", kZero)}, {"end", get(s, "end", kZero)}}},
                          {"word_range", Json::array({0, static_cast<std::int64_t>(split_caption_words(*text).size())})}};
        auto parts = split_clip_by_max_words(std::move(made), max_words, ids);
        if (!parts) {
            return std::unexpected(parts.error());
        }
        for (auto& p : *parts) {
            news.push_back(std::move(p));
        }
    }
    return news;
}

} // namespace gc::core
