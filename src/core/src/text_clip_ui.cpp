#include "gc/core/text_clip_ui.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <initializer_list>

#include "gc/core/captions.hpp"
#include "gc/core/clip_pose_edit.hpp"
#include "gc/core/migrations.hpp"
#include "gc/core/serialize.hpp"
#include "gc/core/text_style.hpp"
#include "gc/core/timeline_view.hpp"
#include "gc/core/keyframes.hpp"
#include "gc/core/timeline_ui.hpp"
#include "gc/core/timeline_view.hpp"

namespace gc::core {

namespace {

constexpr std::array<std::string_view, 9> kLookKeys{"color",  "border_width", "border_color", "bg",       "bg_opacity",
                                                    "shadow", "shadow_color", "glow",         "glow_color"};

#include "text_themes.inc"

const Json& field(const Json& obj, std::string_view key) {
    static const Json kNull;
    if (!obj.is_object()) {
        return kNull;
    }
    const auto it = obj.find(std::string(key));
    return it == obj.end() ? kNull : *it;
}

bool truthy(const Json& v) {
    if (v.is_null()) {
        return false;
    }
    if (v.is_boolean()) {
        return v.get<bool>();
    }
    if (v.is_number()) {
        return v.get<double>() != 0 && !std::isnan(v.get<double>());
    }
    return !v.is_string() || !v.get_ref<const std::string&>().empty();
}

// String(x) de JS para lo que guarda un estilo.
std::string js_string(const Json& v) {
    if (v.is_string()) {
        return v.get<std::string>();
    }
    if (v.is_null()) {
        return "null";
    }
    if (v.is_boolean()) {
        return v.get<bool>() ? "true" : "false";
    }
    if (v.is_number_integer()) {
        return v.dump();
    }
    if (v.is_number()) {
        const double d = v.get<double>();
        return d == std::trunc(d) && std::abs(d) < 1e15 ? std::to_string(static_cast<long long>(d)) : v.dump();
    }
    return v.dump();
}

double js_round_half_up(double v) { // Math.round
    const double r = std::floor(v);
    return v - r >= 0.5 ? r + 1 : r;
}

std::string lower(std::string s) {
    std::ranges::transform(s, s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

// a ?? b ?? c de JS: el primero que no sea null; sin ninguno, la clave no se escribe.
void coalesce(Json& out, const char* key, std::initializer_list<Json> values) {
    for (const Json& v : values) {
        if (!v.is_null()) {
            out[key] = v;
            return;
        }
    }
    out.erase(key);
}

// karaokeOn: algún efecto de palabra conocido (highlight, glow, pop).
bool karaoke_on(const Json& style) {
    const Json& fx = field(style, "word_fx");
    const auto known = [](const Json& x) { return x == "highlight" || x == "glow" || x == "pop"; };
    return fx.is_array() ? std::ranges::any_of(fx, known) : known(fx);
}

} // namespace

Json default_text_style() {
    return {{"font", "Arial"},
            {"size", 0.009375},
            {"color", "#ffffff"},
            {"bold", true},
            {"align", "center"},
            {"x", 0.5},
            {"y", 0.5},
            {"w", 0.8},
            {"border_width", 6},
            {"border_color", "#000000"},
            {"shadow", false},
            {"shadow_color", "#000000"},
            {"glow", false},
            {"bg", "none"},
            {"bg_opacity", 0.55},
            {"highlight_color", "#ffe566"},
            {"word_fx", "none"},
            {"block_appear", "none"},
            {"inactive_opacity", 1},
            {"active_opacity", 1},
            {"opacity", 1},
            {"max_words", 8},
            {"preset", "outline"}};
}

Json subtitle_style() { return apply_theme_to_style(Json::object(), *theme_by_id("classic")); }

const Json& subtitle_themes() {
    static const Json themes = Json::parse(kSubtitleThemesJson);
    return themes;
}

const Json* theme_by_id(std::string_view id) {
    const auto& list = subtitle_themes();
    const auto it = std::ranges::find_if(list, [&](const Json& t) { return t.at("id") == id; });
    return it == list.end() ? nullptr : &*it;
}

Json apply_theme_to_style(const Json& current, const Json& theme) {
    const Json& pack = field(theme, "style").is_object() ? theme.at("style") : Json::object();
    Json out = pack;
    out["theme"] = theme.at("id");
    out["preset"] = theme.at("id");
    for (const char* key : {"x", "y", "w", "max_words"}) {
        coalesce(out, key, {field(current, key), field(pack, key)});
    }
    coalesce(out, "opacity", {field(current, "opacity"), field(pack, "opacity"), Json(1)});
    return out;
}

Json clear_text_theme(const Json& current) {
    const Json plain = default_text_style();
    Json out = plain;
    for (const char* key : {"x", "y", "w", "size", "max_words"}) {
        coalesce(out, key, {field(current, key), field(plain, key)});
    }
    coalesce(out, "opacity", {field(current, "opacity"), Json(1)});
    return out;
}

std::optional<std::string> selected_subtitle_theme_id(const Json& style) {
    const Json& id = truthy(field(style, "theme")) ? field(style, "theme") : field(style, "preset");
    const Json* theme = id.is_string() ? theme_by_id(id.get<std::string>()) : nullptr;
    if (!theme || (karaoke_on(theme->at("style")) && !karaoke_on(style))) {
        return std::nullopt;
    }
    return id.get<std::string>();
}

Json apply_or_clear_theme(const Json& current, const Json* theme) {
    if (!theme || selected_subtitle_theme_id(current) == theme->at("id").get<std::string>()) {
        return clear_text_theme(current);
    }
    return apply_theme_to_style(current, *theme);
}

const Json& text_looks() {
    static const Json looks = Json::parse(kTextLooksJson);
    return looks;
}

Json look_patch(const Json* look) {
    Json out = Json::parse(kLookBaseJson);
    if (look && field(*look, "style").is_object()) {
        for (const auto& [key, value] : look->at("style").items()) {
            out[key] = value;
        }
    }
    return out;
}

std::optional<std::string> active_look_id(const Json& style) {
    static const Json base = Json::parse(kLookBaseJson);
    const auto glow_color = [&] {
        for (const char* key : {"glow_color", "shadow_color"}) {
            if (truthy(field(style, key))) {
                return js_string(field(style, key));
            }
        }
        return std::string("#ffffff");
    };
    const auto same = [&](const Json& ref) {
        return std::ranges::all_of(kLookKeys, [&](std::string_view k) {
            const Json& a = field(style, k).is_null() ? field(base, k) : field(style, k);
            const Json& b = field(ref, k);
            if (k == "bg") {
                return (truthy(a) ? js_string(a) : "none") == (truthy(b) ? js_string(b) : "none");
            }
            if (k == "bg_opacity" && (truthy(field(ref, "bg")) ? js_string(field(ref, "bg")) : "none") == "none") {
                return true;
            }
            if (k == "shadow_color" && !truthy(field(ref, "shadow"))) {
                return true;
            }
            if (k == "glow_color") {
                return !truthy(field(ref, "glow")) || lower(glow_color()) == lower(js_string(b));
            }
            if (b.is_string()) {
                return lower(js_string(a)) == lower(b.get<std::string>());
            }
            return js_number(a) == js_number(b) || a == b;
        });
    };
    if (same(base)) {
        return "none";
    }
    for (const Json& look : text_looks()) {
        if (same(look.at("style"))) {
            return look.at("id").get<std::string>();
        }
    }
    return std::nullopt;
}

Json new_text_style(const Json& track_style) {
    Json style = default_text_style();
    if (track_style.is_object()) {
        for (const auto& [key, value] : track_style.items()) {
            style[key] = value;
        }
        return style;
    }
    const Json sub = subtitle_style();
    for (const char* key : {"x", "y", "w", "size", "opacity"}) {
        style[key] = sub.at(key);
    }
    return style;
}

Json place_text_clip(const Json& payload, const Json& track_style, std::string_view track_id, double start,
                     std::optional<double> fit, IdSource& ids) {
    const Json& theme_id = field(payload, "theme_id");
    const Json* preset = theme_id.is_string() ? theme_by_id(theme_id.get_ref<const std::string&>()) : nullptr;
    const Json& duration = field(payload, "duration");
    const double dur = fit && *fit > 0           ? *fit
                       : truthy(duration)        ? js_number(duration)
                                                 : kTextDefaultDuration;
    const Json& text = field(payload, "text");
    const Json base = new_text_style(truthy(track_style) ? track_style : Json());
    return make_text_clip_json(track_id, start, dur, truthy(text) ? js_string(text) : std::string(kDefaultText),
                               preset ? apply_theme_to_style(base, *preset) : base, ids);
}

Json make_text_clip_json(std::string_view track_id, double start, double duration, std::string_view text,
                         const Json& style, IdSource& ids) {
    const std::string body = text.empty() ? "Texto" : std::string(text);
    const double span = js_fixed3(std::max(0.5, duration));
    Json c = Json::object();
    c["id"] = make_uid(ids, "c");
    c["track_id"] = track_id;
    c["kind"] = "text";
    c["asset_kind"] = "text";
    c["asset_id"] = make_uid(ids, "t");
    c["filename"] = "";
    c["name"] = slice_utf16(body, 22);
    c["start"] = js_fixed3(std::max(0.0, start));
    c["in_point"] = 0;
    c["out_point"] = span;
    c["source_duration"] = span;
    c["volume"] = 1;
    c["muted"] = false;
    c["speed"] = 1;
    c["keep_pitch"] = true;
    c["reverse"] = false;
    c["speed_curve"] = nullptr;
    c["reframe"] = nullptr;
    c["text"] = body;
    c["style"] = style.is_object() ? style : default_text_style();
    c["words"] = Json::array();
    c["text_role"] = "free";
    c["description"] = nullptr;
    c["dup_of"] = nullptr;
    return c;
}

double new_text_duration(const Timeline& tl, double playhead) {
    double end = 0;
    for (const auto& c : tl.clips) {
        end = std::max(end, ui_clip_end(*c));
    }
    return std::max(kTextDefaultDuration, js_fixed3(end - playhead));
}

void change_text(TimelineClip& clip, std::string_view text) {
    clip.text = std::string(text);
    clip.name = slice_utf16(text.empty() ? "Texto" : std::string(text), 22);
}

void upsert_style_kf(TimelineClip& clip, double local_t, const Json& patch, double fps, IdSource& ids) {
    Json vals = Json::object();
    for (const auto key : kTextStyleKfKeys) {
        if (kf_value_valid(key, field(patch, key))) {
            vals[std::string(key)] = field(patch, key);
        }
    }
    if (vals.empty()) {
        return;
    }
    const double t = js_fixed(snap_to_frame(local_t, fps), 6);
    std::vector<Json> items = normalize_kf_items(clip.keyframes ? field(*clip.keyframes, "items") : Json(), ids);
    const auto hit = std::ranges::find_if(
        items, [&](const Json& k) { return std::abs(k.at("t").get<double>() - t) < frame_duration(fps) / 2; });
    if (hit != items.end()) {
        for (const auto& [key, value] : vals.items()) {
            (*hit)["props"][key] = value;
        }
    } else {
        const auto before = std::ranges::find_if(items.rbegin(), items.rend(),
                                                 [&](const Json& k) { return k.at("t").get<double>() < t; });
        const Json* prev = before != items.rend() ? &*before : items.empty() ? nullptr : &items.front();
        items.push_back({{"id", make_uid(ids, "k")},
                         {"t", t},
                         {"interpolation", interp_name(normalize_interp(prev ? prev->at("interpolation") : Json("linear")))},
                         {"props", vals}});
    }
    std::ranges::stable_sort(items, {}, [](const Json& k) { return k.at("t").get<double>(); });
    Json kf = clip.keyframes && clip.keyframes->is_object() ? *clip.keyframes : Json::object();
    kf["enabled"] = true;
    kf["items"] = items;
    clip.keyframes = std::move(kf);
}

void change_text_style(TimelineClip& clip, const Json& patch, double local_t, double fps, bool fixed, IdSource& ids) {
    if (clip.kind != "text" || !patch.is_object()) {
        return;
    }
    const std::vector<std::string> animated = fixed ? std::vector<std::string>{} : text_style_kf_keys(clip);
    Json style = clip.style && clip.style->is_object() ? *clip.style : Json::object();
    for (const auto& [key, value] : patch.items()) {
        style[key] = value;
    }
    clip.style = std::move(style);
    const double t = std::max(0.0, local_t);
    const Json pose = flatten_pose_patch(patch);
    if (!pose.empty() && should_keyframe(clip, pose)) {
        upsert_keyframe_ui(clip, t, pose, std::nullopt, fps, ids);
    }
    Json kf_style = Json::object();
    for (const auto& [key, value] : patch.items()) {
        if (std::ranges::find(animated, key) != animated.end()) {
            kf_style[key] = value;
        }
    }
    if (!kf_style.empty()) {
        upsert_style_kf(clip, t, kf_style, fps, ids);
    }
}

std::string style_kf_state(const TimelineClip& clip, double local_t, const std::vector<std::string>& keys,
                           double fps) {
    if (!keyframes_enabled(clip)) {
        return "off";
    }
    const auto animated = text_style_kf_keys(clip);
    if (std::ranges::none_of(keys, [&](const std::string& k) { return std::ranges::find(animated, k) != animated.end(); })) {
        return "off";
    }
    SequentialIdSource ids;
    const double t = js_fixed(snap_to_frame(local_t, fps), 6);
    const auto items = normalize_kf_items(field(*clip.keyframes, "items"), ids);
    const auto it = std::ranges::find_if(
        items, [&](const Json& k) { return std::abs(k.at("t").get<double>() - t) < frame_duration(fps) / 2; });
    const bool on = it != items.end() && std::ranges::any_of(keys, [&](const std::string& k) {
                        return kf_value_valid(k, field(it->at("props"), k));
                    });
    return on ? "on" : "empty";
}

void remove_style_kf_at(TimelineClip& clip, double local_t, const std::vector<std::string>& keys, double fps,
                        IdSource& ids) {
    const double t = js_fixed(snap_to_frame(local_t, fps), 6);
    Json items = Json::array();
    for (Json k : normalize_kf_items(clip.keyframes ? field(*clip.keyframes, "items") : Json(), ids)) {
        if (std::abs(k.at("t").get<double>() - t) >= frame_duration(fps) / 2) {
            items.push_back(std::move(k));
            continue;
        }
        for (const auto& key : keys) {
            k["props"].erase(key);
        }
        if (!k["props"].empty()) {
            items.push_back(std::move(k));
        }
    }
    Json kf = clip.keyframes && clip.keyframes->is_object() ? *clip.keyframes : Json::object();
    kf["enabled"] = !items.empty();
    kf["items"] = std::move(items);
    clip.keyframes = std::move(kf);
}

void toggle_style_kf(TimelineClip& clip, const Json& track_style, double local_t, const std::vector<std::string>& keys,
                     double fps, IdSource& ids) {
    if (clip.kind != "text") {
        return;
    }
    const double t = std::clamp(std::max(0.0, local_t), 0.0, ui_clip_duration(clip));
    if (style_kf_state(clip, t, keys, fps) == "on") {
        remove_style_kf_at(clip, t, keys, fps, ids);
        return;
    }
    static const Json kDefaults = {{"color", "#ffffff"},      {"border_color", "#000000"}, {"border_width", 0},
                                   {"bg_opacity", 0.55},      {"shadow_color", "#000000"}, {"shadow_opacity", 0.6},
                                   {"shadow_blur", 0.05},     {"shadow_distance", 0.06},   {"shadow_angle", 45}};
    const Json st = with_text_style_kf(effective_text_style(track_style, clip.style.value_or(Json())),
                                       text_style_at(clip, t));
    Json patch = Json::object();
    for (const auto& k : keys) {
        const Json& v = field(st, k);
        patch[k] = !v.is_null() ? v : field(kDefaults, k);
    }
    upsert_style_kf(clip, t, patch, fps, ids);
}

Json normalize_favorites(const Json& raw) {
    Json out = {{"sfx", Json::array()}, {"audios", Json::array()}, {"textStyles", Json::array()}};
    if (!raw.is_object()) {
        return out;
    }
    const auto list = [&](const char* key) { return field(raw, key).is_array() ? field(raw, key) : Json::array(); };
    for (const char* key : {"sfx", "audios"}) {
        for (const Json& v : list(key)) {
            out[key].push_back(js_string(v));
        }
    }
    for (const Json& v : list("textStyles")) {
        if (v.is_object() && truthy(field(v, "style"))) {
            out["textStyles"].push_back(v);
        }
    }
    return out;
}

std::string name_text_style_favorite(const Json& style, const Json& existing) {
    const Json& theme = truthy(field(style, "theme")) ? field(style, "theme") : field(style, "preset");
    const Json& size = field(style, "size");
    const double px = js_round_half_up((size.is_null() ? 0.05 : js_number(size)) * 1280);
    const std::string base = (truthy(theme) ? js_string(theme) : "Estilo") + " · " + js_string(Json(px)) + "px";
    std::vector<std::string> names;
    for (const Json& f : existing.is_array() ? existing : Json::array()) {
        names.push_back(js_string(field(f, "name")));
    }
    const auto taken = [&](const std::string& n) { return std::ranges::find(names, n) != names.end(); };
    if (!taken(base)) {
        return base;
    }
    int i = 2;
    while (taken(base + " (" + std::to_string(i) + ")")) {
        ++i;
    }
    return base + " (" + std::to_string(i) + ")";
}

Json make_text_style_favorite(const Json& style, const Json& existing, IdSource& ids) {
    return {{"id", make_uid(ids, "tf")},
            {"name", name_text_style_favorite(style, existing)},
            {"style", style.is_object() ? style : Json::object()}};
}

std::vector<Json> split_clip_by_max_words(const TimelineClip& clip, const Json& max_words, IdSource& ids) {
    const std::string text = clip.text.value_or("");
    const auto chunks = chunk_caption_text(text, max_words);
    if (chunks.size() <= 1) {
        return {to_json(clip)};
    }
    const std::size_t total = std::max<std::size_t>(1, split_caption_words(text).size());
    const double dur = ui_clip_duration(clip);
    const double base = clip.start;
    const bool has_words = clip.words.size() == split_caption_words(text).size() && !clip.words.empty();
    const Json origin = clip.origin ? *clip.origin : Json();
    const Json& range = origin.is_object() ? origin.value("word_range", Json()) : Json();
    const double origin_start = range.is_array() && !range.empty() && range[0].is_number() ? range[0].get<double>() : 0;
    const Json source = to_json(clip);
    std::vector<Json> out;
    std::size_t acc = 0;
    for (std::size_t i = 0; i < chunks.size(); ++i) {
        const std::size_t n = split_caption_words(chunks[i]).size();
        const std::size_t from = acc;
        const std::size_t to = acc + n;
        acc = to;
        double start = 0;
        double d = 0;
        Json words = Json::array();
        if (has_words) {
            const std::size_t last = std::min(to, clip.words.size());
            const double w0 = from < last ? clip.words[from].start : dur * (static_cast<double>(from) / total);
            const double wn = from < last ? clip.words[last - 1].end : dur * (static_cast<double>(to) / total);
            start = base + w0;
            d = std::max(0.05, wn - w0);
            for (std::size_t k = from; k < last; ++k) {
                const Word& w = clip.words[k];
                Json word = {{"text", w.text},
                             {"start", js_fixed3(std::max(0.0, w.start - w0))},
                             {"end", js_fixed3(std::max(0.0, w.end - w0))}};
                if (w.prob) {
                    word["prob"] = *w.prob;
                }
                words.push_back(std::move(word));
            }
        } else {
            start = base + dur * (static_cast<double>(from) / total);
            const double end = i == chunks.size() - 1 ? base + dur : base + dur * (static_cast<double>(to) / total);
            d = std::max(0.05, end - start);
        }
        Json frag = source;
        frag["id"] = make_uid(ids, "c");
        frag["asset_id"] = make_uid(ids, "t");
        frag["text"] = chunks[i];
        frag["name"] = slice_utf16(chunks[i], 22);
        frag["start"] = js_fixed3(start);
        frag["in_point"] = 0;
        frag["out_point"] = js_fixed3(d);
        frag["source_duration"] = js_fixed3(d);
        frag["style"] = clip.style && clip.style->is_object() ? *clip.style : Json::object();
        frag["words"] = std::move(words);
        if (origin.is_object()) {
            Json o = origin;
            o["fragment_index"] = i;
            o["word_range"] = Json::array({origin_start + static_cast<double>(from), origin_start + static_cast<double>(to)});
            frag["origin"] = std::move(o);
        }
        out.push_back(std::move(frag));
    }
    return out;
}

namespace {

Result<void> append_parts(ClipList& out, const std::vector<Json>& parts) {
    for (const Json& p : parts) {
        auto c = clip_from_json(p);
        if (!c) {
            return std::unexpected(c.error());
        }
        out.emplace_back(std::move(*c));
    }
    return {};
}

} // namespace

Result<ClipList> split_one_text_clip(const ClipList& clips, std::string_view clip_id, const Json& max_words,
                                     IdSource& ids) {
    ClipList out;
    for (const auto& c : clips) {
        if (c->id == clip_id && c->kind == "text") {
            const auto parts = split_clip_by_max_words(*c, max_words, ids);
            if (parts.size() > 1) {
                if (auto r = append_parts(out, parts); !r) {
                    return std::unexpected(r.error());
                }
                continue;
            }
        }
        out.push_back(c);
    }
    return out;
}

Result<ClipList> split_track_text(const ClipList& clips, std::string_view track_id, const Json& max_words,
                                  IdSource& ids) {
    ClipList out;
    for (const auto& c : clips) {
        if (c->kind == "text" && c->track_id == track_id && resolve_text_role(to_json(*c)) != "free") {
            const auto parts = split_clip_by_max_words(*c, max_words, ids);
            if (parts.size() > 1) {
                if (auto r = append_parts(out, parts); !r) {
                    return std::unexpected(r.error());
                }
                continue;
            }
        }
        out.push_back(c);
    }
    return out;
}

} // namespace gc::core
