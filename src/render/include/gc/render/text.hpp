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

// Dibuja encima de lo que haya en el lienzo los textos del .ass global en el fotograma `k` de la
// timeline (a sus fps): los que libass dibujaría en ese instante, en el orden del .ass. Un texto
// visible que aún no se sabe dibujar da Unsupported; con `omit`, se salta y su motivo va a
// `warnings`. Los textos de capa propia (máscara, 3D, fusión) o de una pista con vídeo encima
// también son Unsupported por ahora.
[[nodiscard]] Result<void> draw_timeline_texts(Canvas& canvas, const core::Timeline& timeline, std::int64_t k,
                                               const TextFonts& fonts, bool omit = false,
                                               std::vector<std::string>* warnings = nullptr);

} // namespace gc::render
