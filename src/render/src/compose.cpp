#include "gc/render/compose.hpp"

#include <charconv>
#include <format>
#include <optional>
#include <string>
#include <utility>

#include "gc/core/bg.hpp"
#include "gc/core/clip_filters.hpp"

namespace gc::render {

namespace {

bool is_media(const core::TimelineClip& clip) {
    return clip.kind == "video" || clip.kind == "image" || clip.kind == "motion";
}

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
    if (!is_media(clip)) {
        return std::format("el render aún no dibuja capas de tipo «{}»", clip.kind);
    }
    if (layer.blend != "normal") {
        return std::format("el render aún no hace el modo de fusión «{}»", layer.blend);
    }
    if (auto reason = mask_unsupported(layer.masks); !reason.empty()) {
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
    canvas->clear({0, 0, 0, 255});
    for (const core::FrameLayer& layer : layers) {
        const std::string& id = layer.clip->id;
        if (auto reason = unsupported_reason(layer)) {
            return fail(ErrorCode::Unsupported, std::format("{} (clip {})", *reason, id));
        }
        if (layer.blits.empty()) {
            // Con las reglas del preview, evaluate deja sin blits el material sin tamaño.
            return fail(ErrorCode::InvalidArgument, std::format("el clip {} no tiene las dimensiones de su material", id));
        }
        auto image = source(layer);
        if (!image) {
            return std::unexpected(image.error());
        }
        if (*image == nullptr) {
            return fail(ErrorCode::Internal, std::format("no hay fotograma del material del clip {}", id));
        }
        // Máscaras (en coordenadas del cuadro): el clip va a una capa que después se recorta con
        // su alfa, como el maskedmerge del export tras componer el clip.
        std::optional<MaskAlpha> mask;
        if (!layer.masks.empty()) {
            auto alpha = mask_alpha(layer.masks, width, height);
            if (!alpha) {
                return std::unexpected(alpha.error());
            }
            mask = std::move(*alpha);
            canvas->begin_layer();
        }
        // Filtros de color (o el `look` antiguo): una matriz por clip, sobre lo ya escalado.
        const auto color = core::stack_matrix(core::clip_filters(*layer.clip));
        for (const core::Blit& blit : layer.blits) {
            if (auto drawn = canvas->draw_image(**image, blit.src, blit.m, layer.alpha, layer.wipe, color); !drawn) {
                if (mask) {
                    (void)canvas->end_layer(MaskAlpha{}); // cierra la capa sin dibujarla
                }
                return std::unexpected(drawn.error());
            }
        }
        if (mask) {
            if (auto merged = canvas->end_layer(*mask); !merged) {
                return std::unexpected(merged.error());
            }
        }
    }
    return canvas->snapshot();
}

} // namespace gc::render
