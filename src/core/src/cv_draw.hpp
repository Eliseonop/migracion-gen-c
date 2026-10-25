#pragma once

// Interno de gc_core: lo que dibuja cv2 con LINE_AA en una imagen de un canal de 8 bits, en blanco
// (255), portado de modules/imgproc/src/drawing.cpp de OpenCV 5.0.0 (la versión del export de
// video-yt) con la misma aritmética de punto fijo (16 bits de fracción). Lo usa el pincel de
// «Eliminar fondo» (paint_edit de clip_bg.py).

#include <cstdint>
#include <vector>

namespace gc::core::detail {

struct CvGray {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> px; // width · height, sin relleno
};

struct CvPoint {
    std::int64_t x = 0;
    std::int64_t y = 0;
};

// cv2.polylines(img, [pts], isClosed=False, 255, thickness, cv2.LINE_AA).
void cv_polylines_aa(CvGray& img, const std::vector<CvPoint>& pts, int thickness);

// cv2.circle(img, center, radius, 255, -1, cv2.LINE_AA): un círculo relleno.
void cv_circle_filled_aa(CvGray& img, CvPoint center, int radius);

} // namespace gc::core::detail
