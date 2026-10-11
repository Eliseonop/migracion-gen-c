#include "gc/media/probe.hpp"

#include <charconv>
#include <format>
#include <memory>
#include <string>
#include <string_view>

#include "gc/core/io.hpp"
#include "py.hpp"

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/pixdesc.h>
}

namespace gc::media {

namespace {

struct FormatCloser {
    void operator()(AVFormatContext* ctx) const { avformat_close_input(&ctx); }
};
using FormatPtr = std::unique_ptr<AVFormatContext, FormatCloser>;

// Lo que hace ffprobe antes de imprimir nada: abrir (con scan_all_pmts, como él) y leer la
// información de los streams. Si algo falla, ffprobe no imprime nada.
FormatPtr open_like_ffprobe(const std::filesystem::path& path) {
    const std::string url = core::path_to_utf8(path);
    AVFormatContext* raw = nullptr;
    AVDictionary* opts = nullptr;
    av_dict_set(&opts, "scan_all_pmts", "1", AV_DICT_DONT_OVERWRITE);
    const int rc = avformat_open_input(&raw, url.c_str(), nullptr, &opts);
    av_dict_free(&opts);
    if (rc < 0) {
        return nullptr;
    }
    FormatPtr ctx(raw);
    if (avformat_find_stream_info(ctx.get(), nullptr) < 0) {
        return nullptr;
    }
    return ctx;
}

// `format=duration` de ffprobe leído por Python: ffprobe imprime «N/A» si no se conoce
// (AV_NOPTS_VALUE; Python cae a 0.0, p. ej. con una imagen) y, si no, `ts · av_q2d(AV_TIME_BASE_Q)`
// con "%f"; Python hace `round(float(texto), 3)`.
double ffprobe_duration(const AVFormatContext& ctx) {
    if (ctx.duration == AV_NOPTS_VALUE) {
        return 0.0;
    }
    const double seconds = static_cast<double>(ctx.duration) * av_q2d(AV_TIME_BASE_Q);
    const std::string text = std::format("{:.6f}", seconds);
    double parsed = 0.0;
    const auto res = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (res.ec != std::errc{}) {
        return 0.0;
    }
    return core::detail::py_round(parsed, 3);
}

} // namespace

MediaProbe probe_media(const std::filesystem::path& path) {
    MediaProbe out;
    const FormatPtr ctx = open_like_ffprobe(path);
    if (!ctx) {
        return out;
    }
    out.opened = true;
    out.duration = ffprobe_duration(*ctx);
    bool first_video = true;
    for (unsigned i = 0; i < ctx->nb_streams; ++i) {
        const AVCodecParameters* par = ctx->streams[i]->codecpar;
        if (par->codec_type == AVMEDIA_TYPE_AUDIO) {
            out.has_audio = true;
        } else if (par->codec_type == AVMEDIA_TYPE_VIDEO && first_video) {
            // `-select_streams v:0`: el primer stream de vídeo (también una carátula). ffprobe
            // imprime el nombre de la matriz o «unknown»; Python da por sin etiqueta "" y «unknown».
            first_video = false;
            const char* name = av_color_space_name(par->color_space);
            out.color_untagged = par->color_space == AVCOL_SPC_UNSPECIFIED || name == nullptr ||
                                 std::string_view(name) == "unknown";
        }
    }
    return out;
}

double probe_duration(const std::filesystem::path& path) { return probe_media(path).duration; }

bool color_untagged(const std::filesystem::path& path) { return probe_media(path).color_untagged; }

bool has_audio(const std::filesystem::path& path) { return probe_media(path).has_audio; }

void silence_ffmpeg_log() { av_log_set_level(AV_LOG_QUIET); }

} // namespace gc::media
