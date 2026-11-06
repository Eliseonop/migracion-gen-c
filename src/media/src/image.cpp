#include "gc/media/image.hpp"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <memory>
#include <utility>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/mem.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

namespace gc::media {

namespace {

// La lectura de AVIO sobre los bytes en memoria (con salto, que lo piden TIFF y algunos demuxers).
struct Memory {
    std::span<const std::uint8_t> data;
    std::int64_t pos = 0;
};

int read_memory(void* opaque, std::uint8_t* buf, int size) {
    auto* m = static_cast<Memory*>(opaque);
    const auto left = static_cast<std::int64_t>(m->data.size()) - m->pos;
    if (left <= 0) {
        return AVERROR_EOF;
    }
    const auto n = static_cast<int>(std::min<std::int64_t>(left, size));
    std::memcpy(buf, m->data.data() + m->pos, static_cast<std::size_t>(n));
    m->pos += n;
    return n;
}

std::int64_t seek_memory(void* opaque, std::int64_t offset, int whence) {
    auto* m = static_cast<Memory*>(opaque);
    const auto size = static_cast<std::int64_t>(m->data.size());
    if ((whence & AVSEEK_SIZE) != 0) {
        return size;
    }
    std::int64_t to = offset;
    if ((whence & 0x3) == SEEK_CUR) {
        to = m->pos + offset;
    } else if ((whence & 0x3) == SEEK_END) {
        to = size + offset;
    }
    if (to < 0 || to > size) {
        return -1;
    }
    m->pos = to;
    return to;
}

struct AvioFree {
    void operator()(AVIOContext* c) const {
        if (c != nullptr) {
            av_freep(static_cast<void*>(&c->buffer));
        }
        avio_context_free(&c);
    }
};
struct FormatFree {
    void operator()(AVFormatContext* f) const { avformat_close_input(&f); }
};
struct CodecFree {
    void operator()(AVCodecContext* c) const { avcodec_free_context(&c); }
};
struct PacketFree {
    void operator()(AVPacket* p) const { av_packet_free(&p); }
};
struct FrameFree {
    void operator()(AVFrame* f) const { av_frame_free(&f); }
};
struct SwsFree {
    void operator()(SwsContext* s) const { sws_freeContext(s); }
};

// El primer fotograma que da el decodificador (con la cola vaciada al final del archivo).
bool first_frame(AVFormatContext* fmt, AVCodecContext* dec, int stream, AVFrame* frame) {
    const std::unique_ptr<AVPacket, PacketFree> pkt(av_packet_alloc());
    if (!pkt) {
        return false;
    }
    while (av_read_frame(fmt, pkt.get()) >= 0) {
        const bool mine = pkt->stream_index == stream;
        const int sent = mine ? avcodec_send_packet(dec, pkt.get()) : 0;
        av_packet_unref(pkt.get());
        if (sent < 0) {
            continue;
        }
        if (mine && avcodec_receive_frame(dec, frame) == 0) {
            return true;
        }
    }
    avcodec_send_packet(dec, nullptr);
    return avcodec_receive_frame(dec, frame) == 0;
}

// Los formatos que lee cv2.imdecode (los códecs de imagen que trae OpenCV): ni AVIF ni HEIC (AV1 y
// HEVC), ni el primer fotograma de un vídeo, que FFmpeg sí sabría sacar.
bool opencv_reads(AVCodecID id) {
    switch (id) {
    case AV_CODEC_ID_PNG:
    case AV_CODEC_ID_MJPEG:
    case AV_CODEC_ID_WEBP:
    case AV_CODEC_ID_BMP:
    case AV_CODEC_ID_TIFF:
    case AV_CODEC_ID_GIF:
    case AV_CODEC_ID_PPM:
    case AV_CODEC_ID_PGM:
    case AV_CODEC_ID_PBM:
    case AV_CODEC_ID_PAM:
    case AV_CODEC_ID_SUNRAST:
    case AV_CODEC_ID_EXR:
    case AV_CODEC_ID_RADIANCE_HDR:
    case AV_CODEC_ID_JPEG2000:
        return true;
    default:
        return false;
    }
}

// yuvj* → yuv* (con `full` a true): los mismos planos.
AVPixelFormat without_j(AVPixelFormat f, bool& full) {
    switch (f) {
    case AV_PIX_FMT_YUVJ420P:
        full = true;
        return AV_PIX_FMT_YUV420P;
    case AV_PIX_FMT_YUVJ422P:
        full = true;
        return AV_PIX_FMT_YUV422P;
    case AV_PIX_FMT_YUVJ444P:
        full = true;
        return AV_PIX_FMT_YUV444P;
    case AV_PIX_FMT_YUVJ440P:
        full = true;
        return AV_PIX_FMT_YUV440P;
    case AV_PIX_FMT_YUVJ411P:
        full = true;
        return AV_PIX_FMT_YUV411P;
    default:
        return f;
    }
}

// La copia de un fotograma YUV planar de 8 bits con submuestreo y algún lado impar, a lo par: los
// planos de luma y alfa repiten su última columna y fila; los de croma ya miden ceil(n/2), que es
// la mitad de lo par. Nada si no hace falta (o el formato no es ese).
AVFrame* pad_even(const AVFrame& f, const AVPixFmtDescriptor& desc) {
    const bool sub_w = desc.log2_chroma_w > 0 && (f.width % 2) != 0;
    const bool sub_h = desc.log2_chroma_h > 0 && (f.height % 2) != 0;
    if ((!sub_w && !sub_h) || (desc.flags & AV_PIX_FMT_FLAG_PLANAR) == 0 || (desc.flags & AV_PIX_FMT_FLAG_RGB) != 0 ||
        desc.comp[0].depth != 8) {
        return nullptr;
    }
    AVFrame* p = av_frame_alloc();
    if (p == nullptr) {
        return nullptr;
    }
    p->format = f.format;
    p->width = f.width + (sub_w ? 1 : 0);
    p->height = f.height + (sub_h ? 1 : 0);
    if (av_frame_get_buffer(p, 0) < 0) {
        av_frame_free(&p);
        return nullptr;
    }
    const int planes = av_pix_fmt_count_planes(static_cast<AVPixelFormat>(f.format));
    for (int i = 0; i < planes; ++i) {
        const bool chroma = i == 1 || i == 2;
        const int w = chroma ? AV_CEIL_RSHIFT(f.width, desc.log2_chroma_w) : f.width;
        const int h = chroma ? AV_CEIL_RSHIFT(f.height, desc.log2_chroma_h) : f.height;
        const int pw = chroma ? AV_CEIL_RSHIFT(p->width, desc.log2_chroma_w) : p->width;
        const int ph = chroma ? AV_CEIL_RSHIFT(p->height, desc.log2_chroma_h) : p->height;
        for (int y = 0; y < ph; ++y) {
            const std::uint8_t* from = f.data[i] + (static_cast<std::ptrdiff_t>(std::min(y, h - 1)) * f.linesize[i]);
            std::uint8_t* to = p->data[i] + (static_cast<std::ptrdiff_t>(y) * p->linesize[i]);
            std::memcpy(to, from, static_cast<std::size_t>(w));
            for (int x = w; x < pw; ++x) {
                to[x] = from[w - 1];
            }
        }
    }
    p->color_range = f.color_range;
    p->colorspace = f.colorspace;
    p->chroma_location = f.chroma_location;
    return p;
}

} // namespace

std::optional<RgbaFrame> decode_image(std::span<const std::uint8_t> data) {
    if (data.empty()) {
        return std::nullopt;
    }
    Memory mem{data, 0};
    constexpr int kBuf = 64 * 1024;
    auto* buffer = static_cast<std::uint8_t*>(av_malloc(kBuf));
    if (buffer == nullptr) {
        return std::nullopt;
    }
    const std::unique_ptr<AVIOContext, AvioFree> avio(
        avio_alloc_context(buffer, kBuf, 0, &mem, read_memory, nullptr, seek_memory));
    if (!avio) {
        av_free(buffer);
        return std::nullopt;
    }
    AVFormatContext* raw = avformat_alloc_context();
    if (raw == nullptr) {
        return std::nullopt;
    }
    raw->pb = avio.get();
    raw->flags |= AVFMT_FLAG_CUSTOM_IO;
    if (avformat_open_input(&raw, nullptr, nullptr, nullptr) < 0) {
        return std::nullopt; // avformat_open_input libera el contexto si falla
    }
    const std::unique_ptr<AVFormatContext, FormatFree> fmt(raw);
    if (avformat_find_stream_info(fmt.get(), nullptr) < 0) {
        return std::nullopt;
    }
    const AVCodec* codec = nullptr;
    const int stream = av_find_best_stream(fmt.get(), AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
    if (stream < 0 || codec == nullptr || !opencv_reads(codec->id)) {
        return std::nullopt;
    }
    const std::unique_ptr<AVCodecContext, CodecFree> dec(avcodec_alloc_context3(codec));
    if (!dec || avcodec_parameters_to_context(dec.get(), fmt->streams[stream]->codecpar) < 0 ||
        avcodec_open2(dec.get(), codec, nullptr) < 0) {
        return std::nullopt;
    }
    const std::unique_ptr<AVFrame, FrameFree> frame(av_frame_alloc());
    if (!frame || !first_frame(fmt.get(), dec.get(), stream, frame.get()) || frame->width <= 0 ||
        frame->height <= 0) {
        return std::nullopt;
    }
    // Los formatos «J» (el rango completo del JPEG) como su YUV con el rango dicho aparte: swscale
    // los da por obsoletos.
    bool full_range = frame->color_range == AVCOL_RANGE_JPEG;
    const auto src_format = without_j(static_cast<AVPixelFormat>(frame->format), full_range);
    const AVPixFmtDescriptor* src_desc = av_pix_fmt_desc_get(src_format);
    if (src_desc == nullptr) {
        return std::nullopt;
    }
    const int width = frame->width;
    const int height = frame->height;
    // Con un lado impar y el croma submuestreado, swscale repartiría las ceil(n/2) muestras de croma
    // sobre las n de luma (y se desplazan hacia el borde); libjpeg y libwebp las doblan exactas. Así
    // que se convierte a lo par (la luma repite su última fila o columna) y se recorta.
    const AVFrame* src = frame.get();
    const std::unique_ptr<AVFrame, FrameFree> padded(pad_even(*frame, *src_desc));
    if (padded) {
        src = padded.get();
    }
    // Sin escalar. El croma submuestreado, en bilineal (con la posición de la muestra que swscale da
    // por defecto): el «fancy upsampling» de libjpeg y libwebp, que es lo que usa OpenCV. En bicúbico
    // se aleja (40 dB en vez de 52).
    const std::unique_ptr<SwsContext, SwsFree> sws(sws_alloc_context());
    if (!sws) {
        return std::nullopt;
    }
    av_opt_set_int(sws.get(), "srcw", src->width, 0);
    av_opt_set_int(sws.get(), "srch", src->height, 0);
    av_opt_set_int(sws.get(), "src_format", src_format, 0);
    av_opt_set_int(sws.get(), "dstw", src->width, 0);
    av_opt_set_int(sws.get(), "dsth", src->height, 0);
    av_opt_set_int(sws.get(), "dst_format", AV_PIX_FMT_RGBA, 0);
    av_opt_set_int(sws.get(), "sws_flags", SWS_BILINEAR | SWS_FULL_CHR_H_INT | SWS_ACCURATE_RND, 0);
    if (sws_init_context(sws.get(), nullptr, nullptr) < 0) {
        return std::nullopt;
    }
    // El rango completo del JPEG (y de lo que lo diga).
    int* inv = nullptr;
    int src_range = 0;
    int* table = nullptr;
    int dst_range = 0;
    int brightness = 0;
    int contrast = 0;
    int saturation = 0;
    if (sws_getColorspaceDetails(sws.get(), &inv, &src_range, &table, &dst_range, &brightness, &contrast,
                                 &saturation) >= 0 &&
        full_range) {
        sws_setColorspaceDetails(sws.get(), inv, 1, table, dst_range, brightness, contrast, saturation);
    }
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(src->width) * static_cast<std::size_t>(src->height) * 4);
    std::uint8_t* const dst[4] = {rgba.data(), nullptr, nullptr, nullptr}; // NOLINT(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays): la API de swscale
    const int stride[4] = {src->width * 4, 0, 0, 0}; // NOLINT(cppcoreguidelines-avoid-c-arrays,modernize-avoid-c-arrays): la API de swscale
    if (sws_scale(sws.get(), src->data, src->linesize, 0, src->height, dst, stride) != src->height) {
        return std::nullopt;
    }
    RgbaFrame out;
    out.width = width;
    out.height = height;
    if (src->width == width && src->height == height) {
        out.pixels = std::move(rgba);
    } else {
        const auto row = static_cast<std::size_t>(width) * 4;
        out.pixels.resize(row * static_cast<std::size_t>(height));
        for (int y = 0; y < height; ++y) {
            std::memcpy(out.pixels.data() + (row * static_cast<std::size_t>(y)),
                        rgba.data() + (static_cast<std::size_t>(src->width) * 4 * static_cast<std::size_t>(y)), row);
        }
    }
    // Un TIFF con alfa: libtiff (TIFFReadRGBA, el de OpenCV) da el color premultiplicado, con su
    // redondeo ((c·a + 127) / 255); FFmpeg lo deja como está guardado.
    if (dec->codec_id == AV_CODEC_ID_TIFF && src_desc != nullptr && (src_desc->flags & AV_PIX_FMT_FLAG_ALPHA) != 0) {
        for (std::size_t i = 0; i < out.pixels.size(); i += 4) {
            const unsigned a = out.pixels[i + 3];
            for (std::size_t c = 0; c < 3; ++c) {
                out.pixels[i + c] = static_cast<std::uint8_t>(((out.pixels[i + c] * a) + 127U) / 255U);
            }
        }
    }
    return out;
}

} // namespace gc::media
