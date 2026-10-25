#pragma once

// Un proyecto de video-yt cargado desde su raíz (la de config.BASE_DIR, es decir
// <video-yt>/backend) con las carpetas de donde sale su material, y la ruta del material de cada
// clip como compose._clip_path. Lo usan `genclip frame`, `genclip render` y el export.

#include <filesystem>
#include <optional>
#include <string>

#include "gc/core/bg_matte.hpp"
#include "gc/core/clip_media.hpp"
#include "gc/core/error.hpp"
#include "gc/core/json.hpp"
#include "gc/core/model.hpp"

namespace gc::exporter {

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

// «Eliminar fondo»: el matte de la caché de un clip (build_clip_bg_mask, con la caché en
// DATA_DIR/bgcache). Nada si no lo tiene o si no está; si no se puede preparar, nada y el motivo en
// `warning`, como build_timeline_bg_masks, que lo registra y sigue sin el matte. Un matte con pluma,
// expansión, opacidad o pincel sale con solo la LUT, y con un aviso.
[[nodiscard]] std::optional<gc::core::MatteSpec> clip_matte(const LoadedProject& p,
                                                            const gc::core::TimelineClip& clip,
                                                            std::string& warning);

} // namespace gc::exporter
