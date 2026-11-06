#include "gc/export/image_import.hpp"

#include <cstdint>
#include <fstream>
#include <span>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "gc/media/image.hpp"
#include "gc/render/png.hpp"

namespace gc::exporter {

namespace {

std::span<const std::uint8_t> bytes_of(std::string_view data) {
    return {reinterpret_cast<const std::uint8_t*>(data.data()), data.size()}; // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast): los mismos bytes
}

std::optional<std::string> to_png(std::string_view data) {
    auto frame = media::decode_image(bytes_of(data));
    if (!frame) {
        return std::nullopt;
    }
    const render::Image image{frame->width, frame->height, std::move(frame->pixels)};
    auto png = render::encode_png(image);
    if (!png) {
        return std::nullopt;
    }
    return std::string(png->begin(), png->end());
}

std::optional<std::pair<std::int64_t, std::int64_t>> size_of(const std::filesystem::path& path) {
    std::error_code ec;
    const auto bytes = std::filesystem::file_size(path, ec);
    if (ec || bytes == 0) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> data(static_cast<std::size_t>(bytes));
    std::ifstream f(path, std::ios::binary);
    if (!f.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()))) { // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast): bytes
        return std::nullopt;
    }
    const auto frame = media::decode_image(data);
    if (!frame) {
        return std::nullopt;
    }
    return std::pair<std::int64_t, std::int64_t>{frame->width, frame->height};
}

} // namespace

core::ImageCodec image_codec() { return {to_png, size_of}; }

} // namespace gc::exporter
