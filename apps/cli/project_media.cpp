#include "project_media.hpp"

#include <cstdlib>
#include <format>

#include "gc/core/collections.hpp"
#include "gc/core/io.hpp"
#include "gc/core/projects_store.hpp"
#include "gc/core/settings.hpp"
#include "gc/core/sfx_disk.hpp"

namespace genclip {

namespace fs = std::filesystem;
using namespace gc::core;

gc::Result<LoadedProject> load_project(const fs::path& root, const std::string& id) {
    const char* assets_env = std::getenv("VIDEO_YT_ASSETS"); // NOLINT(concurrency-mt-unsafe): al arrancar
    const fs::path assets =
        assets_env != nullptr && *assets_env != '\0' ? path_from_utf8(assets_env) : root.parent_path() / "assets";
    const SettingsFile settings_file(root / "data" / "settings.json");
    LoadedProject p;
    p.root = root;
    p.settings = settings_file.load();
    p.dirs = MediaDirs{root / "clips", root / "data", collections_root(p.settings, assets / "material")};
    p.sfx_base = sfx_base(p.settings, assets / "sfx_library");
    const ProjectsStore store(p.dirs.data_dir / "projects.json", p.dirs.clips_dir);
    auto project = store.get_project(id);
    if (!project) {
        return std::unexpected(project.error());
    }
    if (!*project) {
        return gc::fail(gc::ErrorCode::NotFound, std::format("no existe el proyecto «{}»", id));
    }
    if (!(*project)->timeline) {
        return gc::fail(gc::ErrorCode::NotFound, std::format("el proyecto «{}» no tiene timeline", id));
    }
    p.project = **project;
    p.timeline = *p.project.timeline;
    return p;
}

std::optional<fs::path> clip_path(const LoadedProject& p, const TimelineClip& clip) {
    if (clip.asset_kind == "sfx") {
        return sfx_resolve(p.sfx_base, clip.filename);
    }
    return clip_media_path(p.project, clip, p.dirs);
}

} // namespace genclip
