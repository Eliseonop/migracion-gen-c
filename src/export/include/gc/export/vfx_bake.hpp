#pragma once

// Los efectos de vídeo (`clip.vfx`) en el export y en `genclip frame`, como bake_timeline de
// fx_bake.py: cada clip visible con efectos en una pista de vídeo se cambia en la timeline por el
// clip horneado (gc::render::VfxBakePlan) y su material sale del tramo original con los efectos.

#include <filesystem>
#include <functional>
#include <map>
#include <string>

#include "gc/core/error.hpp"
#include "gc/core/model.hpp"
#include "gc/export/project.hpp"
#include "gc/render/vfx.hpp"

namespace gc::exporter {

// fx_catalog.CATALOG_PATH: <raíz>/app/fx_assets/catalog.json. Si no hay catálogo activo, carga ese;
// sin él, ningún efecto existe (como en video-yt si falta el archivo).
void ensure_fx_catalog(const LoadedProject& project);

using VfxPlans = std::map<std::string, render::VfxBakePlan>;

// bake_timeline: los clips que se hornean (activos, en una pista de vídeo visible, con material en
// `paths` y algún efecto aplicable), cambiados en `timeline` por su clip horneado. Lo que el render
// aún no sabe hornear (otra velocidad, el revés) es un error o, con `skip`, un aviso y el clip se
// queda como está (y el compositor lo omitirá).
[[nodiscard]] Result<VfxPlans> plan_timeline_vfx(core::Timeline& timeline,
                                                 const std::map<std::string, std::filesystem::path>& paths, int width,
                                                 int height, int fps, bool skip,
                                                 const std::function<void(const std::string&)>& warn);

} // namespace gc::exporter
