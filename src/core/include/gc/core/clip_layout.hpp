#pragma once

// Encuadre (recorte de la fuente) y colocación del resultado en el lienzo de salida
// (espejo de backend/app/clip_layout.py, que a su vez es espejo de
// frontend/src/lib/clipLayout.js). El recorte no cambia al escalar ni mover el resultado.
//
// Lo usan el render (source_crop_px, dest_rect_even, flip_filters), los efectos
// animados (crop_window) y el seguimiento de objetos (source_point_to_output).

#include <cstdint>
#include <string>

#include "gc/core/error.hpp"
#include "gc/core/json.hpp"
#include "gc/core/model.hpp"

namespace gc::core {

// layout == "overlay": objeto libre sobre el lienzo, con su propio recorte y transform.
[[nodiscard]] bool is_overlay(const TimelineClip& clip);

// Volteo del contenido en los ejes PROPIOS del clip, antes del giro (como CapCut). La
// caja, la máscara y los tiradores no se voltean.
struct Flip {
    bool h = false;
    bool v = false;
};
[[nodiscard]] Flip clip_flip(const TimelineClip& clip);
// "hflip", "vflip", "hflip,vflip" o "" para la cadena de FFmpeg de un vídeo o una imagen.
// Las figuras y los textos se voltean al dibujarlos: para ellos siempre "".
[[nodiscard]] std::string flip_filters(const TimelineClip& clip);

// Recorte en píxeles de la fuente: tamaño crop_w × crop_h (fracciones) centrado en
// (cx, cy) y metido dentro de la imagen.
struct SourceCrop {
    double sx = 0.0;
    double sy = 0.0;
    double sw = 0.0;
    double sh = 0.0;
};
[[nodiscard]] SourceCrop source_crop_px(double crop_w, double crop_h, double cx, double cy, double src_w,
                                        double src_h);

// Dónde va en la salida un recorte de crop_sw × crop_sh px según `transform`
// ({x, y, scale, rotation}; null = centrado a escala 1). Como en Python, un valor falso
// (0, "", null) en x, y, scale o rotation cuenta como ausente. Error si alguno no es un número.
struct DestRect {
    double dx = 0.0;
    double dy = 0.0;
    double dw = 0.0;
    double dh = 0.0;
    double rotation = 0.0;
};
[[nodiscard]] Result<DestRect> dest_rect(const Json& transform, double crop_sw, double crop_sh, double out_w,
                                         double out_h);

// El mismo rectángulo en enteros pares para FFmpeg: tamaños de al menos 2 y posiciones
// que pueden ser negativas (FFmpeg recorta lo que sale del lienzo). Error si algún valor no
// es finito o pasa de 4·10¹⁸ px (Python daría un entero enorme; aquí no cabe en 64 bits).
struct DestRectEven {
    std::int64_t dx = 0;
    std::int64_t dy = 0;
    std::int64_t dw = 2;
    std::int64_t dh = 2;
    double rotation = 0.0;
};
[[nodiscard]] Result<DestRectEven> dest_rect_even(const Json& transform, double crop_sw, double crop_sh,
                                                  double out_w, double out_h);

// Ventana de recorte sobre la fuente en un instante: centro (cx, cy) y tamaño (wf, hf),
// todo en 0–1. Sale del reframe (en `src_t`, segundos de la FUENTE), de los keyframes
// cx/cy/zoom (en `local_t`, segundos desde el inicio del clip) o del recorte libre de un
// overlay. Los aspectos son ancho/alto y deben ser finitos y mayores que 0 (Python lanza
// ZeroDivisionError; aquí saldrían inf o NaN).
struct CropWindow {
    double cx = 0.5;
    double cy = 0.5;
    double wf = 1.0;
    double hf = 1.0;
};
[[nodiscard]] CropWindow crop_window(const TimelineClip& clip, double src_aspect, double out_aspect, double src_t,
                                     double local_t);

// Dónde cae en la salida (0–1) el punto (nx, ny) de la fuente (0–1) en ese instante:
// recorte, pose con keyframes, volteo y giro. Las dimensiones deben ser mayores que 0.
struct Point {
    double x = 0.0;
    double y = 0.0;
};
[[nodiscard]] Point source_point_to_output(const TimelineClip& clip, double nx, double ny, double src_w, double src_h,
                                           double out_w, double out_h, double src_t, double local_t);

} // namespace gc::core
