#include "gc/core/material_drop.hpp"

#include <algorithm>
#include <cmath>
#include <initializer_list>

#include "gc/core/shapes.hpp"
#include "gc/core/timeline_ui.hpp"

namespace gc::core {

namespace {

constexpr double kImageDefaultDuration = 5; // IMAGE_DEFAULT_DUR

// Una propiedad de JS: sin valor = undefined (no es lo mismo que null).
using Value = std::optional<Json>;

Value get(const Json& obj, std::string_view key) {
    if (!obj.is_object()) {
        return std::nullopt;
    }
    const auto it = obj.find(std::string(key));
    return it == obj.end() ? Value() : Value(*it);
}

bool truthy(const Value& v) {
    if (!v || v->is_null()) {
        return false;
    }
    if (v->is_boolean()) {
        return v->get<bool>();
    }
    if (v->is_number()) {
        const double n = v->get<double>();
        return n != 0 && !std::isnan(n);
    }
    if (v->is_string()) {
        return !v->get_ref<const std::string&>().empty();
    }
    return true;
}

// a || b || c de JS: el primero verdadero o el último.
Value either(std::initializer_list<Value> values) {
    Value last;
    for (const Value& v : values) {
        if (truthy(v)) {
            return v;
        }
        last = v;
    }
    return last;
}

// String(x) de JS.
std::string js_str(const Value& v) {
    if (!v) {
        return "undefined";
    }
    if (v->is_string()) {
        return v->get<std::string>();
    }
    if (v->is_boolean()) {
        return v->get<bool>() ? "true" : "false";
    }
    if (v->is_null()) {
        return "null";
    }
    return v->dump();
}

double number(const Value& v) {
    return v ? js_number(*v) : std::nan("");
}

// num() de clipExtract.js: un número finito (ni null ni texto vacío) o nada.
std::optional<double> finite(const Value& v) {
    if (!v || v->is_null() || *v == "") {
        return std::nullopt;
    }
    const double n = js_number(*v);
    return std::isfinite(n) ? std::optional(n) : std::nullopt;
}

void put(Json& obj, const char* key, const Value& v) {
    if (v) {
        obj[key] = *v;
    }
}

bool from_collection(const Json& item) {
    return get(item, "scope") == Value("collection") ||
           js_str(either({get(item, "id"), Json("")})).starts_with("col_");
}

bool from_library(const Json& item) {
    return from_collection(item) || get(item, "scope") == Value("library") ||
           js_str(either({get(item, "id"), Json("")})).starts_with("lib_");
}

Json new_reframe() {
    return {{"zoom", 1},
            {"pan_mode", "smooth"},
            {"dual_crop", false},
            {"split_orientation", "vertical"},
            {"split_layout", "auto"},
            {"master", false},
            {"face_track_mode", nullptr},
            {"keyframes", Json::array()},
            {"keyframes2", Json::array()}};
}

// withKfIds de editorModel.js sobre {...newReframe(), ...reframe}.
Json with_kf_ids(const Json& reframe, IdSource& ids) {
    Json out = new_reframe();
    if (reframe.is_object()) {
        for (const auto& [key, value] : reframe.items()) {
            out[key] = value;
        }
    }
    for (const char* key : {"keyframes", "keyframes2"}) {
        Json fixed = Json::array();
        const Value list = get(reframe, key);
        if (truthy(list) && list->is_array()) {
            for (Json k : *list) {
                if (!truthy(get(k, "id"))) {
                    k["id"] = make_uid(ids, "k");
                }
                fixed.push_back(std::move(k));
            }
        }
        out[key] = std::move(fixed);
    }
    return out;
}

std::size_t js_length(const Value& v) {
    if (v && v->is_array()) {
        return v->size();
    }
    return v && v->is_string() ? v->get_ref<const std::string&>().size() : 0;
}

} // namespace

std::optional<std::pair<double, double>> segment_range(const Json& item) {
    const auto a = finite(get(item, "in_point"));
    const auto b = finite(get(item, "out_point"));
    return a && b && *b > *a ? std::optional(std::pair{*a, *b}) : std::nullopt;
}

double material_duration(const Json& item) {
    if (const auto seg = segment_range(item)) {
        return seg->second - seg->first;
    }
    const Value end = get(item, "end");
    const Value start = get(item, "start");
    if (end && !end->is_null() && start && !start->is_null()) {
        return std::max(0.0, number(end) - number(start));
    }
    const double d = number(get(item, "duration"));
    return std::isnan(d) ? 0 : d;
}

bool has_face_track(const Json& item) {
    const Value reframe = get(item, "reframe");
    return truthy(get(item, "face_track")) && reframe && js_length(get(*reframe, "keyframes")) > 0;
}

std::string lane_kind_for_asset(std::string_view asset_kind) {
    if (asset_kind == "clips" || asset_kind == "video" || asset_kind == "images" || asset_kind == "image" ||
        asset_kind == "shape") {
        return "video";
    }
    return asset_kind == "text" ? "text" : "audio";
}

Json drag_payload(std::string_view asset_kind, const Json& item) {
    const bool collection = from_collection(item);
    const bool external = from_library(item);
    const bool image = asset_kind == "images";
    const bool gif = image && truthy(get(item, "animated"));
    Json out = Json::object();
    out["asset_kind"] = asset_kind;
    out["asset_id"] = js_str(!external && asset_kind == "clips" ? get(item, "index") : get(item, "id"));
    put(out, "filename", asset_kind == "sfx" ? get(item, "id") : get(item, "filename"));
    put(out, "name", either({get(item, "label"), get(item, "name"), get(item, "filename")}));
    put(out, "url", get(item, "url"));
    if (image) {
        const double d = number(get(item, "duration"));
        out["duration"] = gif && d > 0 ? d : kImageDefaultDuration;
    } else if (asset_kind == "clips") {
        out["duration"] = material_duration(item);
    } else {
        out["duration"] = *either({get(item, "duration"), Json(0)});
    }
    out["kind"] = image ? "image" : asset_kind == "clips" ? "video" : "audio";
    if (gif) {
        out["animated"] = true;
        out["loop"] = get(item, "loop") != Value(false);
    }
    out["reframe"] = *either({get(item, "reframe"), Json()});
    out["scope"] = collection ? "collection" : external ? "library" : "project";
    out["description"] = *either({get(item, "description"), get(item, "text"), Json()});
    out["media_version"] = *either({get(item, "created_at"), get(item, "media_version"), Json()});
    if (segment_range(item)) {
        put(out, "in_point", get(item, "in_point"));
        put(out, "out_point", get(item, "out_point"));
    }
    if (has_face_track(item)) {
        out["face_track"] = true;
    }
    if (truthy(get(item, "chromaColor"))) {
        out["chroma_color"] = *get(item, "chromaColor");
    }
    return out;
}

Json make_clip_json(std::string_view asset_kind, const Json& item, std::string_view track_id, double start,
                    const Json& duration, IdSource& ids) {
    const std::string kind = asset_kind == "clips" ? "video" : asset_kind == "images" ? "image" : "audio";
    const bool collection = from_collection(item);
    const bool library = from_library(item);
    const bool visual = kind == "video" || kind == "image";
    const Value reframe = get(item, "reframe");
    const bool master =
        kind == "video" && reframe && reframe->is_object() && get(*reframe, "master") == Value(true);
    const bool gif = kind == "image" && truthy(get(item, "animated"));
    const double gif_dur = gif && number(get(item, "duration")) > 0 ? number(get(item, "duration")) : 0;
    const Value dur = truthy(duration)
                          ? Value(duration)
                          : Value(Json(kind == "image" ? (gif_dur ? gif_dur : kImageDefaultDuration) : 1));
    const double span = js_fixed3(std::max(0.3, number(dur)));
    const Value in = get(item, "in_point");
    const Value out = get(item, "out_point");
    const bool seg =
        kind == "video" && in && !in->is_null() && out && !out->is_null() && number(out) > number(in);
    const double seg_in = seg ? js_fixed3(number(in)) : 0;
    const bool tracked = kind == "video" && truthy(get(item, "face_track")) && reframe &&
                         js_length(get(*reframe, "keyframes")) > 0;

    Json c = Json::object();
    c["id"] = make_uid(ids, "c");
    c["track_id"] = track_id;
    c["kind"] = kind;
    c["asset_kind"] = asset_kind;
    c["asset_id"] = js_str(!library && asset_kind == "clips" ? get(item, "index") : get(item, "id"));
    c["asset_scope"] = collection ? "collection" : library ? "library" : "project";
    put(c, "filename", asset_kind == "sfx" ? get(item, "id") : get(item, "filename"));
    put(c, "name", either({get(item, "label"), get(item, "name"), get(item, "filename")}));
    c["start"] = js_fixed3(std::max(0.0, start));
    c["in_point"] = seg_in;
    c["out_point"] = js_fixed3(seg_in + span);
    c["source_duration"] = js_fixed3(seg_in + span);
    if (seg) {
        c["ref_segment"] = true;
    }
    c["volume"] = 1;
    c["muted"] = false;
    c["speed"] = 1;
    c["keep_pitch"] = true;
    c["reverse"] = false;
    c["speed_curve"] = nullptr;
    c["media_version"] = *either({get(item, "created_at"), get(item, "media_version"), Json()});
    if (!visual) {
        c["reframe"] = nullptr;
    } else if (master || tracked) {
        Json merged = new_reframe();
        if (reframe && reframe->is_object()) {
            for (const auto& [key, value] : reframe->items()) {
                merged[key] = value;
            }
        }
        c["reframe"] = with_kf_ids(merged, ids);
    } else {
        c["reframe"] = new_reframe();
    }
    if (visual) {
        c["layout"] = "fill";
        c["frame"] = "full";
    }
    if (gif) {
        c["loop"] = get(item, "loop") != Value(false);
    }
    c["appear"] = "none";
    c["exit"] = "none";
    c["look"] = "none";
    c["effects"] = Json::object();
    c["audio_fx"] = Json::object();
    c["description"] = *either({get(item, "description"), get(item, "text"), Json()});
    c["dup_of"] = nullptr;
    return c;
}

namespace {

Json generated_clip(std::string_view kind, std::string_view track_id, double start, double span, IdSource& ids) {
    Json c = Json::object();
    c["id"] = make_uid(ids, "c");
    c["track_id"] = track_id;
    c["kind"] = kind;
    c["asset_kind"] = kind;
    c["asset_id"] = kind;
    c["filename"] = "";
    c["name"] = "";
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
    c["appear"] = "none";
    c["exit"] = "none";
    c["look"] = "none";
    c["effects"] = Json::object();
    return c;
}

} // namespace

Json make_shape_clip_json(std::string_view track_id, double start, double duration, const Json& preset, IdSource& ids) {
    const Value nested = get(preset, "shape");
    const Value type = either({get(preset, "type"), nested ? get(*nested, "type") : Value(), get(preset, "shape_type"),
                               Json("rect")});
    Json shape = default_shape(*type);
    if (truthy(nested) && nested->is_object()) {
        for (const auto& [key, value] : nested->items()) {
            shape[key] = value;
        }
    }
    Json c = generated_clip("shape", track_id, start, js_fixed3(std::max(0.3, duration != 0 && !std::isnan(duration) ? duration : kShapeDefaultDuration)), ids);
    c["asset_id"] = *type;
    c["name"] = *either({get(preset, "label"), get(preset, "name"), get(shape, "type")});
    c["audio_fx"] = Json::object();
    c["shape"] = shape;
    c["description"] = nullptr;
    c["dup_of"] = nullptr;
    return c;
}

Json make_adjustment_clip_json(std::string_view track_id, double start, double duration, IdSource& ids) {
    Json c = generated_clip("adjustment", track_id, start, js_fixed3(std::max(0.3, duration != 0 && !std::isnan(duration) ? duration : 5)), ids);
    c["name"] = "Capa de ajuste";
    c["filters"] = Json::array();
    c["opacity"] = 1;
    c["audio_fx"] = Json::object();
    c["description"] = nullptr;
    c["dup_of"] = nullptr;
    return c;
}

Json place_asset_clip(const Json& payload, std::string_view track_id, double start,
                      std::optional<double> fit_duration, IdSource& ids) {
    const std::string asset_kind = js_str(get(payload, "asset_kind"));
    if (asset_kind == "shape" || js_str(get(payload, "kind")) == "shape") { // fit || duration || SHAPE_DEFAULT_DUR
        const double dur = fit_duration && *fit_duration > 0 ? *fit_duration
                           : truthy(get(payload, "duration"))                                ? number(get(payload, "duration"))
                                                                                            : kShapeDefaultDuration;
        return make_shape_clip_json(track_id, start, dur, payload, ids);
    }
    Json item = Json::object();
    const auto copy = [&](const char* to, const char* from) { put(item, to, get(payload, from)); };
    copy("index", "asset_id");
    copy("id", "asset_id");
    copy("filename", "filename");
    copy("name", "name");
    copy("label", "name");
    copy("duration", "duration");
    copy("end", "duration");
    item["start"] = 0;
    for (const char* key : {"reframe", "scope", "description", "media_version", "animated", "loop",
                            "in_point", "out_point", "face_track"}) {
        copy(key, key);
    }
    Json clip =
        make_clip_json(asset_kind, item, track_id, start, get(payload, "duration").value_or(Json()), ids);
    const double fit = fit_duration && *fit_duration > 0 ? *fit_duration : 0;
    const bool free = clip["kind"] == "image";
    const double in = clip["in_point"].get<double>();
    const double dur = clip["out_point"].get<double>() - in; // velocidad 1
    if (fit > 0 && (free || dur > fit)) {
        clip["out_point"] = js_fixed3(in + fit);
    }
    return clip;
}

} // namespace gc::core
