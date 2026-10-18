#include "gc/render/compose.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "gc/core/bg.hpp"
#include "gc/core/clip_adjust.hpp"
#include "gc/core/clip_effects.hpp"
#include "gc/core/clip_filters.hpp"
#include "gc/core/masks.hpp"
#include "gc/render/effects.hpp"
#include "gc/render/shape.hpp"

namespace gc::render {

namespace {

bool is_media(const core::TimelineClip& clip) {
    return clip.kind == "video" || clip.kind == "image" || clip.kind == "motion";
}

bool is_adjustment(const core::TimelineClip& clip) { return clip.kind == "adjustment"; }

bool is_shape(const core::TimelineClip& clip) { return clip.kind == "shape"; }

// Lo de «Eliminar fondo» que el render aún no hace: el matte automático listo (auto_active) y,
// con el croma activo, el contorno (outline_params: ancho y opacidad mayores que 0). El croma
// solo ya lo hace el decode (export_frame_at).
std::optional<std::string> bg_unsupported(const core::TimelineClip& clip) {
    if (!clip.bg_removal || !core::bg_capable(clip)) {
        return std::nullopt;
    }
    const Json bg = core::normalize_bg(*clip.bg_removal);
    if (core::bg_auto_active(bg)) {
        return std::string("el render aún no aplica el matte automático de «Eliminar fondo»");
    }
    if (core::bg_chroma_active(bg)) {
        const Json& o = bg["outline"];
        if (o.value("enabled", false) && o.value("width", 0.0) > 0 && o.value("opacity", 0.0) > 0) {
            return std::string("el render aún no dibuja el contorno de «Eliminar fondo»");
        }
    }
    return std::nullopt;
}

// El grano de un fotograma: cambia con el instante del clip y no con la pasada (la base y el
// fantasma de la máscara de ajuste llevan el mismo, como las dos cadenas del export, que
// empiezan con la misma semilla). FNV-1a del id, que no depende de la plataforma.
std::uint64_t grain_seed(const core::FrameLayer& layer, std::size_t blit) {
    std::string_view id = layer.clip->id;
    if (layer.ghost && id.ends_with(core::kGhostSuffix)) {
        id.remove_suffix(core::kGhostSuffix.size());
    }
    std::uint64_t h = 0xCBF29CE484222325ULL;
    for (const char c : id) {
        h = (h ^ static_cast<unsigned char>(c)) * 0x100000001B3ULL;
    }
    return h ^ std::bit_cast<std::uint64_t>(layer.local_t) ^ (blit * 0x9E3779B97F4A7C15ULL);
}

// Un blit con efectos de clip (effects.hpp): el recorte se dibuja primero en una imagen del
// tamaño de su destino (blit.dst: antes del giro, del volteo y del zoom de aparición), con los
// filtros de color, como el export, que escala el clip, le pasa el `look` y los efectos y
// después lo gira. Después, los efectos, y esa imagen va al cuadro con el resto de la
// colocación (blit.place), la opacidad y la cortinilla. Un destino enorme (más de 4 veces el
// cuadro) se reduce a esa área para no reservar memoria sin límite.
Result<void> draw_with_effects(Canvas& canvas, const Image& image, const core::Blit& blit,
                               const core::FrameLayer& layer, const std::optional<core::AffineColorMatrix>& color,
                               const core::ClipEffects& fx, std::size_t index) {
    const core::Box& dst = blit.dst;
    if (!(std::isfinite(dst.w) && std::isfinite(dst.h) && dst.w > 0 && dst.h > 0)) {
        return {};
    }
    double w = std::max(1.0, std::round(dst.w));
    double h = std::max(1.0, std::round(dst.h));
    const double cap = 4.0 * canvas.width() * canvas.height();
    if (w * h > cap) {
        const double k = std::sqrt(cap / (w * h));
        w = std::max(1.0, std::round(w * k));
        h = std::max(1.0, std::round(h * k));
    }
    auto clip = Canvas::make(static_cast<int>(w), static_cast<int>(h));
    if (!clip) {
        return std::unexpected(clip.error());
    }
    clip->clear({0, 0, 0, 0});
    const core::SourceCrop& src = blit.src;
    const core::Affine to_clip{w / src.sw, 0.0, 0.0, h / src.sh, -src.sx * w / src.sw, -src.sy * h / src.sh};
    if (auto drawn = clip->draw_image(image, src, to_clip, 1.0, std::nullopt, color); !drawn) {
        return drawn;
    }
    Image pixels = clip->snapshot();
    apply_effects(pixels, fx, grain_seed(layer, index));
    const core::Affine to_dst{dst.w / w, 0.0, 0.0, dst.h / h, dst.x, dst.y};
    return canvas.draw_image(pixels, {0.0, 0.0, w, h}, blit.place * to_dst, layer.alpha, layer.wipe);
}

// Una figura con efectos: el export la rasteriza en un PNG del cuadro (con su opacidad) y le
// pasa la cadena del clip; aquí, dibujada en una capa transparente del tamaño del cuadro, que
// después va encima.
Result<void> draw_shape_with_effects(Canvas& canvas, const core::FrameLayer& layer,
                                     const std::optional<core::AffineColorMatrix>& color,
                                     const core::ClipEffects& fx) {
    auto shape = Canvas::make(canvas.width(), canvas.height());
    if (!shape) {
        return std::unexpected(shape.error());
    }
    shape->clear({0, 0, 0, 0});
    if (auto drawn = draw_shape(*shape, layer, color); !drawn) {
        return drawn;
    }
    Image pixels = shape->snapshot();
    apply_effects(pixels, fx, grain_seed(layer, 0));
    const double w = canvas.width();
    const double h = canvas.height();
    return canvas.draw_image(pixels, {0.0, 0.0, w, h}, core::Affine{});
}

} // namespace

std::optional<std::string> unsupported_reason(const core::FrameLayer& layer) {
    const core::TimelineClip& clip = *layer.clip;
    if (is_adjustment(clip)) {
        return std::nullopt;
    }
    if (!is_media(clip) && !is_shape(clip)) {
        return std::format("el render aún no dibuja capas de tipo «{}»", clip.kind);
    }
    if (auto reason = core::maskable(clip) ? mask_unsupported(layer.masks) : std::string(); !reason.empty()) {
        return reason;
    }
    if (auto reason = bg_unsupported(clip)) {
        return reason;
    }
    if (clip.vfx && clip.vfx->is_array() && !clip.vfx->empty()) {
        return std::string("el render aún no aplica efectos de vídeo");
    }
    return std::nullopt;
}

Result<Image> compose_frame(const std::vector<core::FrameLayer>& layers, int width, int height,
                            const SourceFn& source) {
    auto canvas = Canvas::make(width, height);
    if (!canvas) {
        return std::unexpected(canvas.error());
    }
    if (auto ok = compose_into(*canvas, layers, source); !ok) {
        return std::unexpected(ok.error());
    }
    return canvas->snapshot();
}

Result<void> compose_into(Canvas& canvas, const std::vector<core::FrameLayer>& layers, const SourceFn& source) {
    const int width = canvas.width();
    const int height = canvas.height();
    canvas.clear({0, 0, 0, 255});
    for (const core::FrameLayer& layer : layers) {
        const std::string& id = layer.clip->id;
        if (auto reason = unsupported_reason(layer)) {
            return fail(ErrorCode::Unsupported, std::format("{} (clip {})", *reason, id));
        }
        // Capa de ajuste (compose.py, #19): su matriz de color sobre todo lo compuesto debajo
        // durante su tramo, sin material ni máscaras (el export no las aplica).
        if (is_adjustment(*layer.clip)) {
            if (const auto m = core::adjustment_matrix(*layer.clip)) {
                canvas.apply_color_matrix(*m);
            }
            continue;
        }
        // Una figura no tiene material: se dibuja su geometría (shape.hpp).
        const bool shape = is_shape(*layer.clip);
        const Image* image = nullptr;
        if (!shape) {
            if (layer.blits.empty()) {
                // Con las reglas del preview, evaluate deja sin blits el material sin tamaño.
                return fail(ErrorCode::InvalidArgument,
                            std::format("el clip {} no tiene las dimensiones de su material", id));
            }
            auto got = source(layer);
            if (!got) {
                return std::unexpected(got.error());
            }
            if (*got == nullptr) {
                return fail(ErrorCode::Internal, std::format("no hay fotograma del material del clip {}", id));
            }
            image = *got;
        }
        // Máscaras (en coordenadas del cuadro): el clip va a una capa que después se recorta con
        // su alfa, como el maskedmerge del export tras componer el clip. Con un modo de fusión, la
        // capa se funde con lo de debajo en vez de ir encima (clip_blend.py: el clip en una capa
        // transparente del tamaño del cuadro y blend_steps; la máscara mezcla después, que con el
        // fondo opaco es lo mismo que recortar la capa antes de fundirla).
        std::optional<MaskAlpha> mask;
        // Solo en los tipos que el export enmascara (clip_mask.maskable: vídeo, imagen, figura y
        // texto); un motion con máscara sale entero, como allí.
        if (!layer.masks.empty() && core::maskable(*layer.clip)) {
            auto alpha = mask_alpha(layer.masks, width, height);
            if (!alpha) {
                return std::unexpected(alpha.error());
            }
            mask = std::move(*alpha);
        }
        const bool blended = layer.blend != "normal";
        if (mask || blended) {
            canvas.begin_layer(layer.blend);
        }
        // Filtros de color (o el `look` antiguo): una matriz por clip, sobre lo ya escalado.
        const auto color = core::stack_matrix(core::clip_filters(*layer.clip));
        const auto close_on_error = [&] {
            if (mask || blended) {
                (void)canvas.end_layer(MaskAlpha{}); // cierra la capa sin dibujarla
            }
        };
        if (shape) {
            const core::ClipEffects fx = core::clip_effects(*layer.clip);
            if (auto drawn = fx.any() ? draw_shape_with_effects(canvas, layer, color, fx)
                                      : draw_shape(canvas, layer, color);
                !drawn) {
                close_on_error();
                return std::unexpected(drawn.error());
            }
        } else {
            const core::ClipEffects fx = core::clip_effects(*layer.clip);
            const bool effects = fx.any();
            for (std::size_t i = 0; i < layer.blits.size(); ++i) {
                const core::Blit& blit = layer.blits[i];
                auto drawn = effects ? draw_with_effects(canvas, *image, blit, layer, color, fx, i)
                                     : canvas.draw_image(*image, blit.src, blit.m, layer.alpha, layer.wipe, color);
                if (!drawn) {
                    close_on_error();
                    return std::unexpected(drawn.error());
                }
            }
        }
        if (mask) {
            if (auto merged = canvas.end_layer(*mask); !merged) {
                return std::unexpected(merged.error());
            }
        } else if (blended) {
            canvas.end_layer();
        }
    }
    return {};
}

} // namespace gc::render
