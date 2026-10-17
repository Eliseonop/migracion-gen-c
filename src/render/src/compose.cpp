#include "gc/render/compose.hpp"

#include <charconv>
#include <format>
#include <optional>
#include <string>
#include <utility>

#include "gc/core/bg.hpp"
#include "gc/core/clip_filters.hpp"
#include "gc/core/masks.hpp"
#include "gc/render/shape.hpp"

namespace gc::render {

namespace {

bool is_media(const core::TimelineClip& clip) {
    return clip.kind == "video" || clip.kind == "image" || clip.kind == "motion";
}

bool is_adjustment(const core::TimelineClip& clip) { return clip.kind == "adjustment"; }

bool is_shape(const core::TimelineClip& clip) { return clip.kind == "shape"; }

// Un valor que activa algo: true o un número distinto de 0, también escrito como texto («2»), como
// el float(val) de _fx_on/_fx_num en clip_fx.py. Por prudencia, un negativo también cuenta (el
// brillo o el contraste negativos sí cambian la imagen).
bool truthy(const Json& v) {
    if (v.is_boolean()) {
        return v.get<bool>();
    }
    if (v.is_number()) {
        return v.get<double>() != 0.0;
    }
    if (v.is_string()) {
        const auto& s = v.get_ref<const std::string&>();
        double d = 0.0;
        const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), d);
        return ec == std::errc{} && end == s.data() + s.size() && d != 0.0;
    }
    return false;
}

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
    if (clip.effects && clip.effects->is_object()) {
        for (const auto& [key, value] : clip.effects->items()) {
            if (truthy(value)) {
                return std::format("el render aún no aplica el efecto «{}»", key);
            }
        }
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
            if (auto drawn = draw_shape(canvas, layer, color); !drawn) {
                close_on_error();
                return std::unexpected(drawn.error());
            }
        } else {
            for (const core::Blit& blit : layer.blits) {
                if (auto drawn = canvas.draw_image(*image, blit.src, blit.m, layer.alpha, layer.wipe, color); !drawn) {
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
