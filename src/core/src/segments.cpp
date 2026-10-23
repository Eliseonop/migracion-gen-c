#include "gc/core/segments.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <set>

#include "gc/core/serialize.hpp"
#include "json_reader.hpp"
#include "py.hpp"

namespace gc::core {

using detail::py_float;
using detail::py_iter;
using detail::py_path_stem;
using detail::py_round;
using detail::py_str;
using detail::py_strip;
using detail::py_truthy;

namespace {

constexpr double kCacheEps = 0.05; // s: tolerancia para reutilizar un análisis cacheado
constexpr double kDeadzone = 0.025; // desplazamiento normalizado que NO mueve la cámara

const Json& field(const Json& obj, std::string_view key) {
    static const Json kNull;
    const auto it = obj.find(key);
    return it == obj.end() ? kNull : *it;
}

// str(x or "").strip()
std::string stripped_text(const Json& v) { return py_truthy(v) ? py_strip(py_str(v)) : std::string(); }

// storage.resolve_media(project, "video", filename), que tiene que existir.
Result<std::filesystem::path> media_path(const ProjectsStore& store, const Project& project, const ClipInfo& clip) {
    const auto path = resolve_project_media(store.clips_dir(), project, "video", clip.filename);
    std::error_code ec;
    if (!path || !std::filesystem::exists(*path, ec)) {
        return fail(ErrorCode::InvalidArgument, "El archivo del vídeo no existe en el proyecto.");
    }
    return *path;
}

Result<Project> project_of(const ProjectsStore& store, std::string_view pid) {
    auto proj = store.get_project(pid);
    if (!proj) {
        return std::unexpected(proj.error());
    }
    if (!*proj) {
        return fail(ErrorCode::NotFound, "Proyecto no encontrado.");
    }
    return std::move(**proj);
}

// statistics.median
double median(std::vector<double> v) {
    std::ranges::sort(v);
    const std::size_t n = v.size();
    return n % 2 == 1 ? v[n / 2] : (v[(n / 2) - 1] + v[n / 2]) / 2;
}

} // namespace

const ClipInfo* find_material_clip(const Project& project, std::string_view ident) {
    for (const ClipInfo& c : project.clips) {
        if (std::to_string(c.index) == ident || (c.id && !c.id->empty() && *c.id == ident)) {
            return &c;
        }
    }
    return nullptr;
}

std::pair<double, double> material_file_range(const ClipInfo& clip) {
    if (clip.in_point && clip.out_point) {
        return {*clip.in_point, *clip.out_point};
    }
    const double dur = clip.end - clip.start;
    return {0.0, dur > 0.0 ? dur : 0.0}; // max(0.0, …)
}

Result<Json> normalize_segment_ranges(const Json& raw, double file_dur) {
    std::vector<Json> items;
    if (py_truthy(raw)) { // raw or []
        auto it = py_iter(raw);
        if (!it) {
            return fail(ErrorCode::InvalidArgument, "los segmentos no son una lista");
        }
        items = std::move(*it);
    }
    Json out = Json::array();
    for (std::size_t i = 0; i < items.size(); ++i) {
        const Json& seg = items[i];
        if (!seg.is_object()) {
            return fail(ErrorCode::InvalidArgument, "un segmento no es un objeto"); // AttributeError
        }
        const auto start = py_float(field(seg, "start"));
        const auto end = start ? py_float(field(seg, "end")) : std::nullopt;
        if (!start || !end) {
            return fail(ErrorCode::InvalidArgument, std::format("Segmento {}: start/end no válidos.", i + 1));
        }
        double a = *start;
        double b = *end;
        if (b < a) {
            std::swap(a, b);
        }
        a = a > 0.0 ? a : 0.0; // max(0.0, a)
        if (file_dur > 0) {
            b = file_dur < b ? file_dur : b; // min(b, file_dur)
        }
        if (b - a < kMinSegment) {
            return fail(ErrorCode::InvalidArgument, std::format("Segmento {}: demasiado corto (mínimo 0.3s).", i + 1));
        }
        Json s = seg;
        s["start"] = py_round(a, 3);
        s["end"] = py_round(b, 3);
        out.push_back(std::move(s));
    }
    if (out.empty()) {
        return fail(ErrorCode::InvalidArgument, "No hay segmentos.");
    }
    return out;
}

Result<std::string> segment_mmss(double seconds) {
    if (!std::isfinite(seconds)) {
        return fail(ErrorCode::InvalidArgument, "no se puede pasar a entero un tiempo infinito o NaN");
    }
    const auto r = static_cast<std::int64_t>(py_round(seconds, 0));
    const std::int64_t s = std::max<std::int64_t>(0, r);
    return std::format("{}:{:02d}", s / 60, s % 60);
}

Result<std::vector<ClipInfo>> create_segments(ProjectsStore& store, std::string_view pid,
                                              std::string_view source_ident, const Json& raw,
                                              const DurationProbe& probe, IdSource& ids) {
    auto proj = project_of(store, pid);
    if (!proj) {
        return std::unexpected(proj.error());
    }
    const ClipInfo* src = find_material_clip(*proj, source_ident);
    if (src == nullptr) {
        return fail(ErrorCode::NotFound, "Material de vídeo no encontrado.");
    }
    auto path = media_path(store, *proj, *src);
    if (!path) {
        return std::unexpected(path.error());
    }
    double file_dur = 0.0; // _file_duration(path, 0.0): la sonda, si da más de 0
    if (probe) {
        if (const auto d = probe(*path); d && *d > 0) {
            file_dur = *d;
        }
    }
    auto ranges = normalize_segment_ranges(raw, file_dur);
    if (!ranges) {
        return std::unexpected(ranges.error());
    }

    const std::string parent_id = src->parent_id && !src->parent_id->empty() ? *src->parent_id
                                  : src->id && !src->id->empty()             ? *src->id
                                                                             : std::to_string(src->index);
    const std::string stem = py_path_stem(src->filename);
    const std::string base_label =
        py_strip(src->label && !src->label->empty() ? *src->label : !stem.empty() ? stem : std::string("Clip"));
    std::int64_t index = 99999; // _next_index: max(índices, 99999) + 1
    for (const ClipInfo& c : proj->clips) {
        index = std::max(index, c.index);
    }
    ++index;

    std::vector<ClipInfo> created;
    std::set<std::string> created_ids;
    for (std::size_t n = 0; n < ranges->size(); ++n) {
        const Json& seg = (*ranges)[n];
        const double start = seg.at("start").get<double>();
        const double end = seg.at("end").get<double>();
        std::string label = stripped_text(field(seg, "label"));
        if (label.empty()) {
            auto a = segment_mmss(start);
            auto b = a ? segment_mmss(end) : a;
            if (!a || !b) {
                return std::unexpected(a ? b.error() : a.error());
            }
            label = std::format("{} · {}–{}", base_label, *a, *b);
        }
        ClipInfo c;
        c.index = index + static_cast<std::int64_t>(n);
        c.id = ids.hex32().substr(0, 12);
        c.filename = src->filename;
        c.url = src->url;
        c.start = start;
        c.end = end;
        c.in_point = start;
        c.out_point = end;
        c.parent_id = parent_id;
        c.source_url = src->source_url;
        c.label = label;
        const std::string description = stripped_text(field(seg, "description"));
        c.description = description.empty() ? src->description : std::optional(description);
        c.origin = "segment";
        c.source = src->source;
        c.provider = src->provider;
        c.author = src->author;
        c.license_info = src->license_info;
        created_ids.insert(*c.id);
        created.push_back(std::move(c));
    }
    if (auto ok = store.add_clips(pid, created, ids); !ok) {
        return std::unexpected(ok.error());
    }
    auto fresh = store.get_project(pid);
    if (!fresh) {
        return std::unexpected(fresh.error());
    }
    std::vector<ClipInfo> out;
    if (*fresh) {
        for (const ClipInfo& c : (*fresh)->clips) {
            if (c.id && created_ids.contains(*c.id)) {
                out.push_back(c);
            }
        }
    }
    return out;
}

Result<std::vector<Keyframe>> seed_face_keyframes(std::span<const TrackPoint> track, double start, double end) {
    const auto key = [](double t, double cx, double cy) {
        Keyframe k;
        k.t = t;
        k.cx = cx;
        k.cy = cy;
        return k;
    };
    const double dur = end - start > 0.0 ? end - start : 0.0;
    if (track.empty()) {
        return std::vector<Keyframe>{key(py_round(start, 3), 0.5, 0.5)};
    }
    const double window = dur / 12.0 > 1.0 ? dur / 12.0 : 1.0; // max(1.0, dur / 12)
    std::map<std::int64_t, std::vector<const TrackPoint*>> buckets;
    for (const TrackPoint& p : track) {
        const double since = p.t - start > 0.0 ? p.t - start : 0.0;
        const double bucket = detail::py_floordiv(since, window);
        // int(…) de Python: con un tiempo infinito, ValueError. Más allá de 64 bits, divergencia.
        if (!std::isfinite(bucket) || std::abs(bucket) >= 9.2e18) {
            return fail(ErrorCode::InvalidArgument, "no se puede pasar a entero un tiempo infinito o NaN");
        }
        buckets[static_cast<std::int64_t>(bucket)].push_back(&p);
    }
    std::vector<Keyframe> out;
    for (const auto& [k, pts] : buckets) {
        std::vector<double> xs;
        std::vector<double> ys;
        for (const TrackPoint* p : pts) {
            xs.push_back(p->cx);
            ys.push_back(p->cy);
        }
        const double cx = median(std::move(xs));
        const double cy = median(std::move(ys));
        const double mid = start + ((static_cast<double>(k) + 0.5) * window);
        const double t = mid < end ? mid : end; // min(end, …)
        if (!out.empty() && std::abs(cx - out.back().cx) < kDeadzone && std::abs(cy - out.back().cy) < kDeadzone) {
            continue;
        }
        out.push_back(key(py_round(t, 3), py_round(cx, 4), py_round(cy, 4)));
    }
    if (out.empty()) { // no pasa: la primera ventana siempre entra
        return out;
    }
    // Extremos anclados: el clip empieza y acaba encuadrado.
    if (out.front().t > start + 0.01) {
        out.insert(out.begin(), key(py_round(start, 3), out.front().cx, out.front().cy));
    }
    if (out.back().t < end - 0.01) {
        out.push_back(key(py_round(end, 3), out.back().cx, out.back().cy));
    }
    return out;
}

const FaceTrack* cached_face_track(const ClipInfo& clip, double start, double end) {
    if (!clip.face_track) {
        return nullptr;
    }
    const FaceTrack& ft = *clip.face_track;
    return std::abs(ft.start - start) <= kCacheEps && std::abs(ft.end - end) <= kCacheEps ? &ft : nullptr;
}

Result<FaceTrackResult> face_track_material(ProjectsStore& store, std::string_view pid, std::string_view ident,
                                            std::optional<double> start, std::optional<double> end, int samples,
                                            bool force, const FaceDetector& detect,
                                            const std::function<std::string()>& now) {
    auto proj = project_of(store, pid);
    if (!proj) {
        return std::unexpected(proj.error());
    }
    const ClipInfo* clip = find_material_clip(*proj, ident);
    if (clip == nullptr) {
        return fail(ErrorCode::NotFound, "Material de vídeo no encontrado.");
    }
    const auto [own_a, own_b] = material_file_range(*clip);
    const double a = start ? (*start > 0.0 ? *start : 0.0) : own_a;
    const double b = end ? *end : own_b;
    if (b - a < kMinSegment) {
        return fail(ErrorCode::InvalidArgument, "El rango es demasiado corto para analizar caras.");
    }
    if (!force) {
        if (const FaceTrack* hit = cached_face_track(*clip, a, b)) {
            return FaceTrackResult{*hit, true};
        }
    }
    auto path = media_path(store, *proj, *clip);
    if (!path) {
        return std::unexpected(path.error());
    }
    auto info = detect(*path, a, b, samples);
    if (!info) {
        return std::unexpected(info.error());
    }
    FaceTrack ft;
    ft.start = py_round(a, 3);
    ft.end = py_round(b, 3);
    ft.width = info->width;
    ft.height = info->height;
    ft.fps = info->fps;
    ft.samples = info->samples;
    ft.track = info->track;
    auto keys = seed_face_keyframes(info->track, a, b);
    if (!keys) {
        return std::unexpected(keys.error());
    }
    ft.keyframes = std::move(*keys);
    ft.created_at = now();
    if (std::abs(a - own_a) <= kCacheEps && std::abs(b - own_b) <= kCacheEps) {
        // El rango del material: se guarda en él con el reframe sembrado.
        Reframe rf = clip->reframe.value_or(Reframe{});
        rf.keyframes = ft.keyframes;
        rf.pan_mode = "smooth";
        rf.face_track_mode = "smooth";
        const Json fields{{"face_track", to_json(ft)}, {"reframe", to_json(rf)}};
        if (auto ok = store.update_material(pid, "clips", std::to_string(clip->index), fields); !ok) {
            return std::unexpected(ok.error());
        }
    }
    return FaceTrackResult{std::move(ft), false};
}

} // namespace gc::core
