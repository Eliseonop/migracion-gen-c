#pragma once

// El fotograma compuesto: las capas de core::evaluate dibujadas, de fondo a frente, sobre el
// fondo negro opaco del export (`color=c=black`, compose.py).
//
// Por ahora las capas con material (vídeo, imagen y motion: las que traen blits), las figuras
// (shape.hpp) y las capas de ajuste (su matriz sobre lo de debajo), con los 12 modos de fusión, con máscaras (salvo la de texto; mask.hpp), con el croma (lo hace el decode) pero sin el matte automático ni
// el contorno, con los filtros de color (`filters` y `look`) y sin efectos (`effects`) ni
// efectos de vídeo (`vfx`). Lo demás es un error Unsupported hasta que llegue,
// para no dar un fotograma incompleto o mal dibujado como bueno (unsupported_reason).

#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "gc/core/error.hpp"
#include "gc/core/frame.hpp"
#include "gc/render/canvas.hpp"

namespace gc::render {

// El fotograma del material de una capa en su instante (layer.src_t). Error si no se puede
// obtener; el puntero tiene que seguir siendo válido hasta que vuelva compose_frame.
using SourceFn = std::function<Result<const Image*>(const core::FrameLayer&)>;

// Por qué el render aún no puede dibujar esta capa, o nada si puede. Una capa de material sin
// blits (evaluate no supo sus dimensiones) no cuenta aquí: compose_frame la da como error de
// argumento.
[[nodiscard]] std::optional<std::string> unsupported_reason(const core::FrameLayer& layer);

[[nodiscard]] Result<Image> compose_frame(const std::vector<core::FrameLayer>& layers, int width, int height,
                                          const SourceFn& source);

// Lo mismo, pero sobre un lienzo que ya existe (se borra a negro opaco): para poner encima los
// textos y reutilizar el lienzo de un fotograma al siguiente. El resultado es opaco.
[[nodiscard]] Result<void> compose_into(Canvas& canvas, const std::vector<core::FrameLayer>& layers,
                                        const SourceFn& source);

} // namespace gc::render
