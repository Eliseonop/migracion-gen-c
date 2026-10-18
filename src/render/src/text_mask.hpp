#pragma once

// La máscara de tipo texto (_paint_text de backend/app/clip_mask.py): la dibuja text.cpp, que es
// quien tiene las fuentes. No sale de la biblioteca.

#include "gc/core/json.hpp"
#include "gc/render/text.hpp"
#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"

namespace gc::render {

// Las líneas de `m["text"]` (la máscara normalizada) con `paint`, como ImageDraw.text de Pillow:
// a `size` = round(text.size · ref) px, cada línea 1,12 · size más abajo y centradas en vertical
// alrededor de (cx, cy), ancladas a la izquierda (cx − hw), al centro o a la derecha (cx + hw); y
// todo girado `rotation` grados alrededor de (cx, cy), como el warpAffine posterior. false si la
// fuente no se puede leer (allí, la tipografía de respaldo de OpenCV).
[[nodiscard]] bool paint_text_mask(SkCanvas& canvas, const SkPaint& paint, const Json& m, double cx, double cy,
                                   double hw, double ref, const TextFonts& fonts);

} // namespace gc::render
