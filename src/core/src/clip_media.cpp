#include "gc/core/clip_media.hpp"

#include <string_view>

#include "gc/core/collections.hpp"
#include "gc/core/export_input.hpp"
#include "gc/core/library.hpp"
#include "gc/core/projects_store.hpp"

namespace gc::core {

namespace {

// _MEDIA_KIND de compose.py: ASSET_DISK_KIND sin los SFX.
std::optional<std::string_view> media_kind(std::string_view asset_kind) {
    if (asset_kind == "clips") {
        return "video";
    }
    if (asset_kind == "audios") {
        return "audio";
    }
    if (asset_kind == "images") {
        return "image";
    }
    return std::nullopt;
}

} // namespace

std::optional<std::filesystem::path> clip_media_path(const Project& project, const TimelineClip& clip,
                                                     const MediaDirs& dirs) {
    if (clip.kind == "shape" || clip.kind == "motion" || clip.asset_kind == "sfx") {
        return std::nullopt;
    }
    auto kind = media_kind(clip.asset_kind);
    if (!kind && is_still_clip(clip)) {
        kind = "image";
    }
    if (!kind) {
        return std::nullopt;
    }
    // `or "project"` (las dos ramas como string_view: con std::string sería un temporal).
    const std::string_view scope =
        clip.asset_scope.empty() ? std::string_view("project") : std::string_view(clip.asset_scope);
    if (scope == "collection") {
        return collection_resolve(dirs.collections, clip.filename);
    }
    if (scope == "library") {
        return resolve_library_media(dirs.data_dir, *kind, clip.filename);
    }
    return resolve_project_media(dirs.clips_dir, project, *kind, clip.filename);
}

} // namespace gc::core
