#include "gc/render/compose.hpp"

#include <format>
#include <string>

#include "gc/core/bg.hpp"

namespace gc::render {

namespace {

bool is_media(const core::TimelineClip& clip) {
    return clip.kind == "video" || clip.kind == "image" || clip.kind == "motion";
}

// Un valor que activa algo: true o un número distinto de 0 (como _fx_on de clip_fx.py).
bool truthy(const Json& v) { return v.is_boolean() ? v.get<bool>() : v.is_number() && v.get<double>() != 0.0; }

// bg_active de clip_bg.py: croma activo, o matte automático listo.
bool bg_active(const core::TimelineClip& clip) {
    if (!clip.bg_removal || !core::bg_capable(clip)) {
        return false;
    }
    const Json bg = core::normalize_bg(*clip.bg_removal);
    if (!bg.is_object() || !bg.value("enabled", false)) {
        return false;
    }
    const Json& chroma = bg["chroma"];
    const Json& a = bg["auto"];
    const bool auto_ready = a.value("enabled", false) && !a.value("base_key", std::string()).empty() &&
                            a.value("status", std::string()) == "ready";
    return chroma.value("enabled", false) || auto_ready;
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
    if (!layer.masks.empty()) {
        return std::string("el render aún no aplica máscaras");
    }
    if (bg_active(clip)) {
        return std::string("el render aún no elimina el fondo (croma o automático)");
    }
    if ((!clip.look.empty() && clip.look != "none") ||
        (clip.filters && clip.filters->is_array() && !clip.filters->empty())) {
        return std::string("el render aún no aplica filtros de color");
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
        for (const core::Blit& blit : layer.blits) {
            if (auto drawn = canvas->draw_image(**image, blit.src, blit.m, layer.alpha, layer.wipe); !drawn) {
                return std::unexpected(drawn.error());
            }
        }
    }
    return canvas->snapshot();
}

} // namespace gc::render
