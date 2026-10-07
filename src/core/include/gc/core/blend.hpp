#pragma once

// Modos de fusión de un clip con lo que tiene debajo (espejo de la parte de datos de
// backend/app/clip_blend.py y frontend/src/lib/clipBlend.js). Las fórmulas son las de
// W3C Compositing and Blending; aplicarlas es cosa del render.

#include <algorithm>
#include <array>
#include <string_view>

#include "gc/core/model.hpp"

namespace gc::core {

inline constexpr std::array<std::string_view, 12> kBlendModes{
    "normal",  "darken",  "multiply",   "color_burn", "lighten",    "screen",
    "color_dodge", "overlay", "soft_light", "hard_light", "difference", "exclusion",
};

// El modo si es uno de los conocidos; si no, "normal".
[[nodiscard]] inline std::string_view normalize_blend(std::string_view mode) {
    const auto it = std::ranges::find(kBlendModes, mode);
    return it == kBlendModes.end() ? "normal" : *it;
}

// Modo de fusión del clip ("normal" si no tiene o no es válido).
[[nodiscard]] inline std::string_view clip_blend(const TimelineClip& clip) {
    return clip.blend_mode ? normalize_blend(*clip.blend_mode) : "normal";
}

} // namespace gc::core
