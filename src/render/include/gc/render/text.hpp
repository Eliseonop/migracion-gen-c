#pragma once

// Texto en el render: los textos que el export pone en el .ass global (text_ass.build_ass,
// dibujado por libass encima de todo el vídeo) dibujados con Skia y HarfBuzz, con los mismos TTF.
//
// Como libass: el tamaño de la fuente es el alto de línea OS/2 (ass_size_factor), cada línea va
// anclada en su \pos (centro, izquierda o derecha y centro vertical de la línea), la forma de
// cada línea la da HarfBuzz, y en cada evento van primero todos los contornos y después todos
// los rellenos. Lo que aún no se dibuja (Fondo, sombra, Brillo, Curva, giro, texto animado…) da
// Unsupported con el motivo.

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "gc/core/error.hpp"
#include "gc/core/font_metrics.hpp"
#include "gc/core/json.hpp"
#include "gc/core/model.hpp"
#include "gc/core/text_metrics.hpp"
#include "gc/render/canvas.hpp"
#include "gc/render/compose.hpp"

namespace gc::render {

// Las fuentes del texto: cada TTF de core::resolve_font_path cargado una vez para Skia y HarfBuzz.
class TextFonts {
public:
    explicit TextFonts(core::FontDirs dirs);
    TextFonts(TextFonts&&) noexcept;
    TextFonts& operator=(TextFonts&&) noexcept;
    TextFonts(const TextFonts&) = delete;
    TextFonts& operator=(const TextFonts&) = delete;
    ~TextFonts();

    [[nodiscard]] const core::FontDirs& dirs() const noexcept;

    // Ancho de un texto como font_metrics.text_width de video-yt: el getlength() de Pillow (su
    // maquetación básica, con el hinting y el kerning de FreeType) a `em` px redondeado a 1/4 de
    // px, más `letter_spacing` por carácter; exacto, con la misma FreeType que ese Pillow. Si Pillow
    // no puede medir (sin la fuente, o un tamaño o un glifo que FreeType rechaza), la estimación de
    // 0,55 em por carácter, como text_width. Se puede llamar desde varios hilos.
    [[nodiscard]] double text_width(std::string_view font, bool bold, double em, std::string_view text,
                                    double letter_spacing) const;

    // Esta medida como core::TextWidthFn (con ella se reparten las líneas, como en el export).
    // draw_timeline_texts la pone solo mientras dibuja (core::ScopedTextWidth).
    [[nodiscard]] core::TextWidthFn width_fn() const;

private:
    struct Impl;
    friend struct TextFontsAccess;
    std::shared_ptr<Impl> impl_;
};

// La versión de FreeType con la que mide TextFonts («2.14.3», la del Pillow de video-yt).
[[nodiscard]] std::string freetype_version();

// Lo que mide Pillow de `text` en `font` a `size` px (ImageFont.truetype con su maquetación básica
// de FreeType, la de video-yt; la usa la máscara de tipo texto): getmetrics(), getlength() y dónde
// queda el origen de la línea (izquierda de la línea base) respecto al ancla de ImageDraw.text
// («ls», «mm», «lm» o «rm»). Todo exacto (la misma FreeType con las mismas llamadas). Nada si
// Pillow no puede medir: la fuente no se abre, o FreeType rechaza el tamaño o un glifo.
struct PillowText {
    int ascent = 0;
    int descent = 0;
    double length = 0;
    double origin_x = 0;
    double origin_y = 0;
};
[[nodiscard]] std::optional<PillowText> pillow_text(const TextFonts& fonts, std::string_view font, bool bold,
                                                    int size, std::string_view text, std::string_view anchor);

// Dibuja encima de lo que haya en el lienzo los textos que el export pone encima de todo el vídeo
// en el fotograma `k` de la timeline (a sus fps): primero los de capa propia (máscara, 3D o
// fusión) por orden de inicio y después los del .ass global, los que libass dibujaría en ese
// instante, en su orden. Un texto visible que aún no se sabe dibujar da Unsupported; con `omit`,
// se salta y su motivo va a `warnings`. No dibuja los de una pista con vídeo encima, que van en la
// cadena de vídeo (compose_with_buried_texts).
[[nodiscard]] Result<void> draw_timeline_texts(Canvas& canvas, const core::Timeline& timeline, std::int64_t k,
                                               const TextFonts& fonts, bool omit = false,
                                               std::vector<std::string>* warnings = nullptr);

// El compuesto del fotograma `k` como el export: compose_into (las capas sin los textos) con los
// textos de las pistas de texto que tienen vídeo encima (buried_text_track_ids) metidos en la
// cadena de vídeo, por (capa de la pila, inicio, índice en la lista), como build_command: cada
// texto de capa propia en su sitio y los demás de la pista en una capa, en el sitio del clip más
// temprano de la pista. Después, draw_timeline_texts pone encima el resto. `cache`, la de
// compose_into (solo se usa si no hay textos en la cadena).
[[nodiscard]] Result<void> compose_with_buried_texts(Canvas& canvas, const core::Timeline& timeline,
                                                     const std::vector<core::FrameLayer>& layers, std::int64_t k,
                                                     const SourceFn& source, const TextFonts& fonts,
                                                     bool omit = false, std::vector<std::string>* warnings = nullptr,
                                                     LayerCache* cache = nullptr);

} // namespace gc::render
