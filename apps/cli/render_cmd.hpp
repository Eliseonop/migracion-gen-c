#pragma once

// genclip render: el MP4 de la timeline de un proyecto, como el export de video-yt
// (compose.build_command): cada fotograma compuesto como `genclip frame`, el audio con el grafo
// del export y el encode H.264 + AAC. Solo con gc_media y gc_render (presets msvc-vcpkg-*).

#include <span>
#include <string>

namespace genclip {

// args: <raíz de video-yt> <proyecto> -o <salida.mp4> [--progress] [--omitir] [--codificador <nombre>]
// Devuelve el código de salida (0 bien, 1 error, 2 uso).
int cmd_render(std::span<const std::string> args);

} // namespace genclip
