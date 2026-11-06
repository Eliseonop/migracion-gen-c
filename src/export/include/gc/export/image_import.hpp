#pragma once

// El OpenCV de images.py para «Importar imagen» (core::import_image): to_working_png decodifica
// con media::decode_image y escribe el PNG de trabajo (RGBA de 8 bits, con el codificador de
// Skia); probe_size decodifica el archivo guardado. El PNG no es el mismo archivo que el de cv2,
// pero sí los mismos píxeles (o casi, en lo con pérdida: test_image_import_parity.cpp).

#include "gc/core/media_import.hpp"

namespace gc::exporter {

[[nodiscard]] core::ImageCodec image_codec();

} // namespace gc::exporter
