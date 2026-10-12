#pragma once

// genclip: un proyecto de video-yt cargado desde su raíz (la de config.BASE_DIR, es decir
// <video-yt>/backend) con las carpetas de donde sale su material, y la ruta del material de cada
// clip como compose._clip_path. Lo comparten `genclip frame` y `genclip render`.

#include <filesystem>
#include <optional>
#include <string>

#include "gc/core/clip_media.hpp"
#include "gc/core/error.hpp"
#include "gc/core/json.hpp"
#include "gc/core/model.hpp"

namespace genclip {

struct LoadedProject {
    std::filesystem::path root;
    gc::core::MediaDirs dirs;
    gc::Json settings;
    std::optional<std::filesystem::path> sfx_base; // sfx.get_base
    gc::core::Project project;
    gc::core::Timeline timeline;
};

// config.py: OUTPUT_DIR = <raíz>/clips, DATA_DIR = <raíz>/data y ASSETS_DIR = VIDEO_YT_ASSETS o
// <raíz>/../assets; las colecciones, en `material_root` de los ajustes o <assets>/material, y los
// SFX en `sfx_folder` o <assets>/sfx_library. Error si el proyecto no existe o no tiene timeline.
[[nodiscard]] gc::Result<LoadedProject> load_project(const std::filesystem::path& root, const std::string& id);

// _clip_path de compose.py: el material de un clip de vídeo, imagen, audio o SFX (nada para las
// figuras, los motion graphics ni los textos).
[[nodiscard]] std::optional<std::filesystem::path> clip_path(const LoadedProject& p, const gc::core::TimelineClip& clip);

} // namespace genclip
