#include "gc/render/png.hpp"

#include <cstddef>
#include <format>
#include <fstream>

#include "gc/core/io.hpp"
#include "include/core/SkData.h"
#include "include/core/SkImageInfo.h"
#include "include/core/SkPixmap.h"
#include "include/encode/SkPngEncoder.h"

namespace gc::render {

Result<std::vector<std::uint8_t>> encode_png(const Image& image) {
    const auto row = static_cast<std::size_t>(image.width) * 4;
    if (image.width <= 0 || image.height <= 0 || image.pixels.size() != row * static_cast<std::size_t>(image.height)) {
        return fail(ErrorCode::InvalidArgument,
                    std::format("imagen de {}x{} con {} bytes", image.width, image.height, image.pixels.size()));
    }
    const SkPixmap pixmap(SkImageInfo::Make(image.width, image.height, kRGBA_8888_SkColorType, kUnpremul_SkAlphaType),
                          image.pixels.data(), row);
    const sk_sp<SkData> data = SkPngEncoder::Encode(pixmap, {});
    if (!data) {
        return fail(ErrorCode::Internal, "Skia no pudo codificar el PNG");
    }
    const auto* bytes = data->bytes();
    return std::vector<std::uint8_t>(bytes, bytes + data->size());
}

Result<void> write_png(const Image& image, const std::filesystem::path& path) {
    auto png = encode_png(image);
    if (!png) {
        return std::unexpected(png.error());
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(png->data()), static_cast<std::streamsize>(png->size()));
    if (!out) {
        return fail(ErrorCode::Io, std::format("no se pudo escribir {}", core::path_to_utf8(path)));
    }
    return {};
}

} // namespace gc::render
