#pragma once

// «Papel recortado» (paper_cutout, fx/effects/paper.js de video-yt), el único efecto de vídeo de
// motor «canvas»: dibuja en `g` (el lienzo de salida del efecto, cw × ch, ya vacío) el contenido
// `src` (su recorte `src_rect`, en px de la imagen) colocado en `content`, con el borde de papel
// rasgado, la textura de arrugas, el pliegue, la sombra y la vibración «stop-motion».

#include <filesystem>

#include "gc/core/error.hpp"
#include "gc/core/json.hpp"
#include "include/core/SkCanvas.h"
#include "include/core/SkImage.h"
#include "include/core/SkRect.h"

namespace gc::render::detail {

// rand(...parts) de fx/core/rand.js: un número en [0, 1) del hash Murmur3 de las partes.
[[nodiscard]] double fx_rand(std::initializer_list<double> parts);

// `assets`: la raíz pública del frontend (con paper-animator/assets/texture/*.webp). Error si falta
// una textura.
[[nodiscard]] Result<void> paper_cutout(SkCanvas& g, const sk_sp<SkImage>& src, const SkRect& src_rect,
                                        const SkRect& content, double t, double dur, const Json& params, double seed,
                                        const std::filesystem::path& assets);

} // namespace gc::render::detail
