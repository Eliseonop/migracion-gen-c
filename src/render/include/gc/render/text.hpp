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

    // Ancho de un texto como font_metrics.text_width de video-yt (Pillow): la fuente a `em` px
    // redondeado a 1/4 de px, con kerning, más `letter_spacing` por carácter. Sin la fuente, la
    // estimación de 0,55 em por carácter.
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

// Lo que mide Pillow de `text` en `font` a `size` px (ImageFont.truetype con su maquetación básica
// de FreeType, la de video-yt; la usa la máscara de tipo texto): getmetrics(), getlength() y dónde
// queda el origen de la línea (izquierda de la línea base) respecto al ancla de ImageDraw.text
// («ls», «mm», «lm» o «rm»). Las métricas y las anclas verticales, exactas (la aritmética de 26.6
// de FreeType); el ancho, con los avances redondeados de HarfBuzz en vez de los del hinting, que
// pueden diferir en 1 px. Nada si la fuente no se puede leer.
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
// temprano de la pista. Después, draw_timeline_texts pone encima el resto.
[[nodiscard]] Result<void> compose_with_buried_texts(Canvas& canvas, const core::Timeline& timeline,
                                                     const std::vector<core::FrameLayer>& layers, std::int64_t k,
                                                     const SourceFn& source, const TextFonts& fonts,
                                                     bool omit = false, std::vector<std::string>* warnings = nullptr);

} // namespace gc::render
