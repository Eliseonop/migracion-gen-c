#include "gc/core/serialize.hpp"

#include <concepts>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "gc/core/migrations.hpp"
#include "json_reader.hpp"

// Cada modelo describe sus campos UNA vez, en `fields(v, s)`, en el mismo orden
// que su clase Pydantic. El mismo `fields` sirve para leer (v = ObjectReader) y
// para escribir (v = ObjectWriter), así lectura y escritura no se desincronizan.

namespace gc::core::detail {

namespace {

// --- Utilidades de tipos --------------------------------------------------------------

template <class S, class M>
concept Is = std::same_as<std::remove_const_t<S>, M>;

// Base con la misma constancia que S (para delegar en los campos heredados).
template <class S, class B>
using Like = std::conditional_t<std::is_const_v<S>, const B, B>;

template <class S>
concept Model = std::is_class_v<S> && !std::same_as<S, Json> && requires(S s) {
    { s.extra } -> std::same_as<Json&>;
};

struct Range {
    double lo;
    double hi;
};

enum class Shape {
    Object, // `dict` de Pydantic
    Array,  // `list` de Pydantic
};

// Pydantic: "Input should be greater than or equal to 0.1" → mismo criterio (inclusivo).
void check_range(double v, Range r, const PathNode& path) {
    if (!(v >= r.lo && v <= r.hi)) {
        fail_at(path, "fuera de rango [" + Json(r.lo).dump() + ", " + Json(r.hi).dump() + "]: " + Json(v).dump());
    }
}

void check_shape(const Json& j, Shape s, const PathNode& path) {
    if (s == Shape::Object && !j.is_object()) {
        fail_at(path, std::string("se esperaba un objeto y llegó ") + json_type_name(j));
    }
    if (s == Shape::Array && !j.is_array()) {
        fail_at(path, std::string("se esperaba una lista y llegó ") + json_type_name(j));
    }
}

// --- Lectura de valores ----------------------------------------------------------------

template <Model S>
void read_value(const Json& j, S& out, const PathNode& path);
template <class T>
void read_value(const Json& j, std::optional<T>& out, const PathNode& path);
template <class T>
void read_value(const Json& j, std::vector<T>& out, const PathNode& path);
template <class T>
void read_value(const Json& j, Cow<T>& out, const PathNode& path);

void read_value(const Json& j, double& out, const PathNode& path) { out = to_float(j, path); }
void read_value(const Json& j, std::int64_t& out, const PathNode& path) { out = to_int(j, path); }
void read_value(const Json& j, bool& out, const PathNode& path) { out = to_bool(j, path); }
void read_value(const Json& j, std::string& out, const PathNode& path) { out = to_str(j, path); }

template <class T>
void read_value(const Json& j, std::optional<T>& out, const PathNode& path) {
    if (j.is_null()) {
        out.reset();
        return;
    }
    T value{};
    read_value(j, value, path);
    out = std::move(value);
}

template <class T>
void read_value(const Json& j, Cow<T>& out, const PathNode& path) {
    T value{};
    read_value(j, value, path);
    out = Cow<T>(std::move(value));
}

template <class T>
void read_value(const Json& j, std::vector<T>& out, const PathNode& path) {
    check_shape(j, Shape::Array, path);
    out.clear();
    out.reserve(j.size());
    for (std::size_t i = 0; i < j.size(); ++i) {
        T value{};
        read_value(j[i], value, path.at(i));
        out.push_back(std::move(value));
    }
}

// Lee los campos de un objeto JSON y apunta cuáles se han usado; lo que sobra
// acaba en `extra`.
class ObjectReader {
public:
    ObjectReader(const Json& obj, const PathNode& path) : obj_(obj), path_(path) {}

    template <class T>
    void req(std::string_view key, T& out) {
        read_value(require(key), out, path_.child(key));
    }
    void req(std::string_view key, double& out, Range r) {
        const PathNode p = path_.child(key);
        read_value(require(key), out, p);
        check_range(out, r, p);
    }

    template <class T>
    void opt(std::string_view key, T& out) {
        if (const Json* v = take(key)) {
            read_value(*v, out, path_.child(key));
        }
    }
    void opt(std::string_view key, double& out, Range r) {
        if (const Json* v = take(key)) {
            const PathNode p = path_.child(key);
            read_value(*v, out, p);
            check_range(out, r, p);
        }
    }
    void opt(std::string_view key, std::optional<double>& out, Range r) {
        if (const Json* v = take(key)) {
            const PathNode p = path_.child(key);
            read_value(*v, out, p);
            if (out) {
                check_range(*out, r, p);
            }
        }
    }

    // Valores sin tipar (`dict`, `list`, `Optional[dict]`, `list[dict]`).
    void opt(std::string_view key, Json& out, Shape s) {
        if (const Json* v = take(key)) {
            check_shape(*v, s, path_.child(key));
            out = *v;
        }
    }
    void opt(std::string_view key, std::optional<Json>& out, Shape s) {
        if (const Json* v = take(key)) {
            if (v->is_null()) {
                out.reset();
                return;
            }
            check_shape(*v, s, path_.child(key));
            out = *v;
        }
    }
    void opt(std::string_view key, std::vector<Json>& out, Shape item) {
        if (const Json* v = take(key)) {
            const PathNode p = path_.child(key);
            check_shape(*v, Shape::Array, p);
            out.clear();
            out.reserve(v->size());
            for (std::size_t i = 0; i < v->size(); ++i) {
                check_shape((*v)[i], item, p.at(i));
                out.push_back((*v)[i]);
            }
        }
    }

    // Claves que ningún campo ha consumido, en su orden original.
    [[nodiscard]] Json leftovers() const {
        Json extra = Json::object();
        for (const auto& [key, value] : obj_.items()) {
            if (!used(key)) {
                extra[key] = value;
            }
        }
        return extra;
    }

private:
    const Json* take(std::string_view key) {
        const auto it = obj_.find(key);
        if (it == obj_.end()) {
            return nullptr;
        }
        used_.push_back(key);
        return &*it;
    }
    const Json& require(std::string_view key) {
        const Json* v = take(key);
        if (v == nullptr) {
            fail_at(path_.child(key), "campo obligatorio");
        }
        return *v;
    }
    [[nodiscard]] bool used(std::string_view key) const {
        for (const auto k : used_) {
            if (k == key) {
                return true;
            }
        }
        return false;
    }

    const Json& obj_;
    const PathNode& path_;
    std::vector<std::string_view> used_;
};

// --- Escritura de valores ---------------------------------------------------------------

template <Model S>
Json write_value(const S& s, DumpMode mode);

inline Json write_value(double v, DumpMode) { return Json(v); }
inline Json write_value(std::int64_t v, DumpMode) { return Json(v); }
inline Json write_value(bool v, DumpMode) { return Json(v); }
inline Json write_value(const std::string& v, DumpMode) { return Json(v); }
inline Json write_value(const Json& v, DumpMode) { return v; }

template <class T>
Json write_value(const std::optional<T>& v, DumpMode mode) {
    return v ? write_value(*v, mode) : Json(nullptr);
}

template <class T>
Json write_value(const Cow<T>& v, DumpMode mode) {
    return write_value(*v, mode);
}

template <class T>
Json write_value(const std::vector<T>& v, DumpMode mode) {
    Json out = Json::array();
    for (const auto& item : v) {
        out.push_back(write_value(item, mode));
    }
    return out;
}

// Escribe todos los campos (como model_dump: también los opcionales vacíos, a null).
class ObjectWriter {
public:
    explicit ObjectWriter(DumpMode mode) : mode_(mode) {}

    template <class T>
    void req(std::string_view key, const T& v) {
        put(key, write_value(v, mode_));
    }
    template <class T>
    void opt(std::string_view key, const T& v) {
        put(key, write_value(v, mode_));
    }
    template <class T>
    void req(std::string_view key, const T& v, Range) {
        req(key, v);
    }
    template <class T>
    void opt(std::string_view key, const T& v, Range) {
        opt(key, v);
    }
    template <class T>
    void opt(std::string_view key, const T& v, Shape) {
        opt(key, v);
    }

    [[nodiscard]] Json finish(const Json& extra) && {
        if (mode_ == DumpMode::Preserve && extra.is_object()) {
            for (const auto& [key, value] : extra.items()) {
                if (!out_.contains(key)) {
                    out_[key] = value;
                }
            }
        }
        return std::move(out_);
    }

private:
    void put(std::string_view key, Json value) { out_[std::string(key)] = std::move(value); }

    DumpMode mode_;
    Json out_ = Json::object();
};

// --- Campos de cada modelo (orden = el de backend/app/schemas.py) ---------------------------

constexpr Range kUnit{0.0, 1.0};
constexpr Range kZoom{0.1, 1.0};
constexpr Range kCrop{0.05, 1.0};

template <class V, class S>
    requires Is<S, Keyframe>
void fields(V& v, S& s) {
    v.req("t", s.t);
    v.req("cx", s.cx, kUnit);
    v.opt("cy", s.cy, kUnit);
    v.opt("zoom", s.zoom, kZoom);
    v.opt("pan_mode", s.pan_mode);
    v.opt("fit", s.fit);
}

template <class V, class S>
    requires Is<S, TrackPoint>
void fields(V& v, S& s) {
    fields(v, static_cast<Like<S, Keyframe>&>(s));
    v.opt("w", s.w);
    v.opt("h", s.h);
}

template <class V, class S>
    requires Is<S, Reframe>
void fields(V& v, S& s) {
    v.opt("zoom", s.zoom, kZoom);
    v.opt("keyframes", s.keyframes);
    v.opt("pan_mode", s.pan_mode);
    v.opt("dual_crop", s.dual_crop);
    v.opt("split_orientation", s.split_orientation);
    v.opt("zoom2", s.zoom2);
    v.opt("keyframes2", s.keyframes2);
    v.opt("crop_w", s.crop_w, kCrop);
    v.opt("crop_h", s.crop_h, kCrop);
    v.opt("master", s.master);
    v.opt("split_layout", s.split_layout);
    v.opt("face_track_mode", s.face_track_mode);
}

template <class V, class S>
    requires Is<S, FaceTrack>
void fields(V& v, S& s) {
    v.req("start", s.start);
    v.req("end", s.end);
    v.opt("width", s.width);
    v.opt("height", s.height);
    v.opt("fps", s.fps);
    v.opt("samples", s.samples);
    v.opt("track", s.track);
    v.opt("keyframes", s.keyframes);
    v.opt("created_at", s.created_at);
}

template <class V, class S>
    requires Is<S, Word>
void fields(V& v, S& s) {
    v.req("text", s.text);
    v.req("start", s.start);
    v.req("end", s.end);
    v.opt("prob", s.prob);
}

template <class V, class S>
    requires Is<S, TranscriptSegment>
void fields(V& v, S& s) {
    v.req("start", s.start);
    v.req("end", s.end);
    v.req("text", s.text);
    v.opt("words", s.words);
}

template <class V, class S>
    requires Is<S, Transcript>
void fields(V& v, S& s) {
    v.req("id", s.id);
    v.opt("source_url", s.source_url);
    v.opt("title", s.title);
    v.req("model", s.model);
    v.opt("language", s.language);
    v.opt("duration", s.duration);
    v.opt("created_at", s.created_at);
    v.opt("file", s.file);
    v.opt("segments", s.segments);
}

template <class V, class S>
    requires Is<S, ClipInfo>
void fields(V& v, S& s) {
    v.req("index", s.index);
    v.req("filename", s.filename);
    v.req("url", s.url);
    v.req("start", s.start);
    v.req("end", s.end);
    v.opt("id", s.id);
    v.opt("source_url", s.source_url);
    v.opt("created_at", s.created_at);
    v.opt("label", s.label);
    v.opt("description", s.description);
    v.opt("description_ai", s.description_ai);
    v.opt("semantic", s.semantic, Shape::Object);
    v.opt("reframe", s.reframe);
    v.opt("transcript", s.transcript);
    v.opt("origin", s.origin);
    v.opt("source", s.source);
    v.opt("provider", s.provider);
    v.opt("external_id", s.external_id);
    v.opt("author", s.author);
    v.opt("license_info", s.license_info);
    v.opt("in_point", s.in_point);
    v.opt("out_point", s.out_point);
    v.opt("parent_id", s.parent_id);
    v.opt("face_track", s.face_track);
}

template <class V, class S>
    requires Is<S, AudioInfo>
void fields(V& v, S& s) {
    v.req("id", s.id);
    v.req("filename", s.filename);
    v.req("url", s.url);
    v.opt("voice", s.voice);
    v.opt("voice2", s.voice2);
    v.opt("blend", s.blend);
    v.opt("speed", s.speed);
    v.opt("pause", s.pause);
    v.opt("text", s.text);
    v.opt("duration", s.duration);
    v.opt("created_at", s.created_at);
    v.opt("engine", s.engine);
    v.opt("label", s.label);
    v.opt("description", s.description);
    v.opt("origin", s.origin);
    v.opt("source", s.source);
    v.opt("youtube_url", s.youtube_url);
    v.opt("youtube_id", s.youtube_id);
}

template <class V, class S>
    requires Is<S, ImageInfo>
void fields(V& v, S& s) {
    v.req("id", s.id);
    v.req("filename", s.filename);
    v.req("url", s.url);
    v.opt("width", s.width);
    v.opt("height", s.height);
    v.opt("created_at", s.created_at);
    v.opt("label", s.label);
    v.opt("description", s.description);
    v.opt("description_ai", s.description_ai);
    v.opt("semantic", s.semantic, Shape::Object);
    v.opt("analysis", s.analysis, Shape::Object);
    v.opt("origin", s.origin);
    v.opt("source", s.source);
    v.opt("provider", s.provider);
    v.opt("external_id", s.external_id);
    v.opt("source_url", s.source_url);
    v.opt("author", s.author);
    v.opt("license_info", s.license_info);
    v.opt("animated", s.animated);
    v.opt("frames", s.frames);
    v.opt("fps", s.fps);
    v.opt("duration", s.duration);
    v.opt("loop", s.loop);
    v.opt("has_alpha", s.has_alpha);
}

template <class V, class S>
    requires Is<S, TimelineTrack>
void fields(V& v, S& s) {
    v.req("id", s.id);
    v.req("kind", s.kind);
    v.req("name", s.name);
    v.opt("hidden", s.hidden);
    v.opt("muted", s.muted);
    v.opt("locked", s.locked);
    v.opt("style", s.style, Shape::Object);
    v.opt("linked_track_id", s.linked_track_id);
    v.opt("magnetic", s.magnetic);
}

template <class V, class S>
    requires Is<S, TimelineClip>
void fields(V& v, S& s) {
    v.req("id", s.id);
    v.req("track_id", s.track_id);
    v.req("kind", s.kind);
    v.req("asset_kind", s.asset_kind);
    v.req("asset_id", s.asset_id);
    v.req("filename", s.filename);
    v.opt("name", s.name);
    v.opt("start", s.start);
    v.opt("in_point", s.in_point);
    v.opt("out_point", s.out_point);
    v.opt("source_duration", s.source_duration);
    v.opt("ref_segment", s.ref_segment);
    v.opt("volume", s.volume);
    v.opt("media_version", s.media_version);
    v.opt("reframe", s.reframe);
    v.opt("text", s.text);
    v.opt("style", s.style, Shape::Object);
    v.opt("layout", s.layout);
    v.opt("transform", s.transform, Shape::Object);
    v.opt("frame", s.frame);
    v.opt("appear", s.appear);
    v.opt("exit", s.exit);
    v.opt("look", s.look);
    v.opt("effects", s.effects, Shape::Object);
    v.opt("filters", s.filters, Shape::Array);
    v.opt("audio_fx", s.audio_fx, Shape::Object);
    v.opt("muted", s.muted);
    v.opt("speed", s.speed);
    v.opt("keep_pitch", s.keep_pitch);
    v.opt("reverse", s.reverse);
    v.opt("speed_curve", s.speed_curve, Shape::Object);
    v.opt("opacity", s.opacity);
    v.opt("flip_h", s.flip_h);
    v.opt("flip_v", s.flip_v);
    v.opt("blend_mode", s.blend_mode);
    v.opt("disabled", s.disabled);
    v.opt("beats", s.beats, Shape::Object);
    v.opt("loop", s.loop);
    v.opt("words", s.words);
    v.opt("origin", s.origin, Shape::Object);
    v.opt("text_role", s.text_role);
    v.opt("shape", s.shape, Shape::Object);
    v.opt("masks", s.masks, Shape::Object);
    v.opt("bg_removal", s.bg_removal, Shape::Object);
    v.opt("anim", s.anim, Shape::Object);
    v.opt("vfx", s.vfx, Shape::Array);
    v.opt("animations", s.animations, Shape::Object);
    v.opt("keyframes", s.keyframes, Shape::Object);
    v.opt("asset_scope", s.asset_scope);
    v.opt("description", s.description);
    v.opt("note", s.note);
    v.opt("note_source", s.note_source);
    v.opt("dup_of", s.dup_of);
    v.opt("composition_id", s.composition_id);
}

template <class V, class S>
    requires Is<S, Timeline>
void fields(V& v, S& s) {
    v.opt("version", s.version);
    v.opt("schema_version", s.schema_version);
    v.opt("fps", s.fps);
    v.opt("width", s.width);
    v.opt("height", s.height);
    v.opt("audio_target_db", s.audio_target_db);
    v.opt("tracks", s.tracks);
    v.opt("clips", s.clips);
    v.opt("markers", s.markers, Shape::Object);
}

template <class V, class S>
    requires Is<S, Project>
void fields(V& v, S& s) {
    v.req("id", s.id);
    v.req("name", s.name);
    v.req("created_at", s.created_at);
    v.opt("folder", s.folder);
    v.opt("group_id", s.group_id);
    v.opt("clips", s.clips);
    v.opt("transcripts", s.transcripts);
    v.opt("audios", s.audios);
    v.opt("images", s.images);
    v.opt("timeline", s.timeline);
    v.opt("motion_compositions", s.motion_compositions, Shape::Object);
    v.opt("scene_directions", s.scene_directions, Shape::Object);
    v.opt("visual_blueprint", s.visual_blueprint, Shape::Object);
}

template <class V, class S>
    requires Is<S, ProjectGroup>
void fields(V& v, S& s) {
    v.req("id", s.id);
    v.req("name", s.name);
    v.req("created_at", s.created_at);
}

template <class V, class S>
    requires Is<S, ProjectsFile>
void fields(V& v, S& s) {
    v.req("projects", s.projects);
    v.opt("groups", s.groups);
}

// --- Modelos completos ----------------------------------------------------------------------

template <Model S>
void read_value(const Json& j, S& out, const PathNode& path) {
    check_shape(j, Shape::Object, path);
    ObjectReader reader(j, path);
    fields(reader, out);
    out.extra = reader.leftovers();
}

template <Model S>
Json write_value(const S& s, DumpMode mode) {
    ObjectWriter writer(mode);
    fields(writer, s);
    return std::move(writer).finish(s.extra);
}

// Prepara una entrada cruda de projects.json igual que projects._project_from_dict:
// clips sin id → id = str(index); la timeline, migrada al schema actual.
Json prepare_project(const Json& raw) {
    Json p = raw;
    const auto clips_it = p.find("clips");
    Json clips = Json::array();
    if (clips_it != p.end() && py_truthy(*clips_it)) {
        clips = *clips_it;
        if (clips.is_array()) {
            for (Json& c : clips) {
                if (!c.is_object()) {
                    continue;
                }
                const auto id = c.find("id");
                const auto index = c.find("index");
                if ((id == c.end() || !py_truthy(*id)) && index != c.end() && !index->is_null()) {
                    c["id"] = py_str(*index);
                }
            }
        }
    }
    p["clips"] = std::move(clips);
    if (const auto tl = p.find("timeline"); tl != p.end() && py_truthy(*tl)) {
        *tl = migrate_timeline(*tl);
    }
    return p;
}

template <class S, class Prepare>
Result<S> parse_model(const Json& raw, Prepare&& prepare) {
    try {
        if (!raw.is_object()) {
            return fail(ErrorCode::Validation, std::string("se esperaba un objeto y llegó ") + json_type_name(raw));
        }
        const Json input = prepare(raw);
        S out{};
        const PathNode root{};
        read_value(input, out, root);
        return out;
    } catch (const ValidationFailure& e) {
        return fail(ErrorCode::Validation, e.message, e.path);
    } catch (const Json::exception& e) {
        return fail(ErrorCode::Internal, e.what());
    }
}

const auto kAsIs = [](const Json& raw) -> const Json& { return raw; };

} // namespace

} // namespace gc::core::detail

namespace gc::core {

Result<Project> project_from_json(const Json& raw) {
    return detail::parse_model<Project>(raw, detail::prepare_project);
}

Result<Timeline> timeline_from_json(const Json& raw) {
    return detail::parse_model<Timeline>(raw, [](const Json& tl) { return migrate_timeline(tl); });
}

Result<ProjectGroup> project_group_from_json(const Json& raw) {
    return detail::parse_model<ProjectGroup>(raw, detail::kAsIs);
}

Result<TimelineClip> clip_from_json(const Json& raw) {
    return detail::parse_model<TimelineClip>(raw, detail::kAsIs);
}

Result<TimelineTrack> track_from_json(const Json& raw) {
    return detail::parse_model<TimelineTrack>(raw, detail::kAsIs);
}

Result<Keyframe> keyframe_from_json(const Json& raw) { return detail::parse_model<Keyframe>(raw, detail::kAsIs); }

Json to_json(const TimelineClip& clip, DumpMode mode) { return detail::write_value(clip, mode); }
Json to_json(const TimelineTrack& track, DumpMode mode) { return detail::write_value(track, mode); }

Result<ProjectsFile> projects_file_from_json(const Json& raw) {
    // Cada proyecto pasa por la misma preparación que en list_projects().
    return detail::parse_model<ProjectsFile>(raw, [](const Json& file) {
        Json out = file;
        if (const auto projects = out.find("projects"); projects != out.end() && projects->is_array()) {
            for (Json& p : *projects) {
                if (p.is_object()) {
                    p = detail::prepare_project(p);
                }
            }
        }
        return out;
    });
}

Json to_json(const Project& project, DumpMode mode) { return detail::write_value(project, mode); }
Json to_json(const Timeline& timeline, DumpMode mode) { return detail::write_value(timeline, mode); }
Json to_json(const ProjectGroup& group, DumpMode mode) { return detail::write_value(group, mode); }
Json to_json(const ProjectsFile& file, DumpMode mode) { return detail::write_value(file, mode); }

} // namespace gc::core
