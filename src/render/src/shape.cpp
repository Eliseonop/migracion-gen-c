#include "gc/render/shape.hpp"

#include <algorithm>
#include <cmath>

#include "gc/core/shape_geometry.hpp"
#include "include/core/SkCanvas.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkRect.h"

#include "canvas_impl.hpp"

namespace gc::render {

namespace {

SkPath path_of(const core::Polyline& pts, bool close) {
    SkPathBuilder b;
    for (std::size_t i = 0; i < pts.size(); ++i) {
        const SkPoint p{static_cast<float>(pts[i].x), static_cast<float>(pts[i].y)};
        if (i == 0) {
            b.moveTo(p);
        } else {
            b.lineTo(p);
        }
    }
    if (close) {
        b.close();
    }
    return b.detach();
}

// El antialias de OpenCV (el export rasteriza al doble y reduce con INTER_AREA) ensancha lo que
// dibuja; medido con cv2 4.x, en px del doble: una línea de grosor t mide 2·⌊(t + 1)/2⌋ + 1,3
// (1,25 con t = 1; 1,43 en horizontal y 1,2 en diagonal), un relleno crece 0,72 por lado y un
// círculo de radio r se ve de r + 0,5. En la salida, la mitad. Sin esto, los bordes de las figuras
// quedan medio píxel por dentro (33–37 dB).
double opencv_line_width(double width_out) {
    const double t = std::round(width_out * 2);
    return (t <= 1 ? 1.25 : 2 * std::floor((t + 1) / 2) + 1.3) / 2;
}
constexpr double kOpenCvFillGrow = 0.72 / 2;
constexpr double kOpenCvDotGrow = 0.5 / 2;

SkColor rgb(core::ShapeRgb c, double alpha = 1.0) {
    const auto a = static_cast<U8CPU>(std::lround(std::clamp(alpha, 0.0, 1.0) * 255));
    return SkColorSetARGB(a, c.r, c.g, c.b);
}

} // namespace

Result<void> draw_shape(Canvas& canvas, const core::FrameLayer& layer,
                        const std::optional<core::AffineColorMatrix>& color) {
    const core::TimelineClip& clip = *layer.clip;
    const int width = canvas.width();
    const int height = canvas.height();
    auto draw = core::shape_draw_at(clip, layer.local_t);
    if (!draw) {
        return std::unexpected(draw.error());
    }
    // La pose de la capa, sin su opacidad: va con la de la aparición en layer.alpha.
    const core::ShapePose pose{layer.pose.x, layer.pose.y, layer.pose.scale, layer.pose.rotation, 1.0};
    auto plan = core::shape_plan(clip.shape.value_or(Json()), pose, *draw, core::clip_flip(clip), width, height);
    if (!plan) {
        return std::unexpected(plan.error());
    }
    const double alpha = std::isnan(layer.alpha) ? 1.0 : std::clamp(layer.alpha, 0.0, 1.0);

    SkCanvas* sk = CanvasAccess::sk(canvas);
    sk->save();
    // Aparición y salida sobre el cuadro entero (el PNG de la figura en el export): deslizar, la
    // cortinilla (en el cuadro ya escalado) y el zoom desde el centro.
    const core::ClipFx& fx = layer.fx;
    const auto w = static_cast<float>(width);
    const auto h = static_cast<float>(height);
    sk->translate(static_cast<float>(fx.tx) * w, static_cast<float>(fx.ty) * h);
    if (fx.wipe < 1.0) {
        sk->clipRect(SkRect::MakeWH(static_cast<float>(std::max(0.0, fx.wipe)) * w, h));
    }
    if (fx.scale != 1.0) {
        sk->translate(w / 2, h / 2);
        sk->scale(static_cast<float>(fx.scale), static_cast<float>(fx.scale));
        sk->translate(-w / 2, -h / 2);
    }
    // Toda la figura en una capa: su opacidad y sus filtros, de una vez.
    SkPaint group;
    group.setAlphaf(static_cast<float>(alpha * plan->opacity));
    if (color) {
        group.setColorFilter(color_filter(*color));
    }
    sk->saveLayer(nullptr, &group);
    SkPaint stroke;
    stroke.setAntiAlias(true);
    stroke.setStyle(SkPaint::kStroke_Style);
    stroke.setStrokeWidth(static_cast<float>(opencv_line_width(plan->line_width)));
    stroke.setStrokeCap(SkPaint::kRound_Cap);
    stroke.setStrokeJoin(SkPaint::kRound_Join);
    stroke.setColor(rgb(plan->stroke));
    for (const core::ShapeOp& op : plan->ops) {
        switch (op.kind) {
        case core::ShapeOp::Kind::Fill: {
            // Relleno y su margen de OpenCV de una vez (un trazo encima doblaría el alfa).
            SkPaint fill;
            fill.setAntiAlias(true);
            fill.setStyle(SkPaint::kStrokeAndFill_Style);
            fill.setStrokeWidth(static_cast<float>(2 * kOpenCvFillGrow));
            fill.setStrokeJoin(SkPaint::kRound_Join);
            fill.setColor(rgb(plan->fill, op.alpha));
            sk->drawPath(path_of(op.points, true), fill);
            break;
        }
        case core::ShapeOp::Kind::Line:
            sk->drawPath(path_of(op.points, op.closed), stroke);
            break;
        case core::ShapeOp::Kind::Dot: {
            SkPaint dot;
            dot.setAntiAlias(true);
            dot.setColor(rgb(plan->stroke));
            sk->drawCircle(static_cast<float>(op.center.x), static_cast<float>(op.center.y),
                           static_cast<float>(op.radius + kOpenCvDotGrow), dot);
            break;
        }
        }
    }
    sk->restore(); // la capa de la figura
    sk->restore();
    return {};
}

} // namespace gc::render
