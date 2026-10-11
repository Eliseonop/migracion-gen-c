#include "gc/render/compose.hpp"

#include <format>
#include <string>

namespace gc::render {

Result<Image> compose_frame(const std::vector<core::FrameLayer>& layers, int width, int height,
                            const SourceFn& source) {
    auto canvas = Canvas::make(width, height);
    if (!canvas) {
        return std::unexpected(canvas.error());
    }
    canvas->clear({0, 0, 0, 255});
    for (const core::FrameLayer& layer : layers) {
        const std::string& id = layer.clip->id;
        if (layer.blits.empty()) {
            const std::string& kind = layer.clip->kind;
            if (kind == "video" || kind == "image" || kind == "motion") {
                // Con las reglas del preview, evaluate deja sin blits el material sin tamaño.
                return fail(ErrorCode::InvalidArgument,
                            std::format("el clip {} no tiene las dimensiones de su material", id));
            }
            return fail(ErrorCode::Unsupported,
                        std::format("el render aún no dibuja capas de tipo «{}» (clip {})", kind, id));
        }
        if (layer.blend != "normal") {
            return fail(ErrorCode::Unsupported,
                        std::format("el render aún no hace el modo de fusión «{}» (clip {})", layer.blend, id));
        }
        if (!layer.masks.empty()) {
            return fail(ErrorCode::Unsupported, std::format("el render aún no aplica máscaras (clip {})", id));
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
