#pragma once

// Máscaras de clip en el render: el alfa que el export rasteriza con OpenCV (combined_alpha de
// backend/app/clip_mask.py) y aplica con `maskedmerge` después de componer el clip.
//
// En coordenadas del CUADRO (x·W, y·H; tamaños en fracciones del alto), como el export y el
// preview. Cada máscara: la forma con antialias (los mismos puntos que clip_mask.py, redondeados
// a píxel como `_to_canvas`), el difuminado (gaussiana de σ = feather·H, con el borde reflejado
// de OpenCV y, si σ ≥ 32, a resolución reducida como `_feather`), invertir y opacidad. Varias
// máscaras se intersecan (producto). La de texto aún no se dibuja (Unsupported).

#include <cstdint>
#include <string>
#include <vector>

#include "gc/core/error.hpp"
#include "gc/core/json.hpp"

namespace gc::render {

// Alfa de 8 bits (0 = se ve lo de debajo, 255 = se ve el clip), width · height, por filas.
struct MaskAlpha {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> alpha;
};

// El alfa combinado de las máscaras activas (las normalizadas de core::clip_masks_at) en un
// cuadro de width × height (como mínimo 2 × 2). Sin máscaras activas, todo 255. Unsupported si
// alguna es de texto.
[[nodiscard]] Result<MaskAlpha> mask_alpha(const std::vector<Json>& masks, int width, int height);

// Por qué el render aún no puede dibujar estas máscaras, o "" si puede.
[[nodiscard]] std::string mask_unsupported(const std::vector<Json>& masks);

} // namespace gc::render
