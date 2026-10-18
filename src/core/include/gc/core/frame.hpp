#pragma once

// El fotograma de un instante: qué se dibuja de cada capa y dónde (espejo de drawComposite
// de frontend/src/features/editor/render/canvas.js, que dibuja el preview).
//
// Por cada clip visible (layers_at) y cada una de sus pasadas (adjust_passes: la base y,
// con máscara de ajuste, el fantasma), una FrameLayer con lo que el render necesita:
// tiempo local y de fuente, pose (render_pose, con las animaciones), aparición/salida
// (clip_fx_at), opacidad final, fusión, volteo y máscaras. Para el vídeo, la imagen y el
// motion con dimensiones conocidas, además, los «blits»: qué rectángulo de la fuente se
// dibuja y con qué transformación afín llega al cuadro de salida, en el MISMO orden de
// operaciones que el contexto 2D del preview (traslada, gira y escala), así que los
// números coinciden bit a bit salvo lo que pasa por sin/cos. El texto, las figuras y la
// capa de ajuste no llevan blits: los dibujan sus propios módulos del render.
//
//   overlay  recorte (crop_window → source_crop_px) colocado según la pose: centro en
//            (x·W, y·H), tamaño recorte × escala, girado, y escalado por la aparición
//            (fx.scale) y desplazado fx.tx/ty × el destino. La cortinilla recorta el
//            destino SIN girar.
//   relleno  el encuadre (reframe o los keyframes cx/cy/zoom) cubre el cuadro entero
//            (contain: la fuente entera encajada) y después la pose mueve, gira y escala
//            el cuadro entero; con doble encuadre, dos mitades. La cortinilla recorta el
//            cuadro.
//
// Con kPreviewLayers se reproduce el preview tal cual, también su atajo: en relleno, una
// escala que solo se aparta de 1 en 0,001 o menos no se aplica. kRenderLayers la aplica.

#include <array>
#include <functional>
#include <optional>
#include <string_view>
#include <vector>

#include "gc/core/clip_fx.hpp"
#include "gc/core/clip_layout.hpp"
#include "gc/core/cow.hpp"
#include "gc/core/error.hpp"
#include "gc/core/json.hpp"
#include "gc/core/layers.hpp"
#include "gc/core/model.hpp"
#include "gc/core/pose.hpp"

namespace gc::core {

// Transformación afín del contexto 2D: x' = a·x + c·y + e, y' = b·x + d·y + f.
struct Affine {
    double a = 1.0;
    double b = 0.0;
    double c = 0.0;
    double d = 1.0;
    double e = 0.0;
    double f = 0.0;
};
// m · n (primero n, después m), con las mismas operaciones que el contexto 2D.
[[nodiscard]] Affine operator*(const Affine& m, const Affine& n);
[[nodiscard]] Affine translated(const Affine& m, double x, double y);
[[nodiscard]] Affine rotated(const Affine& m, double radians);
[[nodiscard]] Affine scaled(const Affine& m, double x, double y);
[[nodiscard]] Point apply(const Affine& m, double x, double y);

// Un rectángulo en px: origen (x, y) arriba a la izquierda, ancho y alto.
struct Box {
    double x = 0.0;
    double y = 0.0;
    double w = 0.0;
    double h = 0.0;
};

// Un dibujo del material: el rectángulo `src` (px de la fuente) llevado al cuadro de
// salida por `m` (px de la fuente → px del cuadro). `m` es `place` · (`src` → `dst`): `dst`
// es el destino del clip antes de su giro, su volteo y el zoom de aparición (en overlay, el
// recorte por la escala de la pose, centrado en 0; en relleno, el cuadro o su mitad, antes de
// la pose), y `place` lo lleva al cuadro. Los efectos de clip trabajan a ese tamaño, como el
// export (que los aplica tras escalar el clip y antes de girarlo).
struct Blit {
    SourceCrop src;
    Affine m;
    Box dst;
    Affine place;
};

struct FrameLayer {
    Cow<TimelineClip> clip; // la pasada que se dibuja (el fantasma lleva «__adj» en el id)
    bool ghost = false;
    double local_t = 0.0;   // s desde el inicio del clip
    double src_t = 0.0;     // s de la fuente, dentro de [in_point, out_point]
    Pose pose;              // render_pose
    ClipFx fx;
    double alpha = 1.0;     // fx.opacity × pose.opacity
    std::string_view blend = "normal";
    Flip flip;
    std::vector<Json> masks; // clip_masks_at: las que recortan
    std::vector<Blit> blits; // vídeo, imagen y motion con dimensiones (2 con doble encuadre)
    std::optional<std::array<Point, 4>> wipe; // recorte de la cortinilla, en px del cuadro
};

struct MediaSize {
    double w = 0.0;
    double h = 0.0;
};
// Dimensiones del material de un clip (nullopt = no se conocen: la capa va sin blits).
using MediaSizeFn = std::function<std::optional<MediaSize>(const TimelineClip&)>;

// Dimensiones de salida tal cual: el export las hace pares (compose.py) y el doble encuadre
// usa H//2; quien llame para exportar tiene que pasarlas ya pares.
struct FrameSpec {
    double out_w = 0.0;
    double out_h = 0.0;
    LayerRules rules = kRenderLayers;
};

// Tiempo de la fuente de un clip en `t` (s de la timeline), con `start` como inicio
// efectivo: in_point + (t − start)·velocidad (al revés, desde out_point), acotado a
// [in_point, out_point].
[[nodiscard]] double source_time(const TimelineClip& clip, double t, double start);

// El fotograma en `t`, de fondo a frente. Error si las dimensiones de salida no son
// números finitos mayores que 0, o (con las reglas del render) si las de un material no
// lo son: Python fallaría con ZeroDivisionError. Con las del preview esa capa va sin
// blits, como el preview, que no dibuja un material sin tamaño.
[[nodiscard]] Result<std::vector<FrameLayer>> evaluate(const Timeline& timeline, double t, const FrameSpec& spec,
                                                       const MediaSizeFn& media);

} // namespace gc::core
