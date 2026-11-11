#pragma once

// La pestaña YouTube del material (EdMaterial.jsx y ytQuality.js): las calidades del vídeo analizado para
// «Agregar vídeo completo», sus textos, y el historial de enlaces tal como lo pinta y lo edita el editor.

#include <string>

#include "gc/core/json.hpp"

namespace gc::core {

// videoQualities: las de /api/analyze (video.qualities) con calidad > 0, de mejor a peor.
[[nodiscard]] Json yt_video_qualities(const Json& result);
// qualityShort: «8K», «4K» o «1080p»; qualityTag: «8K», «4K», «2K», «Full HD», «HD» o «».
[[nodiscard]] std::string yt_quality_short(const Json& quality);
[[nodiscard]] std::string yt_quality_tag(const Json& quality);
// fmtBytes: «1.3 GB» o «12 MB» («» si no hay tamaño); qualityMeta: «60 fps · ≈ 1.3 GB» con lo que se sepa.
[[nodiscard]] std::string yt_fmt_bytes(const Json& bytes);
[[nodiscard]] std::string yt_quality_meta(const Json& item);

// normalizeHistory: las entradas con un enlace. removeHistory: la lista sin `item` (por id del vídeo o enlace).
[[nodiscard]] Json yt_normalize_history(const Json& raw);
[[nodiscard]] Json yt_history_without(const Json& history, const Json& item);

} // namespace gc::core
