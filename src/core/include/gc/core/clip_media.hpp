#pragma once

// Dónde está el material de un clip en el disco (espejo de compose._clip_path): en la carpeta
// del proyecto, en la biblioteca (data/library) o en una colección, según asset_scope, y en
// video/, audio/ o image/ según asset_kind (ASSET_DISK_KIND de clip_kind.py; si no lo dice y
// es un still, image/).
//
// Sin portar aún (nullopt): las figuras (se rasterizan al exportar), los motion graphics (los
// guarda Motion Studio) y los SFX de la biblioteca de sonidos.

#include <filesystem>
#include <optional>

#include "gc/core/model.hpp"

namespace gc::core {

struct MediaDirs {
    std::filesystem::path clips_dir;                  // config.OUTPUT_DIR: <raíz>/clips
    std::filesystem::path data_dir;                   // config.DATA_DIR: <raíz>/data
    std::optional<std::filesystem::path> collections; // la carpeta de colecciones (ajustes), si hay
};

[[nodiscard]] std::optional<std::filesystem::path> clip_media_path(const Project& project, const TimelineClip& clip,
                                                                   const MediaDirs& dirs);

} // namespace gc::core
