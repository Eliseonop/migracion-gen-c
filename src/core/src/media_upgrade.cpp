#include "gc/core/media_upgrade.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <set>

#include "gc/core/segments.hpp"
#include "gc/core/serialize.hpp"
#include "gc/core/youtube_url.hpp"
#include "py.hpp"

namespace gc::core {

using detail::py_path_stem;
using detail::py_round;

namespace {

constexpr double kAspectTol = 0.02; // 2 %: 1920x1080 frente a 854x480 (redondeos de YouTube)
constexpr double kRangeTol = 1.0;   // s: el tramo puede pasarse un poco del final
constexpr const char* kVideoNotFound = "Material de vídeo no encontrado (usa el vídeo completo, no un segmento).";

using Dims = std::pair<std::int64_t, std::int64_t>;

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

// _file_owner: el material dueño del archivo (no un segmento por referencia).
const ClipInfo* file_owner(const Project& project, const std::string& filename) {
    for (const ClipInfo& c : project.clips) {
        if (c.filename == filename && !c.in_point) {
            return &c;
        }
    }
    return nullptr;
}

// _dims: las medidas del archivo, o (0, 0) si no está o no se pueden leer.
Dims dims_of(const ProjectsStore& store, const Project& project, const ClipInfo& clip, const VideoDimsProbe& dims) {
    const auto path = resolve_project_media(store.clips_dir(), project, "video", clip.filename);
    std::error_code ec;
    if (!path || !std::filesystem::exists(*path, ec)) {
        return {0, 0};
    }
    return dims(*path).value_or(Dims{0, 0});
}

// _short: el lado corto (la «calidad»: 720, 1080…), o None.
Json short_side(const Dims& d) { return d.first != 0 && d.second != 0 ? Json(std::min(d.first, d.second)) : Json(); }

std::map<std::string, std::int64_t> timeline_uses(const Project& project) {
    std::map<std::string, std::int64_t> uses;
    if (project.timeline) {
        for (const auto& cow : project.timeline->clips) {
            const TimelineClip& c = *cow;
            if (c.asset_kind == "clips" && (c.asset_scope.empty() || c.asset_scope == "project")) {
                ++uses[c.filename];
            }
        }
    }
    return uses;
}

std::int64_t uses_of(const std::map<std::string, std::int64_t>& uses, const std::string& filename) {
    const auto it = uses.find(filename);
    return it == uses.end() ? 0 : it->second;
}

// _full_frame: el encuadre guardado deja el fotograma ENTERO (sin zoom ni recorte).
bool full_frame(const std::optional<Reframe>& rf) {
    if (!rf || rf->master) {
        return true;
    }
    const auto or_one = [](const std::optional<double>& v) { return v && *v != 0.0 ? *v : 1.0; }; // (x or 1.0)
    if (rf->dual_crop || or_one(rf->crop_w) < 0.999 || or_one(rf->crop_h) < 0.999) {
        return false;
    }
    if (!(rf->zoom >= 0.999)) {
        return false;
    }
    return std::ranges::all_of(rf->keyframes, [](const Keyframe& k) { return !k.zoom || *k.zoom >= 0.999; });
}

// _check: por qué `old` NO se puede cambiar por `next` (nullopt = se puede).
std::optional<std::string> check(const ClipInfo& old, const Dims& old_dims, const ClipInfo& next,
                                 const Dims& new_dims) {
    if (!full_frame(old.reframe)) {
        return "tiene el encuadre horneado (zoom o recorte guardado)";
    }
    const auto [ow, oh] = old_dims;
    const auto [nw, nh] = new_dims;
    if (ow != 0 && oh != 0 && nw != 0 && nh != 0) {
        const double ro = static_cast<double>(ow) / static_cast<double>(oh);
        const double rn = static_cast<double>(nw) / static_cast<double>(nh);
        if (std::abs(ro - rn) / rn > kAspectTol) {
            return std::format("otra proporción ({}×{} frente a {}×{})", ow, oh, nw, nh);
        }
    }
    const double new_off = material_file_offset(next);
    const double new_dur = material_file_duration(next);
    const double start = material_file_offset(old) - new_off;
    if (start < -0.05) {
        return "empieza antes que el vídeo nuevo";
    }
    if (new_dur != 0.0 && start + material_file_duration(old) > new_dur + kRangeTol) {
        return "dura más que el vídeo nuevo";
    }
    return std::nullopt;
}

Json opt_text(const std::optional<std::string>& s) { return s ? Json(*s) : Json(); }

// label or Path(filename).stem
std::string label_or_stem(const ClipInfo& c) { return c.label && !c.label->empty() ? *c.label : py_path_stem(c.filename); }

// La parte de find_versions sobre un proyecto ya cargado.
Result<Json> versions_of(const ProjectsStore& store, const Project& project, std::string_view ident,
                         const VideoDimsProbe& dims) {
    const ClipInfo* next = find_material_clip(project, ident);
    if (next == nullptr || next->in_point) {
        return fail(ErrorCode::NotFound, kVideoNotFound);
    }
    const auto vid = material_youtube_id(project, *next);
    const Dims new_dims = dims_of(store, project, *next, dims);
    const Json new_q = short_side(new_dims);
    const auto uses = timeline_uses(project);
    std::vector<Json> versions;
    if (vid) {
        for (const ClipInfo& c : project.clips) {
            if (&c == next || c.in_point || c.filename == next->filename) {
                continue;
            }
            if (material_youtube_id(project, c) != vid) {
                continue;
            }
            const Dims d = dims_of(store, project, c, dims);
            const Json q = short_side(d);
            const auto reason = check(c, d, *next, new_dims);
            const bool lower = !q.is_null() && !new_q.is_null() && q.get<std::int64_t>() < new_q.get<std::int64_t>();
            Json v = Json::object();
            v["index"] = c.index;
            v["id"] = opt_text(c.id);
            v["label"] = label_or_stem(c);
            v["filename"] = c.filename;
            v["quality"] = q;
            v["offset"] = py_round(material_file_offset(c), 3);
            v["duration"] = py_round(material_file_duration(c), 3);
            v["timeline_uses"] = uses_of(uses, c.filename);
            v["lower"] = lower;
            v["compatible"] = !reason;
            v["reason"] = opt_text(reason);
            versions.push_back(std::move(v));
        }
    }
    // sort(key=(-timeline_uses, offset)): estable.
    std::ranges::stable_sort(versions, [](const Json& a, const Json& b) {
        const auto ua = a["timeline_uses"].get<std::int64_t>();
        const auto ub = b["timeline_uses"].get<std::int64_t>();
        if (ua != ub) {
            return ua > ub;
        }
        return a["offset"].get<double>() < b["offset"].get<double>();
    });
    Json upgradable = Json::array();
    for (const Json& v : versions) {
        if (v["compatible"].get<bool>() && v["lower"].get<bool>()) {
            upgradable.push_back(v);
        }
    }
    Json material = Json::object();
    material["index"] = next->index;
    material["id"] = opt_text(next->id);
    material["label"] = opt_text(next->label);
    material["quality"] = new_q;
    material["youtube_id"] = opt_text(vid);
    Json out = Json::object();
    out["material"] = std::move(material);
    out["versions"] = Json(versions);
    out["upgradable"] = std::move(upgradable);
    return out;
}

// _retarget_segments: los segmentos por referencia de las versiones viejas pasan al archivo nuevo.
Result<std::int64_t> retarget_segments(ProjectsStore& store, std::string_view pid, const Project& project,
                                       const std::map<std::string, double>& deltas, const ClipInfo& next) {
    std::int64_t n = 0;
    for (const ClipInfo& c : project.clips) {
        const auto it = deltas.find(c.filename);
        if (!c.in_point || it == deltas.end()) {
            continue;
        }
        const double d = it->second;
        Json fields = Json::object();
        fields["filename"] = next.filename;
        fields["url"] = next.url;
        fields["parent_id"] = next.id && !next.id->empty() ? *next.id : std::to_string(next.index);
        fields["in_point"] = py_round(*c.in_point + d, 3);
        fields["out_point"] = py_round((c.out_point && *c.out_point != 0.0 ? *c.out_point : c.end) + d, 3);
        fields["start"] = py_round(c.start + d, 3);
        fields["end"] = py_round(c.end + d, 3);
        if (c.face_track) {
            Json ft = to_json(*c.face_track);
            ft["start"] = py_round(ft["start"].get<double>() + d, 3);
            ft["end"] = py_round(ft["end"].get<double>() + d, 3);
            for (const char* key : {"track", "keyframes"}) {
                for (Json& p : ft[key]) {
                    p["t"] = py_round(p["t"].get<double>() + d, 3);
                }
            }
            fields["face_track"] = std::move(ft);
        }
        if (auto r = store.update_material(pid, "clips", std::to_string(c.index), fields); !r) {
            return std::unexpected(r.error());
        }
        ++n;
    }
    return n;
}

} // namespace

std::optional<std::string> material_youtube_id(const Project& project, const ClipInfo& clip) {
    if (clip.provider == "youtube" && clip.external_id && !clip.external_id->empty()) {
        return clip.external_id;
    }
    if (clip.source_url && !clip.source_url->empty() && is_youtube_url(*clip.source_url)) {
        if (auto vid = youtube_id_from_url(*clip.source_url); vid && !vid->empty()) {
            return vid;
        }
    }
    if (clip.in_point) { // segmento: el del vídeo del que sale
        const ClipInfo* owner = file_owner(project, clip.filename);
        if (owner != nullptr && owner != &clip) {
            return material_youtube_id(project, *owner);
        }
    }
    return std::nullopt;
}

double material_file_offset(const ClipInfo& owner) { return owner.start > 0.0 ? owner.start : 0.0; }

double material_file_duration(const ClipInfo& owner) {
    const double d = owner.end - owner.start;
    return d > 0.0 ? d : 0.0;
}

Result<Json> find_media_versions(const ProjectsStore& store, std::string_view pid, std::string_view ident,
                                 const VideoDimsProbe& dims) {
    auto project = project_of(store, pid);
    if (!project) {
        return std::unexpected(project.error());
    }
    return versions_of(store, *project, ident, dims);
}

Result<Json> replace_media_with(ProjectsStore& store, std::string_view pid, std::string_view ident,
                                const std::vector<std::string>& old_idents, const VideoDimsProbe& dims,
                                const TimelineOpApplier& apply_op) {
    auto loaded = project_of(store, pid);
    if (!loaded) {
        return std::unexpected(loaded.error());
    }
    const Project& project = *loaded;
    const ClipInfo* next = find_material_clip(project, ident);
    if (next == nullptr || next->in_point) {
        return fail(ErrorCode::NotFound, kVideoNotFound);
    }
    const Dims new_dims = dims_of(store, project, *next, dims);

    std::vector<const ClipInfo*> olds;
    if (!old_idents.empty()) {
        for (const std::string& oid : old_idents) {
            const ClipInfo* c = find_material_clip(project, oid);
            if (c == nullptr) {
                return fail(ErrorCode::NotFound, std::format("Material no encontrado: {}", oid));
            }
            const ClipInfo* owner = c;
            if (c->in_point) {
                const ClipInfo* f = file_owner(project, c->filename);
                owner = f != nullptr ? f : c;
            }
            if (owner->filename == next->filename) {
                return fail(ErrorCode::InvalidArgument, "No se puede reemplazar un material por sí mismo.");
            }
            if (const auto reason = check(*owner, dims_of(store, project, *owner, dims), *next, new_dims)) {
                const std::string name = owner->label && !owner->label->empty() ? *owner->label : owner->filename;
                return fail(ErrorCode::InvalidArgument, std::format("«{}» no se puede cambiar: {}.", name, *reason));
            }
            // `owner not in olds`: la igualdad de Pydantic (los mismos campos), no la identidad.
            const Json key = to_json(*owner);
            if (std::ranges::none_of(olds, [&](const ClipInfo* o) { return to_json(*o) == key; })) {
                olds.push_back(owner);
            }
        }
    } else {
        auto info = find_media_versions(store, pid, ident, dims);
        if (!info) {
            return std::unexpected(info.error());
        }
        std::set<std::string> keep;
        for (const Json& v : (*info)["upgradable"]) {
            keep.insert(v["filename"].get<std::string>());
        }
        for (const ClipInfo& c : project.clips) {
            if (keep.contains(c.filename) && !c.in_point) {
                olds.push_back(&c);
            }
        }
    }
    if (olds.empty()) {
        return Json{{"ok", true},           {"replaced", 0}, {"changed", Json::array()}, {"versions", Json::array()},
                    {"segments", 0},        {"warnings", Json::array({"no hay versiones de menos calidad que cambiar"})}};
    }

    const double new_off = material_file_offset(*next);
    Json sources = Json::array();
    std::map<std::string, double> deltas; // {filename: delta}: con el mismo archivo, gana el último
    for (const ClipInfo* o : olds) {
        const double delta = py_round(material_file_offset(*o) - new_off, 3);
        sources.push_back({{"filename", o->filename}, {"delta", delta}});
        deltas[o->filename] = delta;
    }
    const Json target{{"asset_id", std::to_string(next->index)},
                      {"filename", next->filename},
                      {"duration", material_file_duration(*next)},
                      {"media_version", opt_text(next->created_at)}};

    const auto uses = timeline_uses(project);
    std::optional<Json> res;
    if (std::ranges::any_of(olds, [&](const ClipInfo* o) { return uses_of(uses, o->filename) != 0; })) {
        auto r = apply_op(pid, "replace_media", Json{{"sources", sources}, {"target", target}});
        if (!r) {
            return std::unexpected(r.error());
        }
        res = std::move(*r);
    }

    auto segments = retarget_segments(store, pid, project, deltas, *next);
    if (!segments) {
        return std::unexpected(segments.error());
    }
    Json versions = Json::array();
    for (const ClipInfo* o : olds) {
        versions.push_back({{"index", o->index}, {"label", opt_text(o->label)}, {"offset", material_file_offset(*o)}});
    }
    Json out = Json::object();
    out["ok"] = true;
    out["replaced"] = res ? (*res)["changed"].size() : 0;
    out["changed"] = res ? (*res)["changed"] : Json::array();
    out["warnings"] = res ? (*res)["warnings"] : Json::array();
    out["versions"] = std::move(versions);
    out["segments"] = *segments;
    out["can_undo"] = res ? (*res)["can_undo"] : Json();
    return out;
}

} // namespace gc::core
