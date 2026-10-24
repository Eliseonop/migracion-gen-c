#pragma once

// Las fuentes como las usa Pillow en video-yt: ImageFont.truetype con la maquetación básica (su
// Pillow no tiene raqm), hecha aquí con FreeType y las mismas llamadas que _imagingft.c:
//
// - getfont(): una petición NOMINAL de `size · 64` en 26.6, con `size` pasado a float de C y la
//   multiplicación en float, truncada a FT_Long de 32 bits (Windows). Si no cabe, MSVC da el entero
//   indefinido (INT_MIN): negativo, así que FT_Request_Size falla y Pillow lanza.
// - truetype(): si getfont() no puede (el archivo no existe o no es una fuente, o FreeType rechaza
//   el tamaño), busca un archivo con el mismo nombre en las fuentes del sistema (WINDIR\fonts en
//   Windows, con el orden de os.walk) y mide con ese; si tampoco, lanza.
// - text_layout_fallback(): un glifo por carácter (FT_Get_Char_Index, sin formas ni ligaduras),
//   cargado con FT_LOAD_DEFAULT (con hinting); su avance es horiAdvance. El kerning de la tabla
//   'kern' (FT_Get_Kerning con FT_KERNING_DEFAULT) se suma al avance del glifo anterior como lo
//   suma Pillow: PIXEL(delta.x), los px del kerning en unidades de 26.6 (1/64 de px por px).
//
// Con la FreeType del Pillow de video-yt (2.14.3, la misma que trae vcpkg), el resultado es el de
// Pillow bit a bit.

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace gc::render::detail {

// Lo que maqueta Pillow de un texto en una fuente y un tamaño.
struct PillowLayout {
    std::vector<std::uint32_t> glyphs;  // el glifo de cada carácter (0 si la fuente no lo tiene)
    std::vector<std::int64_t> advances; // su avance en 26.6, con el kerning de Pillow
    std::int64_t length = 0;            // la suma de los avances: getlength() · 64
    std::int64_t ascender = 0;          // size->metrics.ascender, 26.6 (ya en px enteros)
    std::int64_t descender = 0;         // size->metrics.descender, 26.6 (negativo)
};

// Las fuentes abiertas con FreeType, una vez por archivo. Se puede usar desde varios hilos.
class PillowFonts {
public:
    PillowFonts();
    ~PillowFonts();
    PillowFonts(const PillowFonts&) = delete;
    PillowFonts& operator=(const PillowFonts&) = delete;
    PillowFonts(PillowFonts&&) = delete;
    PillowFonts& operator=(PillowFonts&&) = delete;

    // ImageFont.truetype(path, size) y la maquetación de `text`. Nada si Pillow lanzaría: la
    // fuente no se abre, el tamaño no vale, un glifo no carga o el texto pasa de MAX_STRING_LENGTH
    // caracteres (1 000 000).
    [[nodiscard]] std::optional<PillowLayout> layout(const std::filesystem::path& path, double size,
                                                     std::string_view text);

    // getlength() · 64 de lo mismo, guardado para la próxima vez (el render mide los mismos textos
    // en cada fotograma).
    [[nodiscard]] std::optional<std::int64_t> length(const std::filesystem::path& path, double size,
                                                     std::string_view text);

    // La versión de la FreeType con la que se mide («2.14.3»).
    [[nodiscard]] static std::string freetype_version();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// PIXEL de _imagingft.c: de 26.6 a px enteros, con las mitades hacia arriba.
[[nodiscard]] constexpr std::int64_t ft_pixel(std::int64_t v) noexcept {
    return (v + 32) >> 6;
}

} // namespace gc::render::detail
