#pragma once

// Grabar un directo de TikTok o de YouTube y sacar clips de él (función de GenClipPro: video-yt no la tiene).
//
// yt-dlp saca la dirección del flujo sin bajar nada (`-J`) y FFmpeg lo graba tal cual, sin recomprimir, en MPEG-TS:
// se puede leer mientras crece y queda entero aunque se corte. Se para con «q» por su stdin. Al acabar, el TS pasa a
// MP4 (el navegador de video-yt no lee TS) y entra al material; cada «Marcar momento» deja un clip por referencia
// (create_segments) con un rato antes y otro después de la marca.

#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "gc/core/error.hpp"
#include "gc/core/json.hpp"

namespace gc::core {

enum class LivePlatform { TikTok, YouTube };

struct LiveLink {
    LivePlatform platform = LivePlatform::TikTok;
    std::string url; // con «https://» delante
};

// El enlace de un directo. De TikTok: tiktok.com/@usuario/live, o el perfil (tiktok.com/@usuario, que pasa a su
// /live), y m.tiktok.com/share/live/<id>; un vídeo de TikTok no vale. De YouTube, cualquier enlace de youtube.com o
// youtu.be (un vídeo, /live/<id> o el /live de un canal): yt-dlp dice luego si está en directo. Nada si no es nada
// de eso.
[[nodiscard]] std::optional<LiveLink> live_link(std::string_view text);
[[nodiscard]] std::string_view live_platform_name(LivePlatform platform); // «TikTok» o «YouTube»

// Los argumentos de yt-dlp: el JSON del directo con el mejor formato que lleve vídeo y audio juntos, sin bajar nada.
[[nodiscard]] std::vector<std::string> live_resolve_args(const LiveLink& link);

// Lo que FFmpeg necesita del directo.
struct LiveStream {
    std::string url;     // la del formato elegido (FLV o HLS)
    std::string headers; // las cabeceras HTTP que pide, «Nombre: valor\r\n» una tras otra (el -headers de FFmpeg)
    std::string id;
    std::string title;
    std::string uploader;
};
// El JSON de `yt-dlp -J` (el formato elegido va arriba del todo: `url` y `http_headers`). Error si no está en
// directo ahora (`is_live` o `live_status`) o si no trae la dirección.
[[nodiscard]] Result<LiveStream> live_stream_from_info(const Json& info);
// Un fallo de yt-dlp (lo que dejó en stderr) en palabras del usuario.
[[nodiscard]] std::string live_resolve_error(std::string_view stderr_text);

// FFmpeg: graba el primer vídeo y, si lo hay, el primer audio sin recomprimir en MPEG-TS (`out`), con el progreso
// en stdout (-progress) y sin quedarse colgado si el flujo se para. Se para limpio con «q» por stdin.
[[nodiscard]] std::vector<std::string> live_record_args(const LiveStream& stream, const std::filesystem::path& out);
// FFmpeg: el TS grabado a MP4 sin recomprimir, con el índice al principio.
[[nodiscard]] std::vector<std::string> live_remux_args(const std::filesystem::path& ts, const std::filesystem::path& mp4);

// El progreso de FFmpeg: los segundos grabados (out_time_us), los bytes (total_size) y si acabó (progress=end).
struct LiveProgress {
    double seconds = 0;
    std::int64_t bytes = 0;
    bool ended = false;
};
// Aplica una línea «clave=valor» de -progress; lo demás (y un «N/A») no cambia nada.
void live_apply_progress(LiveProgress& progress, std::string_view line);

// Los tramos de las marcas, en segundos de la grabación: [t − before, t + after] acotados a [0, duration]
// (duration ≤ 0: sin tope por arriba), redondeados a ms, de menor a mayor, juntando los que se pisan o se tocan y sin
// los de menos de kMinSegment.
[[nodiscard]] std::vector<std::pair<double, double>> live_mark_ranges(std::span<const double> marks, double duration,
                                                                      double before, double after);

// El nombre de la grabación: «<título> <stamp>» sin lo que no vale en un nombre de archivo (o el de la plataforma si
// no hay título), con el título en 60 caracteres como mucho.
[[nodiscard]] std::string live_file_stem(std::string_view title, LivePlatform platform, std::string_view stamp);

} // namespace gc::core
