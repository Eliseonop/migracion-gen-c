#pragma once

// El fotograma compuesto: las capas de core::evaluate dibujadas, de fondo a frente, sobre el
// fondo negro opaco del export (`color=c=black`, compose.py).
//
// Por ahora solo las capas con material (vídeo, imagen y motion: las que traen blits), en
// fusión normal y sin máscaras. Lo demás (texto, figuras, capas de ajuste, modos de fusión y
// máscaras) es un error Unsupported hasta que llegue, para no dar un fotograma incompleto
// como bueno.

#include <functional>
#include <vector>

#include "gc/core/error.hpp"
#include "gc/core/frame.hpp"
#include "gc/render/canvas.hpp"

namespace gc::render {

// El fotograma del material de una capa en su instante (layer.src_t). Error si no se puede
// obtener; el puntero tiene que seguir siendo válido hasta que vuelva compose_frame.
using SourceFn = std::function<Result<const Image*>(const core::FrameLayer&)>;

[[nodiscard]] Result<Image> compose_frame(const std::vector<core::FrameLayer>& layers, int width, int height,
                                          const SourceFn& source);

} // namespace gc::render
