#pragma once

// Historial de enlaces de YouTube consultados («Analizar»), espejo de yt_history.py de
// video-yt: vive en la clave `yt_history` de data/settings.json, el más reciente primero.
// Cada entrada es {url, title, video_id, at}, con `at` en segundos Unix.

#include <cstdint>
#include <functional>
#include <string_view>

#include "gc/core/error.hpp"
#include "gc/core/json.hpp"
#include "gc/core/settings.hpp"

namespace gc::core {

inline constexpr std::int64_t kYtHistoryLimit = 40;

// int(time.time()): la hora en segundos Unix. Se inyecta para las pruebas.
using UnixClock = std::function<std::int64_t()>;

// record: apunta `url` (sin espacios alrededor) al principio del historial y quita las
// entradas con el mismo vídeo (video_id o, sin él, la url) o la misma url. `video` es el
// del análisis: un objeto con `id` y `title`, o null. Se queda con max(1, limit or 40)
// entradas. Con una url vacía no guarda nada y devuelve el historial tal cual.
// Falla, como Python, si una entrada guardada tiene un `at` que no se pasa a entero.
[[nodiscard]] Result<Json> yt_history_record(SettingsFile& settings, std::string_view url, const Json& video,
                                             std::int64_t limit, const UnixClock& now);

// remove: quita las entradas cuyo vídeo o url es `url_or_id` (sin espacios alrededor) y
// guarda el historial limpio (también con un texto vacío, que no quita nada).
[[nodiscard]] Result<Json> yt_history_remove(SettingsFile& settings, std::string_view url_or_id);

} // namespace gc::core
