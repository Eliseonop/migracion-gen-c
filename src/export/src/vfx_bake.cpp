#include "gc/export/vfx_bake.hpp"

#include <format>
#include <memory>

#include "gc/core/fx_bake.hpp"
#include "gc/core/fx_catalog.hpp"
#include "gc/media/decode.hpp"

namespace gc::exporter {

namespace fs = std::filesystem;

void ensure_fx_catalog(const LoadedProject& project) {
    // Las texturas de los efectos: las que sirve el horneado (fx_bake.PUBLIC_DIR = frontend/public).
    render::set_vfx_assets_dir(project.root.parent_path() / "frontend" / "public");
    // fx_catalog() nunca es nulo: hasta que se fija, es un catálogo vacío.
    if (const auto cat = core::fx_catalog(); cat && cat->data().contains("effects")) {
        return;
    }
    if (auto cat = core::FxCatalog::load(project.root / "app" / "fx_assets" / "catalog.json")) {
        core::set_fx_catalog(std::make_shared<const core::FxCatalog>(std::move(*cat)));
    }
}

Result<VfxPlans> plan_timeline_vfx(core::Timeline& timeline, const std::map<std::string, fs::path>& paths, int width,
                                   int height, int fps, bool skip, const std::function<void(const std::string&)>& warn) {
    VfxPlans plans;
    std::map<std::string, const core::TimelineTrack*> tracks;
    for (const core::TimelineTrack& t : timeline.tracks) {
        tracks.emplace(t.id, &t);
    }
    for (auto& cow : timeline.clips) {
        const core::TimelineClip& clip = *cow;
        const auto track = tracks.find(clip.track_id);
        const auto path = paths.find(clip.id);
        if (clip.disabled || track == tracks.end() || track->second->hidden || track->second->kind != "video" ||
            path == paths.end()) {
            continue;
        }
        auto has = core::has_vfx(clip);
        if (!has) {
            return std::unexpected(has.error());
        }
        if (!*has) {
            continue;
        }
        // detect.dims: el tamaño del material (su primer fotograma).
        auto first = media::frame_at(path->second, 0.0);
        if (!first || !first->has_value()) {
            return fail(ErrorCode::Io, std::format("no se pudo leer el material del clip {} para sus efectos", clip.id));
        }
        auto plan = render::plan_vfx_bake(clip, (*first)->width, (*first)->height, width, height, fps);
        if (!plan) {
            if (!skip || plan.error().code != ErrorCode::Unsupported) {
                return std::unexpected(plan.error());
            }
            warn(plan.error().message + ": se omite");
            continue;
        }
        if (*plan) {
            const std::string id = clip.id; // `clip` deja de existir al cambiar el clip
            cow = (*plan)->baked;
            plans.emplace(id, std::move(**plan));
        }
    }
    return plans;
}

} // namespace gc::exporter
