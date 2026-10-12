// gc::media::Mp4Writer: un MP4 de vídeo H.264 y audio AAC que se puede volver a leer, con la
// duración, el audio y las etiquetas BT.709 del export, y una imagen fiel a la codificada. Solo con
// FFmpeg (gc_media).

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <numbers>
#include <vector>

#include "gc/core/io.hpp"
#include "gc/media/decode.hpp"
#include "gc/media/encode.hpp"
#include "gc/media/probe.hpp"

namespace {

constexpr int kW = 160;
constexpr int kH = 128;
constexpr int kFps = 30;

// Ondas que se mueven: cada fotograma distinto, con detalle en X e Y.
std::vector<std::uint8_t> frame(int k) {
    std::vector<std::uint8_t> px(static_cast<std::size_t>(kW) * kH * 4);
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
            const auto i = (static_cast<std::size_t>(y) * kW + static_cast<std::size_t>(x)) * 4;
            px[i] = static_cast<std::uint8_t>(128 + 100 * std::sin((y * 0.2) - (k * 0.3)));
            px[i + 1] = static_cast<std::uint8_t>(128 + 100 * std::sin((x * 0.15) + (k * 0.2)));
            px[i + 2] = static_cast<std::uint8_t>(128 + 100 * std::sin((x + y) * 0.1));
            px[i + 3] = 255;
        }
    }
    return px;
}

double psnr_luma(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b) {
    double sum = 0.0;
    const std::size_t n = a.size() / 4;
    for (std::size_t i = 0; i < n; ++i) {
        const auto luma = [&](const std::vector<std::uint8_t>& p) {
            return 0.2126 * p[4 * i] + 0.7152 * p[(4 * i) + 1] + 0.0722 * p[(4 * i) + 2];
        };
        const double d = luma(a) - luma(b);
        sum += d * d;
    }
    const double mse = sum / static_cast<double>(n);
    return mse == 0.0 ? std::numeric_limits<double>::infinity() : 10.0 * std::log10(255.0 * 255.0 / mse);
}

} // namespace

TEST_CASE("Mp4Writer: vídeo y audio que se vuelven a leer", "[media][encode]") {
    gc::media::silence_ffmpeg_log();
    CHECK_FALSE(gc::media::pick_h264_encoder().empty());
    const auto out = std::filesystem::temp_directory_path() / "gc_test_encode.mp4";
    {
        gc::media::EncodeSettings s;
        s.width = kW;
        s.height = kH;
        s.fps = kFps;
        auto w = gc::media::Mp4Writer::open(out, s);
        INFO((w ? std::string() : w.error().message));
        REQUIRE(w);
        INFO("codificador " << w->video_encoder());
        for (int k = 0; k < kFps; ++k) {
            REQUIRE(w->video(frame(k)));
        }
        std::vector<float> tone(48000 * 2);
        for (std::size_t i = 0; i < 48000; ++i) {
            const auto v = static_cast<float>(0.3 * std::sin(2 * std::numbers::pi * 440 * static_cast<double>(i) / 48000));
            tone[2 * i] = v;
            tone[(2 * i) + 1] = v;
        }
        REQUIRE(w->audio(tone));
        REQUIRE(w->finish());
    }
    const auto probe = gc::media::probe_media(out);
    CHECK(probe.has_audio);
    CHECK(std::abs(probe.duration - 1.0) < 0.05);
    CHECK_FALSE(probe.color_untagged); // lleva la matriz BT.709

    // La imagen: el fotograma 15 decodificado frente al que se codificó.
    auto got = gc::media::frame_at(out, 15.0 / kFps);
    REQUIRE(got);
    REQUIRE(got->has_value());
    REQUIRE((*got)->width == kW);
    REQUIRE((*got)->height == kH);
    CHECK(psnr_luma((*got)->pixels, frame(15)) > 33.0);
    std::filesystem::remove(out);

    // Una salida impar no vale.
    gc::media::EncodeSettings odd;
    odd.width = 161;
    CHECK_FALSE(gc::media::Mp4Writer::open(out, odd));
}
