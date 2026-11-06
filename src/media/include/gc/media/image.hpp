#pragma once

// Una imagen fija decodificada desde memoria (PNG, JPG, WebP, BMP, TIFF, GIF…): el primer
// fotograma en RGBA, como cv2.imdecode(…, IMREAD_UNCHANGED) de images.to_working_png. Sin giro
// (ni EXIF ni matriz de visualización: OpenCV con IMREAD_UNCHANGED tampoco gira) y sin escalar.

#include <cstdint>
#include <optional>
#include <span>

#include "gc/core/error.hpp"
#include "gc/media/decode.hpp"

namespace gc::media {

// Nada si los datos no son una imagen que FFmpeg sepa leer o no es de un formato que lea OpenCV
// (PNG, JPEG, WebP, BMP, TIFF, GIF, PNM, Sun raster, EXR, HDR y JPEG 2000: no AVIF, HEIC ni vídeo).
// El croma submuestreado, en bilineal y doblado exacto (también con lados
// impares), como el «fancy upsampling» de libjpeg y libwebp; un TIFF con alfa, premultiplicado
// como lo da libtiff.
[[nodiscard]] std::optional<RgbaFrame> decode_image(std::span<const std::uint8_t> data);

} // namespace gc::media
