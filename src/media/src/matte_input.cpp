#include "matte_input.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "gc/core/io.hpp"
#include "input_args.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
}

namespace gc::media::detail {

namespace {

struct FormatCloser {
    void operator()(AVFormatContext* c) const { avformat_close_input(&c); }
};
struct CodecFree {
    void operator()(AVCodecContext* c) const { avcodec_free_context(&c); }
};
struct FrameFree {
    void operator()(AVFrame* f) const { av_frame_free(&f); }
};
struct PacketFree {
    void operator()(AVPacket* p) const { av_packet_free(&p); }
};
using FramePtr = std::unique_ptr<AVFrame, FrameFree>;

constexpr std::size_t kStartRange = 5; // start_number_range del demuxer image2

// Un PNG crudo del matte, decodificado como lo decodifica FFmpeg y derivado: la LUT o, si el clip
// lleva pluma, expansión, opacidad o pincel, derive_matte entero.
Result<FramePtr> load(const std::filesystem::path& path, const core::MatteSpec& spec) {
    const std::string utf8 = core::path_to_utf8(path);
    AVFormatContext* raw = nullptr;
    if (const int r = avformat_open_input(&raw, utf8.c_str(), nullptr, nullptr); r < 0) {
        return fail(ErrorCode::Io, "no se pudo abrir el matte: " + av_err(r), utf8);
    }
    const std::unique_ptr<AVFormatContext, FormatCloser> fmt(raw);
    if (const int r = avformat_find_stream_info(fmt.get(), nullptr); r < 0) {
        return fail(ErrorCode::Io, "no se pudo leer el matte: " + av_err(r), utf8);
    }
    const AVCodec* codec = nullptr;
    const int stream = av_find_best_stream(fmt.get(), AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
    if (stream < 0 || codec == nullptr) {
        return fail(ErrorCode::Io, "el matte no es una imagen", utf8);
    }
    const std::unique_ptr<AVCodecContext, CodecFree> dec(avcodec_alloc_context3(codec));
    if (!dec || avcodec_parameters_to_context(dec.get(), fmt->streams[stream]->codecpar) < 0 ||
        avcodec_open2(dec.get(), codec, nullptr) < 0) {
        return fail(ErrorCode::Io, "no se pudo abrir el decodificador del matte", utf8);
    }
    const std::unique_ptr<AVPacket, PacketFree> pkt(av_packet_alloc());
    FramePtr frame(av_frame_alloc());
    for (;;) {
        const int rr = av_read_frame(fmt.get(), pkt.get());
        if (rr < 0) {
            static_cast<void>(avcodec_send_packet(dec.get(), nullptr)); // el final: vaciar
        } else {
            if (pkt->stream_index == stream) {
                static_cast<void>(avcodec_send_packet(dec.get(), pkt.get()));
            }
            av_packet_unref(pkt.get());
        }
        const int r = avcodec_receive_frame(dec.get(), frame.get());
        if (r == 0) {
            break;
        }
        if (r != AVERROR(EAGAIN) || rr < 0) {
            return fail(ErrorCode::Io, "el matte no tiene ningún fotograma", utf8);
        }
    }
    if (frame->format != AV_PIX_FMT_GRAY8) {
        return fail(ErrorCode::Unsupported, "el matte crudo no es un PNG en gris de 8 bits", utf8);
    }
    if (av_frame_make_writable(frame.get()) < 0) {
        return fail(ErrorCode::Internal, "sin memoria para el matte");
    }
    const auto row = [&](int y) { return frame->data[0] + (static_cast<std::ptrdiff_t>(y) * frame->linesize[0]); };
    if (spec.lut_only) {
        for (int y = 0; y < frame->height; ++y) {
            std::uint8_t* r = row(y);
            for (int x = 0; x < frame->width; ++x) {
                r[x] = spec.lut[r[x]];
            }
        }
        return frame;
    }
    const auto w = static_cast<std::size_t>(frame->width);
    std::vector<std::uint8_t> crude(w * static_cast<std::size_t>(frame->height));
    for (int y = 0; y < frame->height; ++y) {
        std::memcpy(crude.data() + (w * static_cast<std::size_t>(y)), row(y), w);
    }
    const auto derived = core::derive_matte(crude, frame->width, frame->height, spec.settings);
    for (int y = 0; y < frame->height; ++y) {
        std::memcpy(row(y), derived.data() + (w * static_cast<std::size_t>(y)), w);
    }
    return frame;
}

} // namespace

struct MatteFeed::Impl {
    core::MatteSpec spec;
    std::vector<std::int64_t> raw; // el fotograma crudo de cada uno de la secuencia
    std::int64_t fps = 15;
    bool loop = false;
    std::int64_t last_pts = 0; // con -stream_loop, el último que se da
    std::size_t pos = 0;
    std::int64_t pts = 0;
    FramePtr first;
};

MatteFeed::MatteFeed(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
MatteFeed::MatteFeed(MatteFeed&&) noexcept = default;
MatteFeed& MatteFeed::operator=(MatteFeed&&) noexcept = default;
MatteFeed::~MatteFeed() = default;

Result<MatteFeed> MatteFeed::open(const core::MatteSpec& spec) {
    auto impl = std::make_unique<Impl>();
    impl->spec = spec;
    impl->fps = spec.mask_fps;
    impl->loop = spec.loop;
    // En el export, el `-t` de la salida corta la entrada en bucle; aquí, sin él, no acabaría. Lo
    // que pasa del final del clip (más un fotograma) ya no se ve: alphamerge repite el último.
    impl->last_pts = static_cast<std::int64_t>(std::ceil(std::max(0.0, spec.duration) *
                                                         static_cast<double>(std::max<std::int64_t>(1, spec.mask_fps)))) +
                     1;
    // find_image_range: el primero de los 5 primeros que existe y, desde él, hasta el primero que falta.
    const auto& s = spec.sources;
    std::size_t start = 0;
    while (start < s.size() && start < kStartRange && !s[start]) {
        ++start;
    }
    if (start >= s.size() || !s[start]) {
        return fail(ErrorCode::NotFound, "no hay ningún fotograma del matte al principio de su tramo",
                    core::path_to_utf8(spec.folder));
    }
    for (std::size_t k = start; k < s.size() && s[k]; ++k) {
        impl->raw.push_back(*s[k]);
    }
    auto first = load(spec.folder / core::matte_frame_name(impl->raw.front()), impl->spec);
    if (!first) {
        return std::unexpected(first.error());
    }
    impl->first = std::move(*first);
    return MatteFeed(std::move(impl));
}

std::int64_t MatteFeed::fps() const noexcept {
    return impl_->fps;
}

const AVFrame& MatteFeed::first() const noexcept {
    return *impl_->first;
}

Result<AVFrame*> MatteFeed::next() {
    Impl& s = *impl_;
    if (s.loop && s.pts > s.last_pts) {
        return nullptr;
    }
    if (s.pos >= s.raw.size()) {
        if (!s.loop) {
            return nullptr;
        }
        s.pos = 0; // -stream_loop -1: otra vuelta, con las marcas siguiendo
    }
    FramePtr f;
    if (s.pos == 0) {
        f.reset(av_frame_clone(s.first.get()));
        if (!f) {
            return fail(ErrorCode::Internal, "sin memoria para el matte");
        }
    } else {
        auto loaded = load(s.spec.folder / core::matte_frame_name(s.raw[s.pos]), s.spec);
        if (!loaded) {
            return std::unexpected(loaded.error());
        }
        f = std::move(*loaded);
    }
    f->pts = s.pts++;
    f->duration = 1; // la del paquete de image2 (el fin de la entrada es el último más uno)
    ++s.pos;
    return f.release();
}

} // namespace gc::media::detail
