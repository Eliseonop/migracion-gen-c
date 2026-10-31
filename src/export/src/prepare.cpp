#include "prepare.hpp"

#include <format>
#include <utility>

#include "gc/core/clip_adjust.hpp"

namespace gc::exporter::detail {

namespace {

namespace fs = std::filesystem;
using namespace gc::core;

// Los clips cuyo material se busca en el disco para dibujarlo. Las figuras pasan tal cual
// (unsupported_reason las rechaza) y los motion graphics van aparte.
bool has_media(const TimelineClip& clip) { return clip.kind == "video" || clip.kind == "image"; }

} // namespace

Result<Prepared> prepare(const LoadedProject& project, const Timeline& full, int width, int height, int fps, bool skip,
                         const std::function<void(const std::string&)>& warn) {
    Prepared p;
    for (const auto& c : full.clips) {
        const auto path = clip_path(project, *c);
        if (path && fs::is_regular_file(*path)) {
            p.paths[c->id] = *path;
        }
    }
    // Lo que se dibuja: sin los clips cuyo material no existe (como el export) ni los motion
    // graphics, que el render aún no sabe dibujar.
    p.timeline = full;
    std::erase_if(p.timeline.clips, [&](const Cow<TimelineClip>& c) {
        if (c->kind == "motion") {
            if (!c->disabled) {
                p.motion.push_back(*c);
            }
            return true;
        }
        if (has_media(*c) && !p.paths.contains(c->id)) {
            warn(std::format("sin material para el clip {} ({}): no se dibuja", c->id, c->filename));
            return true;
        }
        return false;
    });
    // Los efectos de vídeo: como bake_timeline, el clip horneado en la timeline (vfx_bake.hpp).
    ensure_fx_catalog(project);
    auto vfx = plan_timeline_vfx(p.timeline, p.paths, width, height, fps, skip, warn);
    if (!vfx) {
        return std::unexpected(vfx.error());
    }
    p.vfx = std::move(*vfx);
    // «Eliminar fondo» con el matte de la caché (build_timeline_bg_masks): de cada clip, o del
    // original si se hornea (el horneado ya no lleva bg_removal).
    for (const auto& c : p.timeline.clips) {
        const auto bake = p.vfx.find(c->id);
        std::string warning;
        auto spec = clip_matte(project, bake != p.vfx.end() ? bake->second.source : *c, warning);
        if (!warning.empty()) {
            warn(warning);
        }
        if (spec) {
            p.mattes.emplace(c->id, std::move(spec));
        }
    }
    return p;
}

std::string material_id(const std::string& clip_id) {
    return clip_id.ends_with(kGhostSuffix) ? clip_id.substr(0, clip_id.size() - kGhostSuffix.size()) : clip_id;
}

Result<media::ClipFrames> open_reader(const Prepared& p, const std::string& id, int fps) {
    const auto path = p.paths.find(id);
    const auto clip = std::ranges::find_if(p.timeline.clips, [&](const auto& c) { return c->id == id; });
    if (path == p.paths.end() || clip == p.timeline.clips.end()) {
        return fail(ErrorCode::NotFound, std::format("sin material para el clip {}", id));
    }
    const auto matte = p.mattes.find(id);
    const MatteSpec* m = matte != p.mattes.end() && matte->second ? &*matte->second : nullptr;
    // Un clip horneado lee su tramo original (desde 0).
    if (const auto bake = p.vfx.find(id); bake != p.vfx.end()) {
        return media::ClipFrames::open_bake_input(bake->second.source, path->second, fps, m);
    }
    return media::ClipFrames::open(**clip, path->second, fps, m);
}

Result<render::Image> read_image(const Prepared& p, const std::string& id, media::ClipFrames& reader,
                                 std::int64_t tick, int fps) {
    // Un clip horneado pide el fotograma m de su tramo original y le aplica sus efectos (vfx.hpp).
    const auto bake = p.vfx.find(id);
    const std::int64_t at = bake != p.vfx.end() ? render::vfx_baked_index(bake->second, tick, fps) : tick;
    auto got = reader.at(at);
    if (!got) {
        return std::unexpected(got.error());
    }
    if (*got == nullptr) {
        return fail(ErrorCode::Io, std::format("el clip {} no da fotograma en el {}", id, tick));
    }
    const auto& f = **got;
    if (bake != p.vfx.end()) {
        return render::vfx_baked_frame(bake->second, render::Image{f.width, f.height, f.pixels}, at, fps);
    }
    return render::Image{f.width, f.height, f.pixels};
}

} // namespace gc::exporter::detail
