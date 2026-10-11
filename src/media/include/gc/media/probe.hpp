#pragma once

// Sondas de un archivo de medios con libavformat: lo que video-yt pregunta a ffprobe.
//   - duration:       sound_design.probe_duration   (format=duration, a 3 decimales; 0 si falla)
//   - color_untagged: compose._color_untagged        (el primer stream de vídeo no declara matriz)
//   - has_audio:      compose._has_audio             (hay algún stream de audio)
// Igual que ffprobe: abre el archivo y lee la información de los streams
// (avformat_find_stream_info), que en algunos códecs decodifica un poco para rellenarla.

#include <filesystem>

namespace gc::media {

struct MediaProbe {
    bool opened = false;         // se pudo abrir y leer la información de los streams
    double duration = 0.0;       // s, como probe_duration (0 si falla o no se conoce)
    bool color_untagged = true;  // como _color_untagged (también si no hay vídeo o falla)
    bool has_audio = false;      // como _has_audio
};

// Las tres de una vez (abre el archivo una sola vez).
[[nodiscard]] MediaProbe probe_media(const std::filesystem::path& path);

[[nodiscard]] double probe_duration(const std::filesystem::path& path);
[[nodiscard]] bool color_untagged(const std::filesystem::path& path);
[[nodiscard]] bool has_audio(const std::filesystem::path& path);

// Calla el registro de FFmpeg (global: av_log_set_level). Para los tests y la CLI, que
// sondean archivos rotos a propósito.
void silence_ffmpeg_log();

} // namespace gc::media
