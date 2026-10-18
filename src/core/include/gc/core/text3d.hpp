#pragma once

// Texto 3D: giro en X (inclinar) e Y (girar) alrededor del centro de la caja, con perspectiva.
// Espejo de backend/app/text3d.py y de text_warp_spec / text_is_3d de text_ass.py.
//
// El texto se dibuja plano (el .ass no lleva \frx ni \fry) en una capa transparente del cuadro;
// el export deforma después una región de esa capa con el filtro `perspective` de FFmpeg
// (sense=destination): cada esquina de la región va a su proyección. Las esquinas se escriben con
// 6 decimales (clipper._fnum), que es lo que lee FFmpeg; aquí ya van redondeadas así.
//
// Convenciones (y hacia abajo, como la pantalla): rot_x > 0 aleja la parte de ARRIBA del texto;
// rot_y > 0 aleja la DERECHA.

#include <array>
#include <cstdint>
#include <optional>
#include <utility>
#include <vector>

#include "gc/core/json.hpp"
#include "gc/core/model.hpp"
#include "gc/core/text_style.hpp"

namespace gc::core {

inline constexpr double kText3dMaxAngle = 75.0;

// Distancia de la cámara al plano: 0 = casi plano (4·alto), 0,5 = el alto, 1 = muy marcada.
[[nodiscard]] double text3d_focal(double frame_h, const Json& perspective);

// (rot_x, rot_y) acotados a ±75°, o nada si el texto no está girado en 3D (los dos < 0,05°).
[[nodiscard]] std::optional<std::pair<double, double>> text3d_angles(double rot_x, double rot_y);
[[nodiscard]] std::optional<std::pair<double, double>> text3d_angles(const Json& props);

// Punto (X, Y) del plano del texto → (X', Y', w); w = (f + z)/f, > 0 si queda delante.
struct Projected {
    double x = 0;
    double y = 0;
    double w = 1;
};
[[nodiscard]] Projected text3d_project(double cx, double cy, double rx, double ry, double f, double x, double y);

// Semiancho y semialto (px) que ocupa el texto alrededor de su centro, con margen (estimación
// generosa: el reparto real en líneas lo hace libass).
[[nodiscard]] std::pair<double, double> text3d_half_extents(const Json& style, std::string_view text, double width,
                                                             double height, double font_px,
                                                             const std::optional<TextShadow>& shadow);

// Pose de un fotograma: centro, ángulos, focal y semiextensión.
struct Text3dPose {
    double cx = 0;
    double cy = 0;
    double rx = 0;
    double ry = 0;
    double f = 1;
    double hx = 0;
    double hy = 0;
};

// Región (x0, y0, x1, y1) de la capa que se deforma, en px enteros: contiene el texto en su sitio
// y proyectado en todos los fotogramas, acotada a dos cuadros alrededor del lienzo.
struct WarpRegion {
    std::int64_t x0 = 0;
    std::int64_t y0 = 0;
    std::int64_t x1 = 0;
    std::int64_t y1 = 0;
};
[[nodiscard]] WarpRegion text3d_warp_region(const std::vector<Text3dPose>& poses, double width, double height);

// Destino de las 4 esquinas de la región (arriba-izq, arriba-der, abajo-izq, abajo-der), relativo
// a su origen, sin redondear.
using Corners = std::array<std::pair<double, double>, 4>;
[[nodiscard]] Corners text3d_region_corners(const Text3dPose& p, const WarpRegion& region);

// (fotograma de la composición, t local) de cada fotograma en que se ve el texto (_text_frames).
[[nodiscard]] std::vector<std::pair<std::int64_t, double>> text_frames(const TimelineClip& clip, double fps);

// ¿Está el texto girado en 3D en algún momento? (va en capa propia)
[[nodiscard]] bool text_is_3d(const TimelineClip& clip, double fps);

// Deformación de la capa de un texto (text_warp_spec): la región, los fotogramas y las esquinas
// de cada uno (con los 6 decimales de _fnum). `style` es el estilo efectivo (pista + clip).
// Nada si no es 3D en ningún fotograma.
struct TextWarp {
    WarpRegion region;
    std::vector<std::int64_t> frames;
    std::vector<Corners> corners;

    // Las esquinas del fotograma `k` de la composición, como la expresión de `perspective`
    // (eval=frame): las del primer fotograma de la lista ≥ k, o las del último.
    [[nodiscard]] const Corners& corners_at(std::int64_t k) const;
};
[[nodiscard]] std::optional<TextWarp> text_warp_spec(const TimelineClip& clip, const Json& style, double width,
                                                     double height, double fps);

// ¿Va el texto en capa propia en el export? (máscara, giro 3D o modo de fusión: _prepare_texts)
[[nodiscard]] bool text_own_layer(const TimelineClip& clip, double fps);

} // namespace gc::core
