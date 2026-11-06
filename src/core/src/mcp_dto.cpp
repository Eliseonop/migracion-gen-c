#include "gc/core/mcp_dto.hpp"

#include <array>
#include <cmath>
#include <numeric>
#include <unordered_map>
#include <utility>

#include "gc/core/serialize.hpp"

#include "json_reader.hpp"
#include "py.hpp"

namespace gc::core::mcp {

namespace {

using detail::py_round;
using detail::py_truthy;

constexpr std::array<std::string_view, 37> kCapabilities{
    "clip.position:top|bottom|full",
    "clip.reframe:center|manual|keyframes|auto",
    "clip.look:bw|cinematic|vintage|contrast|warm|cool|saturated",
    "clip.filters:stack(id,amount)|bw|noir|sepia|vintage|faded|matte|cinematic|teal_orange|night|matrix|contrast|"
    "saturated|warm|golden|cool",
    "clip.speed:0.1-10|keep_pitch|reverse",
    "clip.transition:fade|dissolve|wipe|zoom|slide|pop",
    "clip.opacity:0-1",
    "clip.flip:h|v",
    "clip.disabled",
    "clip.beats:detect|every(1|2|4)",
    "timeline.markers",
    "clip.attributes:paste(transform|flip|blend|animation|transitions|crop|effects|mask|chroma|style|speed|audio)",
    "clip.blend:darken|multiply|color_burn|lighten|screen|color_dodge|overlay|soft_light|hard_light|difference|"
    "exclusion",
    "clip.volume:0-2|mute|fade_in|fade_out",
    "clip.effects:blur|grayscale|sepia|brightness|contrast|saturation",
    "clip.audio_fx:eq|compressor|reverb|echo|denoise|distortion|underwater|telephone|radio|megaphone|muffled|ramp",
    "clip.keyframes:x|y|scale|rotation|opacity|volume|audio_fx",
    "clip.animate:zoom|spin|slide|fade|pop|pulse|draw_in|follow_audio",
    "shape.path:points|closed|smooth",
    "shape.dash:solid|dash|dot",
    "clip.track_object:position|position_scale|position_scale_rotation",
    "clip.sound_design:ai|apply",
    "timeline.adjustment_layer:filters|effects|intensity",
    "timeline.cinema_bars:2.39|2|1.85|16:9|animate",
    "timeline.recipes:cinema_grade|text_reflection|pass_through_text|film_strips|subject_pop",
    "clip.duplicate",
    "subtitles.fragmentation:max_words",
    "text.role:caption|free",
    "format:9:16|1:1|16:9|4:5|4:3|custom",
    "media.library",
    "media.image",
    "media.shape",
    "voice:kokoro|piper|gemini|azure",
    "tracks.link",
    "tracks.rename",
    "tracks.audio:volume|mute|fx|fade",
    "history.undo_redo|checkpoints",
};

struct Aspect {
    std::string_view id;
    int w;
    int h;
};
// timeline_ops.ASPECTS, en su orden.
constexpr std::array<Aspect, 6> kAspects{{
    {"16:9", 16, 9},
    {"9:16", 9, 16},
    {"1:1", 1, 1},
    {"4:3", 4, 3},
    {"3:4", 3, 4},
    {"4:5", 4, 5},
}};
constexpr double kRatioTol = 0.01;

// `x or 0.0` de Python sobre un real: el cero (también −0,0) es falso y pasa a 0,0.
double or_zero(double x) {
    return x == 0.0 ? 0.0 : x;
}

// max(a, b) de Python: b solo si es mayor (con NaN, a).
double py_max(double a, double b) {
    return b > a ? b : a;
}

// `len(x)` de Python sobre un valor JSON; nada si Python lanza TypeError.
std::optional<std::size_t> py_len(const Json& v) {
    if (v.is_string()) {
        return detail::py_len(v.get_ref<const std::string&>());
    }
    if (v.is_array() || v.is_object()) {
        return v.size();
    }
    return std::nullopt;
}

// El nombre del tipo de Python de un escalar JSON, para «object of type 'int' has no len()».
const char* py_type(const Json& v) {
    if (v.is_boolean()) {
        return "bool";
    }
    if (v.is_number_float()) {
        return "float";
    }
    if (v.is_number()) {
        return "int";
    }
    return "NoneType";
}

Json opt(const std::optional<std::string>& s) {
    return s ? Json(*s) : Json(nullptr);
}

template <class T>
Json opt_num(const std::optional<T>& v) {
    return v ? Json(*v) : Json(nullptr);
}

// text_role.resolve_text_role sobre el clip (un objeto de Python, no un dict).
Json text_role(const TimelineClip& c) {
    if (c.kind != "text") {
        return nullptr;
    }
    if (c.text_role && (*c.text_role == "caption" || *c.text_role == "free")) {
        return *c.text_role;
    }
    if ((c.origin && py_truthy(*c.origin)) || !c.words.empty()) {
        return "caption";
    }
    return "free";
}

Json format_dto(const Timeline* tl) {
    const std::int64_t w = tl != nullptr ? tl->width : 720;
    const std::int64_t h = tl != nullptr ? tl->height : 1280;
    const std::int64_t fps = tl != nullptr ? tl->fps : 30;
    return {{"aspect", aspect_ratio(w, h)}, {"width", w}, {"height", h}, {"fps", fps}};
}

double timeline_end(const Timeline& tl) {
    double end = 0.0;
    for (const auto& c : tl.clips) {
        end = py_max(end, or_zero(c->start) + clip_timeline_duration(*c));
    }
    return end;
}

// dto._dup_counts: cuántos otros clips comparten el linaje (dup_of o, si no, el id) de cada id.
std::unordered_map<std::string, std::int64_t> dup_counts(const Timeline& tl) {
    const auto root = [](const TimelineClip& c) -> const std::string& {
        return c.dup_of && !c.dup_of->empty() ? *c.dup_of : c.id;
    };
    std::unordered_map<std::string, std::int64_t> roots;
    for (const auto& c : tl.clips) {
        ++roots[root(*c)];
    }
    std::unordered_map<std::string, std::int64_t> out;
    for (const auto& c : tl.clips) {
        out[c->id] = std::max<std::int64_t>(0, roots[root(*c)] - 1);
    }
    return out;
}

Result<Json> clip_summary(const TimelineClip& c, std::int64_t dup_count, const std::string* track_kind) {
    Json d = {
        {"id", c.id},
        {"track_id", c.track_id},
        {"track_kind", track_kind != nullptr && !track_kind->empty() ? *track_kind : c.kind},
        {"kind", c.kind},
        {"name", opt(c.name)},
        {"start", py_round(or_zero(c.start), 3)},
        {"duration", py_round(clip_timeline_duration(c), 3)},
        {"in_point", c.in_point},
        {"out_point", c.out_point},
        {"frame", c.frame},
        {"layout", c.layout},
        {"look", c.look},
        {"speed", c.speed},
        {"muted", c.muted},
        {"has_reframe", c.reframe.has_value()},
        {"face_track_mode", c.reframe ? opt(c.reframe->face_track_mode) : Json(nullptr)},
        {"asset_id", c.asset_id},
        {"asset_kind", c.asset_kind},
        {"asset_scope", c.asset_scope},
        {"dup_of", opt(c.dup_of)},
        {"dup_count", dup_count},
        {"description", opt(c.description)},
        {"trim", {{"in_point", c.in_point}, {"out_point", c.out_point}, {"source_duration", c.source_duration}}},
    };
    if (c.flip_h || c.flip_v) {
        d["flip"] = {{"h", c.flip_h}, {"v", c.flip_v}};
    }
    if (c.blend_mode && !c.blend_mode->empty()) {
        d["blend_mode"] = *c.blend_mode;
    }
    if (c.disabled) {
        d["disabled"] = true;
    }
    if (c.beats && py_truthy(*c.beats) && c.beats->is_object()) {
        const Json& beats = *c.beats;
        const auto times = beats.find("times");
        if (times != beats.end() && py_truthy(*times)) {
            const auto count = py_len(*times);
            if (!count) {
                return fail(ErrorCode::Internal, std::string("object of type '") + py_type(*times) + "' has no len()");
            }
            const auto bpm = beats.find("bpm");
            const auto every = beats.find("every");
            d["beats"] = {{"bpm", bpm != beats.end() ? *bpm : Json(nullptr)},
                          {"count", *count},
                          {"every", every != beats.end() ? *every : Json(1)}};
        }
    }
    if (c.kind == "video" || c.kind == "audio") {
        d["volume"] = py_round(c.volume, 3);
        d["has_audio_fx"] = c.audio_fx && py_truthy(*c.audio_fx);
    }
    if (c.kind == "text") {
        d["text"] = opt(c.text);
        d["text_role"] = text_role(c);
        d["word_count"] = c.words.size();
    }
    return d;
}

Json media_dto(const Project& p) {
    Json clips = Json::array();
    for (const ClipInfo& c : p.clips) {
        clips.push_back({{"id", c.id && !c.id->empty() ? *c.id : std::to_string(c.index)},
                         {"index", c.index},
                         {"label", c.label && !c.label->empty() ? *c.label : c.filename},
                         {"description", opt(c.description)},
                         {"duration", py_round(or_zero(c.end) - or_zero(c.start), 3)},
                         {"has_transcript", c.transcript.has_value()}});
    }
    Json audios = Json::array();
    for (const AudioInfo& a : p.audios) {
        audios.push_back({{"id", a.id},
                          {"label", a.label && !a.label->empty() ? *a.label : a.filename},
                          {"description", opt(a.description)},
                          {"duration", opt_num(a.duration)}});
    }
    Json images = Json::array();
    for (const ImageInfo& im : p.images) {
        images.push_back({{"id", im.id},
                          {"label", im.label && !im.label->empty() ? *im.label : im.filename},
                          {"description", opt(im.description)},
                          {"width", opt_num(im.width)},
                          {"height", opt_num(im.height)}});
    }
    return {{"clips", p.clips.size()},
            {"audios", p.audios.size()},
            {"images", p.images.size()},
            {"transcripts", p.transcripts.size()},
            {"clip_list", std::move(clips)},
            {"audio_list", std::move(audios)},
            {"image_list", std::move(images)}};
}

// `x.get(key)` de un dict JSON; null si no está (o si no es un dict).
const Json& get(const Json& obj, std::string_view key) {
    static const Json null_value;
    if (!obj.is_object()) {
        return null_value;
    }
    const auto it = obj.find(key);
    return it == obj.end() ? null_value : *it;
}

} // namespace

std::span<const std::string_view> editor_capabilities() {
    return kCapabilities;
}

std::optional<std::string_view> aspect_of(std::int64_t width, std::int64_t height) {
    if (width <= 0 || height <= 0) {
        return std::nullopt;
    }
    const double r = static_cast<double>(width) / static_cast<double>(height);
    for (const Aspect& a : kAspects) {
        const double target = static_cast<double>(a.w) / a.h;
        if (std::abs(r - target) / target < kRatioTol) {
            return a.id;
        }
    }
    return std::nullopt;
}

std::string aspect_ratio(std::int64_t width, std::int64_t height) {
    if (width <= 0 || height <= 0) {
        return "?";
    }
    if (const auto known = aspect_of(width, height)) {
        return std::string(*known);
    }
    const std::int64_t d = std::gcd(width, height);
    return std::to_string(width / d) + ":" + std::to_string(height / d);
}

double clip_timeline_duration(const TimelineClip& clip) {
    const double span = py_max(0.0, or_zero(clip.out_point) - or_zero(clip.in_point));
    if (clip.kind == "text" || clip.kind == "image") {
        return span;
    }
    const double speed = clip.speed == 0.0 ? 1.0 : clip.speed;
    return speed > 0 ? span / speed : span;
}

Json timeline_dto(const Timeline* tl) {
    Json kinds = {{"video", 0}, {"audio", 0}, {"text", 0}};
    if (tl == nullptr) {
        return {{"present", false}, {"tracks", Json::array()}, {"clip_count", 0}, {"duration", 0.0}, {"kinds", kinds}};
    }
    for (const auto& c : tl->clips) {
        Json& n = kinds[c->kind];
        n = (n.is_number() ? n.get<std::int64_t>() : 0) + 1;
    }
    Json tracks = Json::array();
    for (const TimelineTrack& t : tl->tracks) {
        tracks.push_back({{"id", t.id}, {"kind", t.kind}, {"name", t.name}, {"linked_track_id", opt(t.linked_track_id)}});
    }
    return {{"present", true},
            {"tracks", std::move(tracks)},
            {"clip_count", tl->clips.size()},
            {"duration", py_round(timeline_end(*tl), 3)},
            {"kinds", std::move(kinds)}};
}

Result<Json> timeline_detail(const Timeline* tl) {
    if (tl == nullptr) {
        return Json{{"present", false},
                    {"format", format_dto(nullptr)},
                    {"tracks", Json::array()},
                    {"clips", Json::array()},
                    {"duration", 0.0}};
    }
    const auto counts = dup_counts(*tl);
    // {t.id: t.kind}: con dos pistas con el mismo id, gana la última.
    std::unordered_map<std::string, const std::string*> kinds_by_id;
    Json tracks = Json::array();
    for (const TimelineTrack& t : tl->tracks) {
        kinds_by_id[t.id] = &t.kind;
        Json track = {{"id", t.id},           {"kind", t.kind},   {"name", t.name},
                      {"hidden", t.hidden},   {"muted", t.muted}, {"locked", t.locked},
                      {"linked_track_id", opt(t.linked_track_id)}};
        if (t.magnetic) {
            track["magnetic"] = true;
        }
        tracks.push_back(std::move(track));
    }
    Json clips = Json::array();
    for (const auto& c : tl->clips) {
        const auto kind = kinds_by_id.find(c->track_id);
        const auto count = counts.find(c->id);
        auto summary = clip_summary(*c, count != counts.end() ? count->second : 0,
                                    kind != kinds_by_id.end() ? kind->second : nullptr);
        if (!summary) {
            return std::unexpected(summary.error());
        }
        clips.push_back(std::move(*summary));
    }
    Json markers = Json::array();
    for (const Json& m : tl->markers) {
        markers.push_back(m);
    }
    return Json{{"present", true},
                {"format", format_dto(tl)},
                {"tracks", std::move(tracks)},
                {"clips", std::move(clips)},
                {"markers", std::move(markers)},
                {"duration", py_round(timeline_end(*tl), 3)}};
}

Json clip_detail(const TimelineClip& clip) {
    Json data = to_json(clip, DumpMode::Model);
    data["timeline_duration"] = py_round(clip_timeline_duration(clip), 3);
    return data;
}

Json media_list(const Project& p) {
    Json clips = Json::array();
    for (const ClipInfo& c : p.clips) {
        clips.push_back({{"id", c.id && !c.id->empty() ? *c.id : std::to_string(c.index)},
                         {"index", c.index},
                         {"filename", c.filename},
                         {"label", opt(c.label)},
                         {"description", opt(c.description)},
                         {"start", c.start},
                         {"end", c.end},
                         {"duration", py_round(or_zero(c.end) - or_zero(c.start), 3)},
                         {"origin", opt(c.origin)},
                         {"source", opt(c.source)},
                         {"has_transcript", c.transcript.has_value()}});
    }
    Json audios = Json::array();
    for (const AudioInfo& a : p.audios) {
        audios.push_back({{"id", a.id},
                          {"filename", a.filename},
                          {"label", opt(a.label)},
                          {"duration", opt_num(a.duration)},
                          {"voice", opt(a.voice)},
                          {"engine", opt(a.engine)},
                          {"description", opt(a.description)},
                          {"origin", opt(a.origin)},
                          {"source", opt(a.source)}});
    }
    Json images = Json::array();
    for (const ImageInfo& im : p.images) {
        images.push_back({{"id", im.id},
                          {"filename", im.filename},
                          {"label", opt(im.label)},
                          {"description", opt(im.description)},
                          {"width", opt_num(im.width)},
                          {"height", opt_num(im.height)},
                          {"origin", opt(im.origin)},
                          {"source", opt(im.source)}});
    }
    Json transcripts = Json::array();
    for (const Transcript& t : p.transcripts) {
        transcripts.push_back({{"id", t.id},
                               {"title", opt(t.title)},
                               {"language", opt(t.language)},
                               {"segment_count", t.segments.size()},
                               {"duration", opt_num(t.duration)}});
    }
    return {{"clips", std::move(clips)},
            {"audios", std::move(audios)},
            {"images", std::move(images)},
            {"transcripts", std::move(transcripts)}};
}

Json analyze_dto(const Json& resp) {
    const Json& video = get(resp, "video");
    Json qualities = Json::array();
    if (const Json& qs = get(video, "qualities"); qs.is_array()) {
        for (const Json& q : qs) {
            qualities.push_back(get(q, "quality"));
        }
    }
    Json segments = Json::array();
    if (const Json& segs = get(resp, "segments"); segs.is_array()) {
        for (const Json& s : segs) {
            const Json& score = get(s, "score");
            segments.push_back({{"index", get(s, "index")},
                                {"start", get(s, "start")},
                                {"end", get(s, "end")},
                                {"duration", get(s, "duration")},
                                {"score", score.is_number() ? Json(py_round(score.get<double>(), 3)) : score}});
        }
    }
    return {{"video",
             {{"id", get(video, "id")},
              {"title", get(video, "title")},
              {"duration", get(video, "duration")},
              {"uploader", get(video, "uploader")},
              {"qualities", std::move(qualities)}}},
            {"has_heatmap", get(resp, "has_heatmap")},
            {"segments", std::move(segments)}};
}

Json job_dto(const Json& job) {
    Json status = get(job, "status");
    if (py_truthy(get(job, "cancel_requested")) && status != "done") {
        status = "cancelled";
    }
    Json d = {{"id", get(job, "id")}, {"status", std::move(status)}, {"progress", get(job, "progress")},
              {"message", get(job, "message")}};
    if (const Json& error = get(job, "error"); py_truthy(error)) {
        d["error"] = error;
    }
    Json result = Json::object();
    if (const Json& clips = get(job, "clips"); py_truthy(clips)) {
        result["clips"] = clips.size();
    }
    if (const Json& url = get(job, "export_url"); py_truthy(url)) {
        result["export_url"] = url;
    }
    if (const Json& transcript = get(job, "transcript"); !transcript.is_null()) {
        result["transcript_id"] = get(transcript, "id");
    }
    if (const Json& audio = get(job, "audio"); !audio.is_null()) {
        result["audio_id"] = get(audio, "id");
    }
    if (!get(job, "reframe_prep").is_null()) {
        result["reframe_prep"] = true;
    }
    if (const Json& extra = get(job, "result"); py_truthy(extra) && extra.is_object()) {
        for (const auto& [key, value] : extra.items()) {
            result[key] = value; // dict.update: una clave que ya está se queda en su sitio
        }
    }
    if (!result.empty()) {
        d["result"] = std::move(result);
    }
    return d;
}

Json project_context(const Project& p, const Json& history) {
    const Timeline* tl = p.timeline ? &*p.timeline : nullptr;
    const Json& hist = py_truthy(history) ? history : Json::object();
    Json checkpoints = Json::array();
    if (const Json& names = get(hist, "checkpoints"); names.is_array()) {
        checkpoints = names;
    } else if (const auto items = detail::py_iter(names)) {
        checkpoints = Json(*items); // list(x): las claves de un dict, los caracteres de un texto
    }
    return {{"project", {{"id", p.id}, {"name", p.name}, {"created_at", p.created_at}, {"folder", opt(p.folder)}}},
            {"format", format_dto(tl)},
            {"media", media_dto(p)},
            {"timeline", timeline_dto(tl)},
            {"history",
             {{"can_undo", py_truthy(get(hist, "can_undo"))},
              {"can_redo", py_truthy(get(hist, "can_redo"))},
              {"checkpoints", std::move(checkpoints)}}}};
}

} // namespace gc::core::mcp
