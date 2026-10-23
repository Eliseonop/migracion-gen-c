#pragma once

// Calidades de un vídeo de YouTube para descargarlo COMPLETO (la parte sin red de
// youtube_video.py de video-yt), sobre la información de yt-dlp (`info["formats"]`).
//
// La «calidad» es la etiqueta de YouTube (720p, 1080p…), que mide el lado CORTO: de `format_note`
// («1080p60 HDR») o, si falta, min(ancho, alto). Por calidad se elige un formato de vídeo: SDR antes
// que HDR, más fps, descarga directa antes que HLS (m3u8), H.264 antes que VP9 y AV1, y luego más
// bitrate y tamaño. El audio va aparte (m4a si lo hay, sin la compresión de rango «-drc»).
//
// Los valores raros de yt-dlp se leen como Python: `float(v or 0)` (0 si no es un número), y lo que
// en Python lanza (un `int()` de NaN, `.lower()` de algo que no es texto) es un error.

#include <cstdint>
#include <optional>
#include <string>

#include "gc/core/error.hpp"
#include "gc/core/json.hpp"

namespace gc::core {

// Por debajo de esto no sirve para editar; solo se ofrece si no hay nada mejor.
inline constexpr std::int64_t kYoutubeMinQuality = 360;

// quality_of: la etiqueta de calidad de un formato, o nullopt si no es vídeo.
[[nodiscard]] Result<std::optional<std::int64_t>> youtube_format_quality(const Json& format);

// available_qualities: [{quality, fps, size}] de mejor a peor, sin las de menos de 360p (si queda
// alguna). `size` es la estimación en bytes (vídeo + audio) o null.
[[nodiscard]] Result<Json> youtube_available_qualities(const Json& info);

// resolve_quality: la pedida, o la mejor por debajo; sin pedir (nullopt o 0), la mejor; si todas
// son mayores, la peor. nullopt si no hay ninguna.
[[nodiscard]] std::optional<std::int64_t> youtube_resolve_quality(const Json& qualities,
                                                                  std::optional<std::int64_t> wanted);

// format_spec: el selector de yt-dlp para `quality`: el formato elegido y, si no trae audio, el
// mejor audio (m4a si lo hay). Error «El vídeo no tiene calidad {q}p.» si no la tiene.
[[nodiscard]] Result<std::string> youtube_format_spec(const Json& info, std::int64_t quality);

// _mb: «3.2 MB» por debajo de 10 MB y «12 MB» por encima.
[[nodiscard]] std::string mb_label(double bytes);

} // namespace gc::core
