#pragma once

// Medidas del texto que la lógica de edición necesita: tamaño de letra, interlineado y
// reparto en líneas (espejo de _font_px, _line_height y _rows de text_ass.py).
//
// El ancho de un texto depende de la fuente. Hasta F4 (FreeType), text_width usa una
// estimación: 0,55 em por carácter más el espaciado entre letras. Es la misma que
// inyectan las fixtures en video-yt, así que el resto de la lógica se compara exacta.
// La app puede poner la medida real con set_text_width.

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "gc/core/json.hpp"

namespace gc::core {

// Ancho en px de `text` con esa fuente a `em` px y `letter_spacing` px entre letras.
using TextWidthFn = std::function<double(std::string_view font, bool bold, double em, std::string_view text,
                                         double letter_spacing)>;

// nullptr = la estimación por defecto.
void set_text_width(TextWidthFn fn);
[[nodiscard]] double text_width(std::string_view font, bool bold, double em, std::string_view text,
                                double letter_spacing);

// Pone `fn` como medida mientras vive y, al destruirse, repone la que hubiera antes (también la
// estimación). Así quien dibuja con la medida real no la deja puesta para el resto del proceso.
// Vale solo en el hilo que la pone, y ahí manda sobre la de set_text_width: dos renders en hilos
// distintos, cada uno con su guarda, no se pisan.
class ScopedTextWidth {
public:
    explicit ScopedTextWidth(TextWidthFn fn);
    ~ScopedTextWidth();
    ScopedTextWidth(const ScopedTextWidth&) = delete;
    ScopedTextWidth& operator=(const ScopedTextWidth&) = delete;
    ScopedTextWidth(ScopedTextWidth&&) = delete;
    ScopedTextWidth& operator=(ScopedTextWidth&&) = delete;

private:
    std::optional<std::shared_ptr<const TextWidthFn>> previous_;
};

// Valores del estilo de un texto ({size, scale, letter_spacing, line_height, w, font, bold}).
[[nodiscard]] double text_style_scale(const Json& style);                  // escala (> 0; si no, 1)
[[nodiscard]] double text_font_px(const Json& style, double height);       // em en px
[[nodiscard]] double text_letter_spacing_px(const Json& style, double em); // −0,5 a 1 em
[[nodiscard]] double text_line_height(const Json& style);                  // 0,6 a 3 em

// Palabras de cada línea, como wrapWordRows del editor: caja de ancho `w` × escala menos
// 0,4 em, y cada salto de línea del texto empieza una línea (vacía si no tiene palabras).
[[nodiscard]] std::vector<std::vector<std::string>> text_rows(const Json& style, double width, double height,
                                                              std::string_view text);

} // namespace gc::core
