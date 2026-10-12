#pragma once

// Interno de gc_media: los argumentos de entrada de ffmpeg (core::ffmpeg_input_args) separados en
// lo que entiende cada pieza de libav*, y el texto de un código de error de FFmpeg.

#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "gc/core/error.hpp"

extern "C" {
#include <libavutil/error.h>
}

namespace gc::media::detail {

inline std::string av_err(int code) {
    std::array<char, AV_ERROR_MAX_STRING_SIZE> buf{};
    av_strerror(code, buf.data(), buf.size());
    return buf.data();
}

struct InputOptions {
    std::string path;
    std::string decoder;                                      // -c:v
    std::optional<std::int64_t> duration_us;                  // -t, en µs (como av_parse_time del CLI)
    std::optional<std::int64_t> seek_us;                      // -ss antes de -i: seek exacto
    std::vector<std::pair<std::string, std::string>> demuxer; // -loop, -framerate…
};

inline Result<InputOptions> parse_input_args(const std::vector<std::string>& args) {
    InputOptions out;
    bool have_input = false;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (i + 1 >= args.size() || a.empty() || a.front() != '-') {
            return fail(ErrorCode::InvalidArgument, "argumento de entrada de ffmpeg inesperado: " + a);
        }
        const std::string& v = args[++i];
        if (a == "-i") {
            out.path = v;
            have_input = true;
        } else if (a == "-c:v") {
            out.decoder = v;
        } else if (a == "-t" || a == "-ss") {
            double s = 0.0;
            const auto [end, ec] = std::from_chars(v.data(), v.data() + v.size(), s);
            // Más allá de ±9,2·10¹² s el valor en µs no cabe en 64 bits (ffmpeg: «Invalid duration»).
            if (ec != std::errc{} || end != v.data() + v.size() || !std::isfinite(s) || std::fabs(s) > 9.2e12) {
                return fail(ErrorCode::InvalidArgument, "instante o duración de entrada no válidos: " + v);
            }
            (a == "-t" ? out.duration_us : out.seek_us) = std::llround(s * 1e6);
        } else {
            out.demuxer.emplace_back(a.substr(1), v);
        }
    }
    if (!have_input) {
        return fail(ErrorCode::InvalidArgument, "faltan «-i» y la ruta en los argumentos de entrada");
    }
    return out;
}

} // namespace gc::media::detail
