#pragma once

// Fuentes del texto y sus métricas (espejo de font_metrics.py de video-yt).
//
// El editor usa el tamaño de letra CSS: el «em». libass (el .ass del export) toma el tamaño
// como alto de LÍNEA según las métricas OS/2 de la fuente:
//
//     em = tamaño_ass · unitsPerEm / (usWinAscent + usWinDescent)
//
// ass_size_factor da ese factor: el tamaño en px del alto de línea por cada px de em. Sirve
// también al render para dar a cada línea el alto que tenía en el export.

#include <filesystem>
#include <string_view>

namespace gc::core {

// Carpetas de fuentes: las del sistema y las que van con la app (Anton, en resources/fonts).
struct FontDirs {
    std::filesystem::path system;
    std::filesystem::path bundled;
};

// La del sistema que usa video-yt: C:/Windows/Fonts (fuera de Windows no existe y cada
// fuente acaba en un arial.ttf que no se puede leer, con factor 1, como en video-yt).
[[nodiscard]] std::filesystem::path default_system_font_dir();

// TTF de una fuente: primero las de la app (Anton…) si el archivo existe; si no, el de la
// carpeta del sistema (el negrita si lo hay) y, si ese no existe, arial.ttf.
[[nodiscard]] std::filesystem::path resolve_font_path(std::string_view name, bool bold, const FontDirs& dirs);

// (usWinAscent + usWinDescent) / unitsPerEm de un TTF (hhea si no hay OS/2 completa);
// 1 si no se puede leer o el resultado no vale.
[[nodiscard]] double font_line_height_em(const std::filesystem::path& file);

// Tamaño ASS = tamaño del editor (em) × este factor. Un nombre vacío es Arial. Recuerda el
// resultado de cada archivo (las fuentes no cambian mientras corre la app).
[[nodiscard]] double ass_size_factor(std::string_view name, bool bold, const FontDirs& dirs);

} // namespace gc::core
