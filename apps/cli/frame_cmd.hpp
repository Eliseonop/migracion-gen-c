#pragma once

// genclip frame: un PNG del fotograma compuesto de la timeline de un proyecto en un instante,
// como compose.render_frame de video-yt. Solo con gc_media y gc_render (presets msvc-vcpkg-*).

#include <span>
#include <string>

namespace genclip {

// args: <raíz de video-yt> <proyecto> --at <s> -o <salida.png> [--omitir]
// Devuelve el código de salida (0 bien, 1 error, 2 uso).
int cmd_frame(std::span<const std::string> args);

} // namespace genclip
