#include "gc/media/encode.hpp"

#include <algorithm>
#include <array>
#include <format>
#include <string_view>
#include <utility>

#include "gc/core/io.hpp"
#include "input_args.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/audio_fifo.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

namespace gc::media {

namespace {

using detail::av_err;

struct CodecFree {
    void operator()(AVCodecContext* c) const { avcodec_free_context(&c); }
};
struct FrameFree {
    void operator()(AVFrame* f) const { av_frame_free(&f); }
};
struct PacketFree {
    void operator()(AVPacket* p) const { av_packet_free(&p); }
};
struct SwsFree {
    void operator()(SwsContext* s) const { sws_freeContext(s); }
};
struct FifoFree {
    void operator()(AVAudioFifo* f) const { av_audio_fifo_free(f); }
};
struct DictFree {
    void operator()(AVDictionary* d) const { av_dict_free(&d); }
};

using CodecPtr = std::unique_ptr<AVCodecContext, CodecFree>;

constexpr std::array<std::string_view, 3> kHwPreference{"h264_nvenc", "h264_qsv", "h264_amf"};

// _encoder_args_for de gpu.py: la calidad del CRF en las opciones de cada codificador.
AVDictionary* encoder_options(std::string_view name, int crf, const std::string& preset) {
    AVDictionary* d = nullptr;
    const std::string q = std::to_string(crf);
    if (name.ends_with("nvenc")) {
        av_dict_set(&d, "preset", "p5", 0);
        av_dict_set(&d, "cq", q.c_str(), 0);
    } else if (name == "h264_qsv") {
        av_dict_set(&d, "global_quality", q.c_str(), 0);
        av_dict_set(&d, "preset", "medium", 0);
    } else if (name == "h264_amf") {
        av_dict_set(&d, "rc", "cqp", 0);
        av_dict_set(&d, "qp_i", q.c_str(), 0);
        av_dict_set(&d, "qp_p", q.c_str(), 0);
        av_dict_set(&d, "qp_b", q.c_str(), 0);
    } else if (name == "libopenh264") {
        // Sin CRF: el cuantificador fijo en el del CRF.
        av_dict_set(&d, "qmin", q.c_str(), 0);
        av_dict_set(&d, "qmax", q.c_str(), 0);
    } else {
        av_dict_set(&d, "crf", q.c_str(), 0); // libx264, si algún día se compila con él
        av_dict_set(&d, "preset", preset.c_str(), 0);
    }
    return d;
}

// El formato de píxel: yuv420p (`-pix_fmt yuv420p`) si el codificador lo admite; si no, el
// primero que admita (lo que hace el CLI, con un aviso).
AVPixelFormat pixel_format(const AVCodec* codec) {
    const void* list = nullptr;
    int n = 0;
    if (avcodec_get_supported_config(nullptr, codec, AV_CODEC_CONFIG_PIX_FORMAT, 0, &list, &n) < 0 || list == nullptr) {
        return AV_PIX_FMT_YUV420P;
    }
    const auto* fmts = static_cast<const AVPixelFormat*>(list);
    for (int i = 0; i < n; ++i) {
        if (fmts[i] == AV_PIX_FMT_YUV420P) {
            return AV_PIX_FMT_YUV420P;
        }
    }
    return n > 0 ? fmts[0] : AV_PIX_FMT_YUV420P;
}

Result<CodecPtr> open_video(std::string_view name, int width, int height, int fps, int crf, const std::string& preset,
                            bool global_header) {
    const AVCodec* codec = avcodec_find_encoder_by_name(std::string(name).c_str());
    if (codec == nullptr) {
        return fail(ErrorCode::Unsupported, std::format("este FFmpeg no tiene el codificador {}", name));
    }
    CodecPtr ctx(avcodec_alloc_context3(codec));
    if (!ctx) {
        return fail(ErrorCode::Internal, "sin memoria para el codificador de vídeo");
    }
    ctx->width = width;
    ctx->height = height;
    ctx->time_base = {1, fps};
    ctx->framerate = {fps, 1};
    ctx->pix_fmt = pixel_format(codec);
    ctx->colorspace = AVCOL_SPC_BT709;
    ctx->color_primaries = AVCOL_PRI_BT709;
    ctx->color_trc = AVCOL_TRC_BT709;
    ctx->color_range = AVCOL_RANGE_MPEG;
    if (global_header) {
        ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }
    std::unique_ptr<AVDictionary, DictFree> opts(encoder_options(name, crf, preset));
    AVDictionary* raw = opts.release();
    const int r = avcodec_open2(ctx.get(), codec, &raw);
    av_dict_free(&raw);
    if (r < 0) {
        return fail(ErrorCode::Io, std::format("no se pudo abrir el codificador {}: {}", name, av_err(r)));
    }
    return ctx;
}

// _encoder_works de gpu.py: codificar un fotograma de 64×64 y que salga algo.
bool encoder_works(std::string_view name) {
    auto ctx = open_video(name, 64, 64, 5, 23, "fast", false);
    if (!ctx) {
        return false;
    }
    std::unique_ptr<AVFrame, FrameFree> frame(av_frame_alloc());
    frame->format = (*ctx)->pix_fmt;
    frame->width = 64;
    frame->height = 64;
    if (av_frame_get_buffer(frame.get(), 0) < 0 || av_frame_make_writable(frame.get()) < 0) {
        return false;
    }
    for (int p = 0; p < 4 && frame->data[p] != nullptr; ++p) {
        std::fill_n(frame->data[p], static_cast<std::size_t>(frame->linesize[p]) * 64, std::uint8_t{128});
    }
    frame->pts = 0;
    if (avcodec_send_frame(ctx->get(), frame.get()) < 0 || avcodec_send_frame(ctx->get(), nullptr) < 0) {
        return false;
    }
    std::unique_ptr<AVPacket, PacketFree> pkt(av_packet_alloc());
    bool got = false;
    while (avcodec_receive_packet(ctx->get(), pkt.get()) == 0) {
        got = got || pkt->size > 0;
        av_packet_unref(pkt.get());
    }
    return got;
}

} // namespace

const std::vector<std::string>& working_hw_encoders() {
    static const std::vector<std::string> kWorking = [] {
        std::vector<std::string> out;
        for (const std::string_view name : kHwPreference) {
            if (avcodec_find_encoder_by_name(std::string(name).c_str()) != nullptr && encoder_works(name)) {
                out.emplace_back(name);
            }
        }
        return out;
    }();
    return kWorking;
}

std::string pick_h264_encoder() {
    const auto& hw = working_hw_encoders();
    return hw.empty() ? std::string("libopenh264") : hw.front();
}

struct Mp4Writer::Impl {
    EncodeSettings settings;
    std::string encoder;
    AVFormatContext* fmt = nullptr;
    bool header = false;
    CodecPtr vctx;
    CodecPtr actx;
    AVStream* vst = nullptr;
    AVStream* ast = nullptr;
    std::unique_ptr<SwsContext, SwsFree> sws;
    std::unique_ptr<AVFrame, FrameFree> vframe;
    std::unique_ptr<AVFrame, FrameFree> aframe;
    std::unique_ptr<AVPacket, PacketFree> pkt;
    std::unique_ptr<AVAudioFifo, FifoFree> fifo;
    std::int64_t vcount = 0;
    std::int64_t acount = 0;
    bool finished = false;

    ~Impl() {
        if (fmt != nullptr) {
            if (header && !finished) {
                static_cast<void>(av_write_trailer(fmt));
            }
            if ((fmt->oformat->flags & AVFMT_NOFILE) == 0) {
                avio_closep(&fmt->pb);
            }
            avformat_free_context(fmt);
        }
    }

    Result<void> drain(AVCodecContext* ctx, AVStream* st) {
        while (true) {
            const int r = avcodec_receive_packet(ctx, pkt.get());
            if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) {
                return {};
            }
            if (r < 0) {
                return fail(ErrorCode::Io, "error al codificar: " + av_err(r));
            }
            av_packet_rescale_ts(pkt.get(), ctx->time_base, st->time_base);
            pkt->stream_index = st->index;
            if (const int w = av_interleaved_write_frame(fmt, pkt.get()); w < 0) {
                return fail(ErrorCode::Io, "no se pudo escribir el MP4: " + av_err(w));
            }
        }
    }

    Result<void> send_audio(int samples) {
        aframe->nb_samples = samples;
        if (av_frame_make_writable(aframe.get()) < 0) {
            return fail(ErrorCode::Internal, "sin memoria para el audio");
        }
        if (av_audio_fifo_read(fifo.get(), reinterpret_cast<void**>(aframe->data), samples) < samples) {
            return fail(ErrorCode::Internal, "faltan muestras en la cola de audio");
        }
        aframe->pts = acount;
        acount += samples;
        if (const int r = avcodec_send_frame(actx.get(), aframe.get()); r < 0) {
            return fail(ErrorCode::Io, "error al codificar el audio: " + av_err(r));
        }
        return drain(actx.get(), ast);
    }
};

Mp4Writer::Mp4Writer(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Mp4Writer::Mp4Writer(Mp4Writer&&) noexcept = default;
Mp4Writer& Mp4Writer::operator=(Mp4Writer&&) noexcept = default;
Mp4Writer::~Mp4Writer() = default;

const std::string& Mp4Writer::video_encoder() const noexcept { return impl_->encoder; }

Result<Mp4Writer> Mp4Writer::open(const std::filesystem::path& path, const EncodeSettings& settings) {
    if (settings.width <= 0 || settings.height <= 0 || settings.width % 2 != 0 || settings.height % 2 != 0 ||
        settings.fps <= 0) {
        return fail(ErrorCode::InvalidArgument, std::format("salida de {}x{} a {} fps: hacen falta dimensiones pares",
                                                            settings.width, settings.height, settings.fps));
    }
    auto impl = std::make_unique<Impl>();
    impl->settings = settings;
    impl->encoder = settings.video_encoder.empty() ? pick_h264_encoder() : settings.video_encoder;
    const std::string utf8 = core::path_to_utf8(path);
    if (const int r = avformat_alloc_output_context2(&impl->fmt, nullptr, "mp4", utf8.c_str()); r < 0) {
        return fail(ErrorCode::Internal, "no se pudo preparar el MP4: " + av_err(r));
    }
    const bool global = (impl->fmt->oformat->flags & AVFMT_GLOBALHEADER) != 0;
    auto vctx = open_video(impl->encoder, settings.width, settings.height, settings.fps, settings.crf, settings.preset,
                           global);
    if (!vctx) {
        return std::unexpected(vctx.error());
    }
    impl->vctx = std::move(*vctx);
    impl->vst = avformat_new_stream(impl->fmt, nullptr);
    if (impl->vst == nullptr || avcodec_parameters_from_context(impl->vst->codecpar, impl->vctx.get()) < 0) {
        return fail(ErrorCode::Internal, "no se pudo crear el stream de vídeo");
    }
    impl->vst->time_base = impl->vctx->time_base;
    impl->vst->avg_frame_rate = impl->vctx->framerate;

    if (settings.audio) {
        const AVCodec* aac = avcodec_find_encoder_by_name("aac");
        if (aac == nullptr) {
            return fail(ErrorCode::Unsupported, "este FFmpeg no tiene el codificador AAC");
        }
        impl->actx.reset(avcodec_alloc_context3(aac));
        AVCodecContext* a = impl->actx.get();
        a->sample_fmt = AV_SAMPLE_FMT_FLTP;
        a->sample_rate = 48000;
        av_channel_layout_default(&a->ch_layout, 2);
        a->bit_rate = settings.audio_bitrate;
        a->time_base = {1, 48000};
        if (global) {
            a->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        }
        if (const int r = avcodec_open2(a, aac, nullptr); r < 0) {
            return fail(ErrorCode::Io, "no se pudo abrir el codificador AAC: " + av_err(r));
        }
        impl->ast = avformat_new_stream(impl->fmt, nullptr);
        if (impl->ast == nullptr || avcodec_parameters_from_context(impl->ast->codecpar, a) < 0) {
            return fail(ErrorCode::Internal, "no se pudo crear el stream de audio");
        }
        impl->ast->time_base = a->time_base;
        impl->fifo.reset(av_audio_fifo_alloc(AV_SAMPLE_FMT_FLTP, 2, a->frame_size > 0 ? a->frame_size : 1024));
        impl->aframe.reset(av_frame_alloc());
        impl->aframe->format = AV_SAMPLE_FMT_FLTP;
        impl->aframe->sample_rate = 48000;
        av_channel_layout_default(&impl->aframe->ch_layout, 2);
        impl->aframe->nb_samples = a->frame_size > 0 ? a->frame_size : 1024;
        if (!impl->fifo || av_frame_get_buffer(impl->aframe.get(), 0) < 0) {
            return fail(ErrorCode::Internal, "sin memoria para el audio");
        }
    }

    if ((impl->fmt->oformat->flags & AVFMT_NOFILE) == 0) {
        if (const int r = avio_open(&impl->fmt->pb, utf8.c_str(), AVIO_FLAG_WRITE); r < 0) {
            return fail(ErrorCode::Io, "no se pudo crear el archivo: " + av_err(r), utf8);
        }
    }
    AVDictionary* mux = nullptr;
    av_dict_set(&mux, "movflags", "+faststart", 0);
    const int hr = avformat_write_header(impl->fmt, &mux);
    av_dict_free(&mux);
    if (hr < 0) {
        return fail(ErrorCode::Io, "no se pudo escribir la cabecera del MP4: " + av_err(hr), utf8);
    }
    impl->header = true;

    // RGBA → yuv420p con la matriz BT.709 y rango TV (BT709_FINAL_FILTER), con los flags de
    // swscale por defecto del CLI (bicúbico).
    impl->sws.reset(sws_getContext(settings.width, settings.height, AV_PIX_FMT_RGBA, settings.width, settings.height,
                                   impl->vctx->pix_fmt, SWS_BICUBIC, nullptr, nullptr, nullptr));
    if (!impl->sws) {
        return fail(ErrorCode::Internal, "no se pudo preparar la conversión a YUV");
    }
    const int* bt709 = sws_getCoefficients(SWS_CS_ITU709);
    sws_setColorspaceDetails(impl->sws.get(), sws_getCoefficients(SWS_CS_DEFAULT), 1, bt709, 0, 0, 1 << 16, 1 << 16);
    impl->vframe.reset(av_frame_alloc());
    impl->vframe->format = impl->vctx->pix_fmt;
    impl->vframe->width = settings.width;
    impl->vframe->height = settings.height;
    impl->vframe->colorspace = AVCOL_SPC_BT709;
    impl->vframe->color_primaries = AVCOL_PRI_BT709;
    impl->vframe->color_trc = AVCOL_TRC_BT709;
    impl->vframe->color_range = AVCOL_RANGE_MPEG;
    if (av_frame_get_buffer(impl->vframe.get(), 0) < 0) {
        return fail(ErrorCode::Internal, "sin memoria para el vídeo");
    }
    impl->pkt.reset(av_packet_alloc());
    return Mp4Writer(std::move(impl));
}

Result<void> Mp4Writer::video(std::span<const std::uint8_t> rgba) {
    Impl& s = *impl_;
    const auto row = static_cast<std::size_t>(s.settings.width) * 4;
    if (rgba.size() != row * static_cast<std::size_t>(s.settings.height)) {
        return fail(ErrorCode::InvalidArgument, std::format("fotograma de {} bytes para una salida de {}x{}",
                                                            rgba.size(), s.settings.width, s.settings.height));
    }
    if (av_frame_make_writable(s.vframe.get()) < 0) {
        return fail(ErrorCode::Internal, "sin memoria para el vídeo");
    }
    const std::uint8_t* src[] = {rgba.data()};
    const int stride[] = {static_cast<int>(row)};
    sws_scale(s.sws.get(), src, stride, 0, s.settings.height, s.vframe->data, s.vframe->linesize);
    s.vframe->pts = s.vcount++;
    if (const int r = avcodec_send_frame(s.vctx.get(), s.vframe.get()); r < 0) {
        return fail(ErrorCode::Io, "error al codificar el vídeo: " + av_err(r));
    }
    return s.drain(s.vctx.get(), s.vst);
}

Result<void> Mp4Writer::audio(std::span<const float> interleaved) {
    Impl& s = *impl_;
    if (!s.actx) {
        return fail(ErrorCode::InvalidArgument, "este MP4 no lleva audio");
    }
    const std::size_t n = interleaved.size() / 2;
    std::vector<float> left(n);
    std::vector<float> right(n);
    for (std::size_t i = 0; i < n; ++i) {
        left[i] = interleaved[2 * i];
        right[i] = interleaved[(2 * i) + 1];
    }
    void* planes[] = {left.data(), right.data()};
    if (av_audio_fifo_write(s.fifo.get(), planes, static_cast<int>(n)) < static_cast<int>(n)) {
        return fail(ErrorCode::Internal, "sin memoria para la cola de audio");
    }
    const int frame = s.actx->frame_size > 0 ? s.actx->frame_size : 1024;
    while (av_audio_fifo_size(s.fifo.get()) >= frame) {
        if (auto r = s.send_audio(frame); !r) {
            return r;
        }
    }
    return {};
}

Result<void> Mp4Writer::finish() {
    Impl& s = *impl_;
    if (s.finished) {
        return {};
    }
    if (s.actx) {
        if (const int left = av_audio_fifo_size(s.fifo.get()); left > 0) { // el último, más corto
            if (auto r = s.send_audio(left); !r) {
                return r;
            }
        }
        avcodec_send_frame(s.actx.get(), nullptr);
        if (auto r = s.drain(s.actx.get(), s.ast); !r) {
            return r;
        }
    }
    avcodec_send_frame(s.vctx.get(), nullptr);
    if (auto r = s.drain(s.vctx.get(), s.vst); !r) {
        return r;
    }
    s.finished = true;
    if (const int r = av_write_trailer(s.fmt); r < 0) {
        return fail(ErrorCode::Io, "no se pudo cerrar el MP4: " + av_err(r));
    }
    return {};
}

} // namespace gc::media
