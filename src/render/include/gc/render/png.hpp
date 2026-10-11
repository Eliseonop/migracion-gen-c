#pragma once

// PNG de un fotograma (con el codificador de Skia): lo que escribe `genclip frame`, como el PNG
// de compose.render_frame.

#include <cstdint>
#include <filesystem>
#include <vector>

#include "gc/core/error.hpp"
#include "gc/render/canvas.hpp"

namespace gc::render {

// RGBA de 8 bits sin premultiplicar. Error si la imagen está mal formada.
[[nodiscard]] Result<std::vector<std::uint8_t>> encode_png(const Image& image);
// encode_png y al disco (sobrescribe).
[[nodiscard]] Result<void> write_png(const Image& image, const std::filesystem::path& path);

} // namespace gc::render
