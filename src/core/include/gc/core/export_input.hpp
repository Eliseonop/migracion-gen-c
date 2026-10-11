#pragma once

// Cómo lee el export de video-yt el material de cada clip (espejo de clip_kind.py:
// is_still_clip, ffmpeg_trim_window, ffmpeg_trim_filter y ffmpeg_input_args). gc_media
// reproduce con libav* lo que estas cadenas piden a ffmpeg: qué entrada abrir y qué tramo
// tomar, ya a CFR al fps de la timeline.

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "gc/core/model.hpp"

namespace gc::core {

// Un clip se dibuja a partir de una imagen fija: figura, imagen o material de `images`.
[[nodiscard]] bool is_still_clip(const TimelineClip& clip);

// Lo que se adelanta el final si el clip llega al fin del archivo (pedir justo el EOF deja
// un fotograma negro): el máximo entre esto y un fotograma.
inline constexpr double kLastFramePull = 0.04;

// Ventana [entrada, salida] del `trim` sobre la entrada de ffmpeg (ffmpeg_trim_window).
// Still: [0, max(out − in, 0,1)] (la entrada es un bucle desde 0). Vídeo: [in, out], con la
// salida adelantada si `out ≥ source_duration − 1e-4`.
[[nodiscard]] std::pair<double, double> ffmpeg_trim_window(const TimelineClip& clip, int fps = 30);

// "fps=F,trim=a:b" (vídeo) o "trim=a:b" (still), con 3 decimales (ffmpeg_trim_filter).
[[nodiscard]] std::string ffmpeg_trim_filter(const TimelineClip& clip, int fps = 30);

// Argumentos de ffmpeg de la entrada del clip (ffmpeg_input_args), terminados en "-i" y la
// ruta: WebM o motion con el decodificador libvpx-vp9 (el VP9 nativo no da el alfa),
// secuencia PNG («%0…») a `fps` desde 0, GIF con su demuxer (-ignore_loop), imagen en bucle
// (-loop 1 -framerate fps) y el resto, el archivo tal cual. `path` en UTF-8.
[[nodiscard]] std::vector<std::string> ffmpeg_input_args(const TimelineClip& clip, std::string_view path, int fps);

} // namespace gc::core
