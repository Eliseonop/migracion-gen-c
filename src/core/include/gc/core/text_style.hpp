#pragma once

// Estilo de un clip de texto: los cálculos puros de text_ass.py de video-yt (estilo efectivo,
// opacidades, colores ASS, Fondo, sombra paralela, Brillo, Curva, posición y May./min.).
//
// El estilo es el JSON del proyecto tal cual: cada campo se lee con las reglas de Python
// (float("0.5"), «if x:», un NaN que no cuenta…), para dar lo mismo que el export. Donde
// Python lanzaría una excepción (un color que no es texto, un ángulo de sombra infinito), aquí
// se toma la salida más segura y se dice en cada función.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "gc/core/json.hpp"

namespace gc::core {

// Estilo efectivo: la pista pone la base y el clip la pisa campo a campo ({**pista, **clip}).
[[nodiscard]] Json effective_text_style(const Json& track_style, const Json& clip_style);

// Efectos de la palabra activa (word_fx: uno, una lista o "none").
struct WordFx {
    bool glow = false;
    bool highlight = false;
    bool pop = false;
    [[nodiscard]] bool any() const { return glow || highlight || pop; }
};
[[nodiscard]] WordFx word_fx_set(const Json& style);

// _clamp01: float(v) acotado a [0, 1]; lo que no es número da `fallback` y un NaN da 1.
[[nodiscard]] double text_clamp01(const Json& value, double fallback);
// Opacidad del texto y la de una palabra (activa o no) sobre ella.
[[nodiscard]] double style_opacity(const Json& style);
[[nodiscard]] double word_opacity(const Json& style, bool active);
// Alfa ASS de una opacidad: «00» opaco, «FF» transparente (redondeo al par).
[[nodiscard]] std::string alpha_hex(double opacity);
// Color ASS «&HAABBGGRR&» de un «#rgb» / «#rrggbb» (lo que no se entiende es blanco).
[[nodiscard]] std::string ass_bgr(std::string_view hex_color, std::string_view alpha = "00");
// Texto de un color del estilo: el valor si es texto no vacío; si no, `fallback`. (Un color
// que no es texto hace fallar a video-yt; aquí vale `fallback`.)
[[nodiscard]] std::string style_color(const Json& style, std::string_view key, std::string_view fallback);

// Escala X / Y del texto sobre su Escala (solo con la Escala uniforme apagada), 0,01 a 100.
[[nodiscard]] std::pair<double, double> stretch_of(const Json& style);

// Color ASS (con alfa) de la caja de Fondo, o nada si no hay Fondo. (Un bg que no es texto
// hace fallar a video-yt; aquí no hay caja.)
[[nodiscard]] std::optional<std::string> box_color(const Json& style);
// ¿El Fondo va dibujado como forma (bloque, redondeado, margen, desplazamiento o animado)?
[[nodiscard]] bool box_drawn(const Json& style);

// Palabra activa repartiendo la duración a partes iguales (-1 = ninguna).
[[nodiscard]] std::int64_t active_word_index(std::int64_t word_count, double local_t, double duration);
// Palabras del texto (str.split()).
[[nodiscard]] std::vector<std::string> split_words(std::string_view text);
// May./min. de CapCut: "upper", "lower" o "title" (cada palabra con su primera letra en
// mayúscula y el resto en minúsculas); cualquier otro modo deja el texto igual.
[[nodiscard]] std::string apply_text_case(std::string_view text, const Json& mode);

// Silueta desenfocada debajo del texto (sombra paralela o halo del Brillo), en px.
struct TextShadow {
    std::string color;
    double opacity = 0;
    double dx = 0;
    double dy = 0;
    double sigma = 0;
    double spread = 0; // solo el Brillo «intenso»
};
[[nodiscard]] std::string glow_color(const Json& style);
[[nodiscard]] std::optional<TextShadow> text_glow(const Json& style, double font_px);
// El halo del Brillo si está activo; si no, la sombra paralela. (Con un ángulo infinito
// video-yt falla; aquí no hay sombra.)
[[nodiscard]] std::optional<TextShadow> text_shadow(const Json& style, double font_px);

// Centro de la caja del texto en px (x, y entre −10 y 11 del cuadro, redondeado al par).
[[nodiscard]] std::pair<std::int64_t, std::int64_t> text_xy(const Json& style, double width, double height);

// Fuerza de la Curva que se dibuja, −1 a 1 (0 = recto; con Fondo no hay curva).
[[nodiscard]] double curve_of(const Json& style);

} // namespace gc::core
