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
#include "gc/core/model.hpp"

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

// --- Geometría (con text_width de text_metrics) ----------------------------------------------

// Caja de Fondo: centro relativo al del bloque (en el eje del texto, sin girar) y tamaño, en px.
struct BoxRect {
    double x = 0;
    double y = 0;
    double w = 0;
    double h = 0;
};
// Una por línea con palabras (ancho del texto × alto de línea de la fuente) o una para todo el
// bloque, más el contorno, el margen de Alto / Ancho y el desplazamiento. `size_factor` es el
// ass_size_factor de la fuente del estilo.
[[nodiscard]] std::vector<BoxRect> box_rects(const Json& style, double width, double height,
                                             const std::vector<std::vector<std::string>>& rows, double size_factor);

// Una letra del texto curvo: su palabra (índice global), la letra, su centro relativo al del
// bloque (en el eje del texto) y su giro en radianes.
struct CurveGlyph {
    std::int64_t word = 0;
    std::string ch;
    double x = 0;
    double y = 0;
    double angle = 0;
};
[[nodiscard]] std::vector<CurveGlyph> curve_glyphs(const Json& style, double width, double height,
                                                   const std::vector<std::vector<std::string>>& rows);

// --- Un clip de texto en el tiempo ------------------------------------------------------------

// Duración del clip de texto: out_point − in_point (≥ 0).
[[nodiscard]] double text_clip_duration(const TimelineClip& clip);

// Pose del texto en `local_t`: la del render (keyframes, pistas anim y animaciones) más el giro
// 3D (rot_x, rot_y; de los keyframes o del estilo propio del clip).
struct TextPose {
    double x = 0.5;
    double y = 0.5;
    double scale = 1.0;
    double rotation = 0.0;
    double opacity = 1.0;
    double rot_x = 0.0;
    double rot_y = 0.0;
};
[[nodiscard]] TextPose text_pose_at(const TimelineClip& clip, double local_t);

// Estilo con la pose del clip en `local_t` (_posed_style): keyframes de estilo, x, y, escala,
// giros, volteo y la opacidad (la de los keyframes o la del estilo por las animaciones).
[[nodiscard]] Json posed_text_style(const TimelineClip& clip, const Json& style, double local_t);

// ¿Cambia la pose (o el estilo) del texto durante el clip? Muestrea a `fps` como el export.
[[nodiscard]] bool text_animates(const TimelineClip& clip, double fps);

// Ventana absoluta (t0, t1) de cada palabra: la de `words` si cuadran con el texto (activa hasta
// la siguiente) o la duración repartida a partes iguales.
[[nodiscard]] std::vector<std::pair<double, double>> word_windows(const TimelineClip& clip);
// Palabra activa en `local_t` de `word_count` palabras: la última de `words` que ya empezó, o
// repartiendo la duración si no cuadran.
[[nodiscard]] std::int64_t active_word_at(const TimelineClip& clip, std::int64_t word_count, double local_t,
                                          double duration);

// Lo que deja ver la aparición «escribir» con progreso 0–1 (los primeros caracteres).
[[nodiscard]] std::string typing_text(std::string_view text, double progress);

// Lo que se ve de un texto en `local_t` con su estilo con la pose: el texto (todo, o lo escrito
// con «escribir») y qué palabra va resaltada. Nada si «escribir» aún no muestra nada.
struct TextContent {
    std::string text;
    std::int64_t active = -1; // palabra activa (-1: ninguna)
    bool all_active = false;  // una sola palabra con efecto: activa todo el clip
    [[nodiscard]] bool is_active(std::int64_t word) const { return all_active || word == active; }
};
[[nodiscard]] std::optional<TextContent> text_content_at(const TimelineClip& clip, const Json& posed_style,
                                                         double local_t, double duration);

} // namespace gc::core
