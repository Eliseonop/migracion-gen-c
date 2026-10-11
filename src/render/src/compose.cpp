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
            return fail(ErrorCode::Unsupported,
                        std::format("el render aún no dibuja capas de tipo «{}» (clip {})", layer.clip->kind, id));
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
        for (const core::Blit& blit : layer.blits) {
            if (auto drawn = canvas->draw_image(**image, blit.src, blit.m, layer.alpha, layer.wipe); !drawn) {
                return std::unexpected(drawn.error());
            }
        }
    }
    return canvas->snapshot();
}

} // namespace gc::render
