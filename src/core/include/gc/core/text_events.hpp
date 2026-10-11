#pragma once

// Cuándo se ve cada parte de un texto QUIETO en el export: los eventos del .ass de text_ass.py
// (caption_dialogues) y el instante que FFmpeg pasa a libass en cada fotograma.
//
// El export escribe un evento por palabra activa (karaoke) o uno para todo el clip, y uno por
// paso con la aparición «escribir». Sus tiempos van en centésimas («0:00:01.23») y libass dibuja
// un evento en el instante `now` (ms) si start ≤ now < end.

#include <cstdint>
#include <string>
#include <vector>

#include "gc/core/json.hpp"
#include "gc/core/model.hpp"
#include "gc/core/text_style.hpp"

namespace gc::core {

// Tiempo de un evento en ms tal como lo lee libass: ass_time (h:mm:ss.cc, con los segundos
// redondeados a centésimas como "{:05.2f}" de Python) pasado a ms.
[[nodiscard]] std::int64_t ass_time_ms(double t);

// El `now` del fotograma k de una timeline a `fps` en el filtro ass de FFmpeg: el pts en ms,
// truncado (k · (1 / fps) · 1000).
[[nodiscard]] std::int64_t ass_frame_ms(std::int64_t k, double fps);

// Un evento del texto: [start_ms, end_ms), lo que se ve (texto y palabra resaltada) y si es el
// primero (la aparición de bloque, \fad o \move, solo va en el primero).
struct TextEvent {
    std::int64_t start_ms = 0;
    std::int64_t end_ms = 0;
    TextContent content;
    bool first = false;
};

// Eventos de un texto quieto con su estilo con la pose en 0 (caption_dialogues): por palabra
// con efecto de palabra y más de una, por paso con «escribir», o uno para todo el clip.
[[nodiscard]] std::vector<TextEvent> static_text_events(const TimelineClip& clip, const Json& posed_style);

// ¿Entra este texto en el .ass? Más de 0,02 s, con palabras y no desactivado (build_ass).
[[nodiscard]] bool text_in_ass(const TimelineClip& clip);

} // namespace gc::core
