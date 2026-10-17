#pragma once

// El MP4 del export: vídeo H.264 en yuv420p BT.709 de rango TV y audio AAC a 48 kHz estéreo, como
// la salida de compose.build_command (`BT709_FINAL_FILTER`, `BT709_OUTPUT_ARGS`, `-c:a aac -ar 48000
// -b:a 128k`, `-r fps`, `-movflags +faststart`).
//
// El codificador de vídeo se elige como gpu.py: el primero de NVENC, QSV y AMF que el FFmpeg trae Y
// que pasa una sonda real (codificar un fotograma); si ninguno, OpenH264 por software (el export usa
// libx264, que pasaría todo a GPL; plan §10). La calidad es la del CRF de los ajustes del export:
// `-cq` en NVENC, `-global_quality` en QSV, `-qp_*` en AMF y qmin = qmax en OpenH264, que no tiene
// CRF (de ahí que el tamaño del archivo no coincida con el de x264). OpenH264 solo usa varios hilos
// con un número de hilos y de *slices* explícito: hasta 4 de cada.

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

#include "gc/core/error.hpp"

namespace gc::media {

struct EncodeSettings {
    int width = 720;   // par
    int height = 1280; // par
    int fps = 30;
    int crf = 23;               // la calidad de los ajustes del export (standard)
    std::string preset = "fast";
    bool audio = true;
    std::string video_encoder;  // "" = elegirlo como gpu.py
    std::int64_t audio_bitrate = 128000;
};

// Los codificadores H.264 por hardware que trae este FFmpeg y pasan la sonda real, en el orden de
// gpu.py (h264_nvenc, h264_qsv, h264_amf). Se calcula una vez.
[[nodiscard]] const std::vector<std::string>& working_hw_encoders();
// El que usará el export: el primero de los de arriba o «libopenh264».
[[nodiscard]] std::string pick_h264_encoder();

// Un fotograma de vídeo ya en el formato del codificador (lo que da FrameConverter): Mp4Writer lo
// codifica tal cual.
class VideoFrame {
public:
    VideoFrame(VideoFrame&&) noexcept;
    VideoFrame& operator=(VideoFrame&&) noexcept;
    VideoFrame(const VideoFrame&) = delete;
    VideoFrame& operator=(const VideoFrame&) = delete;
    ~VideoFrame();

private:
    friend class FrameConverter;
    friend class Mp4Writer;
    struct Impl;
    explicit VideoFrame(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

// RGBA sin premultiplicar (width · height · 4, sin relleno entre filas) → el formato del
// codificador de un Mp4Writer: yuv420p (o el primero que admita) BT.709 de rango TV, con el swscale
// bicúbico del CLI (BT709_FINAL_FILTER). Cada hilo usa el suyo (Mp4Writer::converter), así la
// conversión no ocupa el hilo que codifica.
class FrameConverter {
public:
    FrameConverter(FrameConverter&&) noexcept;
    FrameConverter& operator=(FrameConverter&&) noexcept;
    FrameConverter(const FrameConverter&) = delete;
    FrameConverter& operator=(const FrameConverter&) = delete;
    ~FrameConverter();

    [[nodiscard]] Result<VideoFrame> convert(std::span<const std::uint8_t> rgba);

private:
    friend class Mp4Writer;
    struct Impl;
    explicit FrameConverter(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

class Mp4Writer {
public:
    [[nodiscard]] static Result<Mp4Writer> open(const std::filesystem::path& path, const EncodeSettings& settings);

    Mp4Writer(Mp4Writer&&) noexcept;
    Mp4Writer& operator=(Mp4Writer&&) noexcept;
    Mp4Writer(const Mp4Writer&) = delete;
    Mp4Writer& operator=(const Mp4Writer&) = delete;
    ~Mp4Writer();

    // El siguiente fotograma (RGBA sin premultiplicar, width · height · 4 del tamaño de salida).
    [[nodiscard]] Result<void> video(std::span<const std::uint8_t> rgba);
    // El siguiente fotograma, ya convertido por un conversor de este Mp4Writer.
    [[nodiscard]] Result<void> video(VideoFrame frame);
    // Un conversor nuevo al formato de este codificador, para usarlo en otro hilo.
    [[nodiscard]] Result<FrameConverter> converter() const;
    // Más audio (estéreo entrelazado a 48 kHz).
    [[nodiscard]] Result<void> audio(std::span<const float> interleaved);
    // Vacía los codificadores y cierra el archivo.
    [[nodiscard]] Result<void> finish();

    [[nodiscard]] const std::string& video_encoder() const noexcept;

private:
    struct Impl;
    explicit Mp4Writer(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

} // namespace gc::media
