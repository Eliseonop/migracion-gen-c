#pragma once

// Lo que preparan el export entero (render.cpp) y el fotograma suelto (frame.cpp) antes de evaluar
// ningún fotograma, para que los dos dibujen lo mismo: el material de cada clip (como _clip_path +
// media_exists de compose.py), la timeline que se dibuja (sin los clips de vídeo o imagen sin
// material ni los motion graphics, que el render aún no sabe dibujar), los efectos de vídeo
// (bake_timeline) y el matte de «Eliminar fondo» de cada clip (build_timeline_bg_masks). Y cómo
// sale el fotograma de un clip de su lector. Interno de gc_export.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "gc/core/bg_matte.hpp"
#include "gc/core/error.hpp"
#include "gc/core/model.hpp"
#include "gc/export/project.hpp"
#include "gc/export/vfx_bake.hpp"
#include "gc/media/decode.hpp"
#include "gc/render/canvas.hpp"

namespace gc::exporter::detail {

// NOLINTNEXTLINE(bugprone-exception-escape): el move de std::map de MSVC reserva su nodo (bad_alloc)
struct Prepared {
    core::Timeline timeline;                            // lo que se dibuja
    std::map<std::string, std::filesystem::path> paths; // el material de cada clip que lo tiene (de la timeline entera)
    std::vector<core::TimelineClip> motion;             // los motion graphics que se quitaron (sin los desactivados)
    VfxPlans vfx;                                       // los clips horneados (vfx_bake.hpp)
    std::map<std::string, std::optional<core::MatteSpec>> mattes;
};

// El material de cada clip de `full` que lo tiene en el disco (_clip_path + media_exists).
[[nodiscard]] std::map<std::string, std::filesystem::path> material_paths(const LoadedProject& project,
                                                                          const core::Timeline& full);

// El audio entero de `full`, como el export (audio.cpp): el mismo PCM que el CLI con el grafo de
// build_command, de `total` s. Vacío si no suena nada.
[[nodiscard]] Result<std::vector<float>> mix_audio(const core::Timeline& full,
                                                   const std::map<std::string, std::filesystem::path>& paths, int fps,
                                                   double total);

// `full`: la timeline del proyecto entera. Avisa (`warn`) del material que falta; con `skip`, los
// efectos de vídeo que no se saben hacer se saltan con un aviso en vez de ser un error.
[[nodiscard]] Result<Prepared> prepare(const LoadedProject& project, const core::Timeline& full, int width, int height,
                                       int fps, bool skip, const std::function<void(const std::string&)>& warn);

// El id del clip cuyo material usa una capa (las pasadas fantasma, kGhostSuffix, usan el de su clip).
[[nodiscard]] std::string material_id(const std::string& clip_id);

// El lector del clip `id` (el tramo original si se hornea), con su matte.
[[nodiscard]] Result<media::ClipFrames> open_reader(const Prepared& p, const std::string& id, int fps,
                                                  bool hwaccel = false);

// El fotograma del clip `id` en el tick de la timeline, del lector `reader`: con el horneado de
// sus efectos de vídeo si los tiene.
[[nodiscard]] Result<render::Image> read_image(const Prepared& p, const std::string& id, media::ClipFrames& reader,
                                               std::int64_t tick, int fps);

} // namespace gc::exporter::detail
