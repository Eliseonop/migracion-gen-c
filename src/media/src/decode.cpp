#include "gc/media/decode.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <exception>
#include <format>
#include <memory>
#include <mutex>
#include <optional>
#include <ranges>
#include <string_view>
#include <thread>
#include <vector>

#include "gc/core/bg.hpp"
#include "gc/core/export_input.hpp"
#include "gc/core/io.hpp"
#include "gc/core/timeline_rules.hpp"
#include "gc/media/probe.hpp"
#include "input_args.hpp"
#include "matte_input.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavformat/avformat.h>
#include <libavutil/display.h>
#include <libavutil/error.h>
#include <libavutil/hwcontext.h>
#include <libavutil/md5.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
}

namespace gc::media {




namespace {

struct FormatCloser {
    void operator()(AVFormatContext* c) const { avformat_close_input(&c); }
};
struct CodecFree {
    void operator()(AVCodecContext* c) const { avcodec_free_context(&c); }
};
struct GraphFree {
    void operator()(AVFilterGraph* g) const { avfilter_graph_free(&g); }
};
struct FrameFree {
    void operator()(AVFrame* f) const { av_frame_free(&f); }
};
struct PacketFree {
    void operator()(AVPacket* p) const { av_packet_free(&p); }
};
struct InOutFree {
    void operator()(AVFilterInOut* io) const { avfilter_inout_free(&io); }
};

using detail::av_err;
using detail::InputOptions;
using detail::parse_input_args;

// `-t` de una entrada: el CLI le pone a su salida un `trim` con la duración en µs.
// En texto, una duración sin unidad son segundos: el valor va con «us».
std::string duration_trim(std::int64_t us) { return std::format("trim=duration={}us,", us); }

// El autogiro del CLI (configure_input_video_filter de fftools/ffmpeg_filter.c, con get_rotation de
// cmdutils.c): los vídeos de móvil guardan el giro en la matriz de visualización del contenedor, que
// libavcodec pega a cada fotograma, y el CLI lo deshace con filtros delante de todo lo demás (también
// del trim de -ss y -t). Lo decide la matriz del PRIMER fotograma, la que configura el grafo.
std::string autorotate_filters(const AVFrame& first) {
    const AVFrameSideData* sd = av_frame_get_side_data(&first, AV_FRAME_DATA_DISPLAYMATRIX);
    if (sd == nullptr || sd->size < sizeof(std::int32_t) * 9) {
        return {}; // sin matriz, el CLI la deja a ceros: el ángulo es NaN y no inserta nada
    }
    std::array<std::int32_t, 9> m{};
    std::memcpy(m.data(), sd->data, sizeof(std::int32_t) * 9);
    double theta = -std::round(av_display_rotation_get(m.data()));
    theta -= 360 * std::floor((theta / 360) + (0.9 / 360));
    if (std::abs(theta - 90) < 1.0) {
        return m[3] > 0 ? "transpose=cclock_flip," : "transpose=clock,";
    }
    if (std::abs(theta - 180) < 1.0) {
        return std::string(m[0] < 0 ? "hflip," : "") + (m[4] < 0 ? "vflip," : "");
    }
    if (std::abs(theta - 270) < 1.0) {
        return m[3] < 0 ? "transpose=clock_flip," : "transpose=cclock,";
    }
    if (std::abs(theta) > 1.0) {
        return std::format("rotate={:f}*PI/180,", theta); // "%f*PI/180"
    }
    if (std::abs(theta) < 1.0 && m[4] < 0) {
        return "vflip,";
    }
    return {};
}

// Decode por hardware (DecodeSpec::hwaccel): los tipos de dispositivo que se prueban, en orden.
constexpr std::array kHwDevices = {AV_HWDEVICE_TYPE_D3D11VA, AV_HWDEVICE_TYPE_CUDA, AV_HWDEVICE_TYPE_VAAPI,
                                   AV_HWDEVICE_TYPE_VIDEOTOOLBOX};

// Un dispositivo por tipo para todo el proceso (crear uno de D3D11 o CUDA cuesta, y cada clip, cada
// salto atrás y cada reapertura abren otro decodificador): se crea la primera vez y no se libera. Los
// de FFmpeg se pueden usar desde varios hilos (D3D11VA lleva su cerrojo). Nada si no se puede crear.
AVBufferRef* shared_hw_device(AVHWDeviceType type) {
    static std::mutex mutex;
    static std::array<AVBufferRef*, kHwDevices.size()> devices{};
    static std::array<bool, kHwDevices.size()> tried{};
    std::size_t i = 0; // (con MSVC, el iterador de std::array no es un puntero: un bucle)
    while (i + 1 < kHwDevices.size() && kHwDevices.at(i) != type) {
        ++i;
    }
    const std::scoped_lock lock(mutex);
    if (!tried.at(i)) {
        tried.at(i) = true;
        if (av_hwdevice_ctx_create(&devices.at(i), type, nullptr, nullptr, 0) < 0) {
            devices.at(i) = nullptr;
        }
    }
    return devices.at(i) != nullptr ? av_buffer_ref(devices.at(i)) : nullptr;
}

// El fotograma bajado de la GPU (NV12 o P010) al formato del decodificador por software (YUV420P,
// YUVJ420P o YUV420P10), sin pérdidas: la GPU da los mismos YUV que el software, pero swscale convierte NV12 a
// RGBA por otro camino que YUV420P y no salen los mismos píxeles. Copia también las propiedades.
// Falso si no es uno de esos dos pares.
bool to_software_format(const AVFrame& src, AVPixelFormat want, AVFrame& dst) {
    const auto from = static_cast<AVPixelFormat>(src.format);
    // YUVJ420P (H.264 de rango completo) tiene los mismos planos que YUV420P.
    const bool nv12 = from == AV_PIX_FMT_NV12 && (want == AV_PIX_FMT_YUV420P || want == AV_PIX_FMT_YUVJ420P);
    const bool p010 = from == AV_PIX_FMT_P010LE && want == AV_PIX_FMT_YUV420P10LE;
    if (!nv12 && !p010) {
        return false;
    }
    dst.format = want;
    dst.width = src.width;
    dst.height = src.height;
    if (av_frame_get_buffer(&dst, 0) < 0 || av_frame_copy_props(&dst, &src) < 0) {
        return false;
    }
    const std::ptrdiff_t cw = (src.width + 1) / 2;
    const int ch = (src.height + 1) / 2;
    if (nv12) {
        for (int y = 0; y < src.height; ++y) {
            std::memcpy(dst.data[0] + static_cast<std::ptrdiff_t>(y) * dst.linesize[0],
                        src.data[0] + static_cast<std::ptrdiff_t>(y) * src.linesize[0], static_cast<std::size_t>(src.width));
        }
        for (int y = 0; y < ch; ++y) {
            const std::uint8_t* uv = src.data[1] + static_cast<std::ptrdiff_t>(y) * src.linesize[1];
            std::uint8_t* u = dst.data[1] + static_cast<std::ptrdiff_t>(y) * dst.linesize[1];
            std::uint8_t* v = dst.data[2] + static_cast<std::ptrdiff_t>(y) * dst.linesize[2];
            for (std::ptrdiff_t x = 0; x < cw; ++x) {
                u[x] = uv[2 * x];
                v[x] = uv[(2 * x) + 1];
            }
        }
        return true;
    }
    // P010: 10 bits en los bits altos de cada muestra de 16; YUV420P10: en los bajos.
    const auto row16 = [](const AVFrame& f, int plane, int y) {
        return reinterpret_cast<const std::uint16_t*>(f.data[plane] + static_cast<std::ptrdiff_t>(y) * f.linesize[plane]);
    };
    const auto out16 = [](AVFrame& f, int plane, int y) {
        return reinterpret_cast<std::uint16_t*>(f.data[plane] + static_cast<std::ptrdiff_t>(y) * f.linesize[plane]);
    };
    for (int y = 0; y < src.height; ++y) {
        const std::uint16_t* s = row16(src, 0, y);
        std::uint16_t* d = out16(dst, 0, y);
        for (int x = 0; x < src.width; ++x) {
            d[x] = static_cast<std::uint16_t>(s[x] >> 6U);
        }
    }
    for (int y = 0; y < ch; ++y) {
        const std::uint16_t* uv = row16(src, 1, y);
        std::uint16_t* u = out16(dst, 1, y);
        std::uint16_t* v = out16(dst, 2, y);
        for (std::ptrdiff_t x = 0; x < cw; ++x) {
            u[x] = static_cast<std::uint16_t>(uv[2 * x] >> 6U);
            v[x] = static_cast<std::uint16_t>(uv[(2 * x) + 1] >> 6U);
        }
    }
    return true;
}

class Decoder {
public:
    explicit Decoder(DecodeSpec spec) : spec_(std::move(spec)) {}
    Decoder(const Decoder&) = delete;
    Decoder& operator=(const Decoder&) = delete;
    Decoder(Decoder&&) = delete; // el decodificador guarda `this` (get_format)
    Decoder& operator=(Decoder&&) = delete;
    ~Decoder() = default;

    Result<void> start() {
        auto opts = parse_input_args(spec_.input_args);
        if (!opts) {
            return std::unexpected(opts.error());
        }
        opts_ = std::move(*opts);
        if (spec_.matte) {
            auto feed = detail::MatteFeed::open(*spec_.matte);
            if (!feed) {
                return std::unexpected(feed.error());
            }
            matte_.emplace(std::move(*feed));
        }
        pkt_.reset(av_packet_alloc());
        frame_.reset(av_frame_alloc());
        out_.reset(av_frame_alloc());
        return open();
    }

    // El siguiente fotograma de la cadena, o nada al final. Solo lee paquetes cuando el grafo los
    // necesita, así que no se acumulan fotogramas (ni los que repite el `fps`).
    Result<std::optional<RgbaFrame>> next() {
        while (!finished_) {
            if (graph_) {
                const int r = av_buffersink_get_frame(sink_, out_.get());
                if (r == 0) {
                    if (skip_ > 0) { // tras pasar a software: los que ya salieron de la GPU
                        --skip_;
                        av_frame_unref(out_.get());
                        continue;
                    }
                    ++count_;
                    return take();
                }
                if (r == AVERROR_EOF) {
                    finished_ = true; // el trim ya terminó: no hace falta leer más
                    break;
                }
                if (r != AVERROR(EAGAIN)) {
                    return fail(ErrorCode::Internal, "error en el grafo de filtros: " + av_err(r));
                }
                // La segunda entrada (el matte), cuando el grafo la pide o cuando ya no queda nada
                // más que mandarle, como el CLI, que lee de la entrada que el grafo espera.
                if (matte_src_ != nullptr && !matte_eof_ &&
                    (av_buffersrc_get_nb_failed_requests(matte_src_) > 0 || eof_sent_)) {
                    if (auto m = feed_matte(); !m) {
                        return std::unexpected(m.error());
                    }
                    continue;
                }
            }
            if (input_done_) {
                // Todo enviado y el grafo no da más: o no se decodificó nada (sin grafo), o ya
                // se le mandó el final.
                if (!graph_ || eof_sent_) {
                    finished_ = true;
                    break;
                }
                // Fin del grafo. Si falla, lo dirá el av_buffersink_get_frame de arriba.
                static_cast<void>(av_buffersrc_add_frame_flags(src_, nullptr, 0));
                eof_sent_ = true;
                continue;
            }
            if (auto r = feed(); !r) {
                return std::unexpected(r.error());
            }
            if (overshoot_) {
                // El salto previo cayó después de su instante: desde el principio, sin saltar.
                preroll_failed_ = true;
                if (auto r = reopen(); !r) {
                    return std::unexpected(r.error());
                }
            } else if (hw_reopen_) {
                // La GPU falló (hw_to_software): otra vez desde el principio, por software, saltando
                // lo que ya salió.
                skip_ = count_;
                if (auto r = reopen(); !r) {
                    return std::unexpected(r.error());
                }
            }
        }
        // Los paquetes rotos se saltan, pero si no se pudo decodificar nada es un error (el CLI
        // acaba sin salida y extract_frame lanza ValueError), no «no hay fotograma en ese instante».
        if (count_ == 0 && decode_errors_ > 0) {
            return fail(ErrorCode::Io, "no se pudo decodificar ningún fotograma", opts_.path);
        }
        return std::optional<RgbaFrame>{};
    }

private:
    // Un paquete más al decodificador (o, al final del archivo, vaciarlo) y lo que salga, al grafo.
    Result<void> feed() {
        const int rr = av_read_frame(fmt_.get(), pkt_.get());
        if (rr < 0) { // EOF o error de lectura: se vacía lo que quede
            input_done_ = true;
            return send(nullptr);
        }
        Result<void> r{};
        if (pkt_->stream_index == stream_) {
            r = send(pkt_.get());
        }
        av_packet_unref(pkt_.get());
        return r;
    }

    Result<void> open() {
        AVDictionary* dict = nullptr;
        av_dict_set(&dict, "scan_all_pmts", "1", AV_DICT_DONT_OVERWRITE);
        for (const auto& [k, v] : opts_.demuxer) {
            av_dict_set(&dict, k.c_str(), v.c_str(), 0);
        }
        AVFormatContext* raw = nullptr;
        const int rc = avformat_open_input(&raw, opts_.path.c_str(), nullptr, &dict);
        av_dict_free(&dict); // lo que el demuxer no usó
        if (rc < 0) {
            return fail(ErrorCode::Io, "no se pudo abrir el material: " + av_err(rc), opts_.path);
        }
        fmt_.reset(raw);
        if (const int r = avformat_find_stream_info(fmt_.get(), nullptr); r < 0) {
            return fail(ErrorCode::Io, "no se pudo leer el material: " + av_err(r), opts_.path);
        }
        const AVCodec* codec = nullptr;
        stream_ = av_find_best_stream(fmt_.get(), AVMEDIA_TYPE_VIDEO, -1, -1, &codec, 0);
        if (stream_ < 0) {
            return fail(ErrorCode::NotFound, "el material no tiene vídeo", opts_.path);
        }
        if (!opts_.decoder.empty()) {
            codec = avcodec_find_decoder_by_name(opts_.decoder.c_str());
            if (codec == nullptr) {
                return fail(ErrorCode::Unsupported, "este FFmpeg no tiene el decodificador " + opts_.decoder);
            }
        }
        const AVStream* st = fmt_->streams[stream_];
        // El CLI desplaza las marcas de tiempo de cada paquete con `ts_offset = −start_time` del
        // contenedor (sin -copyts), así que el `trim` de la cadena cuenta desde el principio del
        // archivo aunque el contenedor no empiece en 0.
        // Con -ss, el CLI salta a ese instante (más el start_time) y lo resta también: lo que
        // queda antes sale con marcas negativas y el `trim=starti=0` de la cadena lo descarta.
        const std::int64_t start = fmt_->start_time != AV_NOPTS_VALUE ? fmt_->start_time : 0;
        const std::int64_t seek = opts_.seek_us.value_or(0);
        if (opts_.seek_us) {
            const std::int64_t ts = seek + start;
            // Si falla, el CLI solo avisa y sigue desde el principio (el trim hace el resto).
            static_cast<void>(avformat_seek_file(fmt_.get(), -1, INT64_MIN, ts, ts, 0));
        }
        ts_offset_ = av_rescale_q(-(seek + start), AV_TIME_BASE_Q, st->time_base);
        // El salto previo (DecodeSpec::preroll_us): a un fotograma clave en o antes de ese instante,
        // sin tocar las marcas (la cadena ve los mismos tiempos que decodificando desde 0).
        preroll_pts_.reset();
        if (!opts_.seek_us && spec_.preroll_us && *spec_.preroll_us > 0 && !preroll_failed_) {
            const std::int64_t ts = *spec_.preroll_us + start;
            if (avformat_seek_file(fmt_.get(), -1, INT64_MIN, ts, ts, 0) >= 0) {
                preroll_pts_ = av_rescale_q(*spec_.preroll_us, AV_TIME_BASE_Q, st->time_base);
            }
        }
        dec_.reset(avcodec_alloc_context3(codec));
        if (!dec_ || avcodec_parameters_to_context(dec_.get(), st->codecpar) < 0) {
            return fail(ErrorCode::Internal, "no se pudo preparar el decodificador");
        }
        dec_->pkt_timebase = st->time_base;
        dec_->thread_count = 0; // automático, como el CLI
        hw_fmt_ = AV_PIX_FMT_NONE;
        if (spec_.hwaccel && !hw_failed_) {
            setup_hw(*codec);
        }
        if (const int r = avcodec_open2(dec_.get(), codec, nullptr); r < 0) {
            return fail(ErrorCode::Io, "no se pudo abrir el decodificador: " + av_err(r), opts_.path);
        }
        return {};
    }

    // El primer dispositivo de kHwDevices que el códec admita y que se pueda crear. Si ninguno, nada:
    // por software. Solo códecs cuyo decode es exacto por la norma (transformadas enteras): en MPEG-4
    // Part 2, MPEG-2, VC-1 o MJPEG la IDCT de la GPU no es la de FFmpeg y los fotogramas cambian.
    void setup_hw(const AVCodec& codec) {
        constexpr std::array kExact = {AV_CODEC_ID_H264, AV_CODEC_ID_HEVC, AV_CODEC_ID_VP8, AV_CODEC_ID_VP9,
                                       AV_CODEC_ID_AV1};
        if (std::ranges::find(kExact, codec.id) == kExact.end()) {
            return;
        }
        for (const AVHWDeviceType type : kHwDevices) {
            for (int i = 0;; ++i) {
                const AVCodecHWConfig* cfg = avcodec_get_hw_config(&codec, i);
                if (cfg == nullptr) {
                    break;
                }
                if ((cfg->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) == 0 || cfg->device_type != type) {
                    continue;
                }
                AVBufferRef* device = shared_hw_device(type);
                if (device == nullptr) {
                    break; // no hay ese dispositivo: el siguiente tipo
                }
                dec_->hw_device_ctx = device;
                dec_->opaque = this;
                dec_->get_format = pick_format;
                hw_fmt_ = cfg->pix_fmt;
                return;
            }
        }
    }

    // get_format: el formato de la GPU si el decodificador lo ofrece; si no (o si ya falló al
    // iniciarse, que libavcodec lo quita de la lista y vuelve a preguntar), el primero de software.
    static AVPixelFormat pick_format(AVCodecContext* ctx, const AVPixelFormat* fmts) {
        const auto* self = static_cast<const Decoder*>(ctx->opaque);
        for (const AVPixelFormat* p = fmts; *p != AV_PIX_FMT_NONE; ++p) {
            if (*p == self->hw_fmt_) {
                return *p;
            }
        }
        for (const AVPixelFormat* p = fmts; *p != AV_PIX_FMT_NONE; ++p) {
            if ((av_pix_fmt_desc_get(*p)->flags & AV_PIX_FMT_FLAG_HWACCEL) == 0) {
                return *p;
            }
        }
        return AV_PIX_FMT_NONE;
    }

    // Un error de decode (o un fotograma dañado o que no se puede bajar) por hardware: con datos rotos,
    // la GPU no da lo mismo que el software. Se vuelve a empezar por software (next()) y se saltan los
    // fotogramas que ya salieron, que hasta aquí eran iguales: la secuencia es la del software.
    void hw_to_software() {
        if (hw_fmt_ != AV_PIX_FMT_NONE && !hw_failed_) {
            hw_failed_ = true;
            hw_reopen_ = true;
        }
    }

    // frame_ en la GPU → en memoria y en el formato del software (to_software_format). Falso si no se
    // puede.
    bool download() {
        if (!hw_tmp_ || !hw_sw_) {
            hw_tmp_.reset(av_frame_alloc());
            hw_sw_.reset(av_frame_alloc());
            if (!hw_tmp_ || !hw_sw_) {
                return false;
            }
        }
        av_frame_unref(hw_tmp_.get());
        av_frame_unref(hw_sw_.get());
        if (av_hwframe_transfer_data(hw_tmp_.get(), frame_.get(), 0) < 0 ||
            av_frame_copy_props(hw_tmp_.get(), frame_.get()) < 0) {
            return false;
        }
        AVFrame* result = hw_tmp_.get();
        if (hw_tmp_->format != dec_->sw_pix_fmt) {
            if (!to_software_format(*hw_tmp_, dec_->sw_pix_fmt, *hw_sw_)) {
                return false;
            }
            result = hw_sw_.get();
        }
        av_frame_unref(frame_.get());
        av_frame_move_ref(frame_.get(), result);
        hw_used_ = true;
        return true;
    }

    // El CLI arma el grafo con los parámetros del primer fotograma decodificado.
    Result<void> build_graph(const AVFrame& first) {
        graph_.reset(avfilter_graph_alloc());
        if (!graph_) {
            return fail(ErrorCode::Internal, "sin memoria para el grafo de filtros");
        }
        av_opt_set(graph_.get(), "scale_sws_opts", "flags=bicubic", 0); // -sws_flags del CLI
        src_ = avfilter_graph_alloc_filter(graph_.get(), avfilter_get_by_name("buffer"), "in");
        sink_ = avfilter_graph_alloc_filter(graph_.get(), avfilter_get_by_name("buffersink"), "out");
        if (src_ == nullptr || sink_ == nullptr) {
            return fail(ErrorCode::Internal, "faltan los filtros buffer/buffersink");
        }
        AVBufferSrcParameters* par = av_buffersrc_parameters_alloc();
        if (par == nullptr) {
            return fail(ErrorCode::Internal, "sin memoria para la entrada del grafo");
        }
        par->format = first.format;
        par->width = first.width;
        par->height = first.height;
        par->time_base = fmt_->streams[stream_]->time_base;
        par->sample_aspect_ratio = first.sample_aspect_ratio;
        par->color_space = first.colorspace;
        par->color_range = first.color_range;
        const int pr = av_buffersrc_parameters_set(src_, par);
        av_free(par);
        if (pr < 0 || avfilter_init_str(src_, nullptr) < 0) {
            return fail(ErrorCode::Internal, "no se pudo preparar la entrada del grafo");
        }
        if (avfilter_init_str(sink_, nullptr) < 0) {
            return fail(ErrorCode::Internal, "no se pudo preparar la salida del grafo");
        }
        if (matte_) {
            // Los parámetros de la entrada image2: los del primer PNG, en 1/mask_fps (-framerate).
            const AVFrame& m = matte_->first();
            matte_src_ = avfilter_graph_alloc_filter(graph_.get(), avfilter_get_by_name("buffer"), "matte");
            AVBufferSrcParameters* mp = av_buffersrc_parameters_alloc();
            if (matte_src_ == nullptr || mp == nullptr) {
                av_free(mp);
                return fail(ErrorCode::Internal, "sin memoria para la entrada del matte");
            }
            mp->format = m.format;
            mp->width = m.width;
            mp->height = m.height;
            mp->time_base = AVRational{1, static_cast<int>(matte_->fps())};
            mp->sample_aspect_ratio = m.sample_aspect_ratio;
            mp->color_space = m.colorspace;
            mp->color_range = m.color_range;
            const int mr = av_buffersrc_parameters_set(matte_src_, mp);
            av_free(mp);
            if (mr < 0 || avfilter_init_str(matte_src_, nullptr) < 0) {
                return fail(ErrorCode::Internal, "no se pudo preparar la entrada del matte");
            }
        }
        // Lo que el CLI pone delante (autogiro, seek exacto y -t), la cadena del export y el formato
        // de salida.
        std::string desc = autorotate_filters(first) +
                           (opts_.seek_us ? std::string("trim=starti=0,") : std::string()) +
                           (opts_.duration_us ? duration_trim(*opts_.duration_us) : std::string()) + spec_.chain;
        if (!desc.empty() && desc.back() != ',') {
            desc += ',';
        }
        desc += "format=rgba";
        std::unique_ptr<AVFilterInOut, InOutFree> outputs(avfilter_inout_alloc());
        std::unique_ptr<AVFilterInOut, InOutFree> inputs(avfilter_inout_alloc());
        outputs->name = av_strdup("in");
        outputs->filter_ctx = src_;
        if (matte_src_ != nullptr) { // `[matte]` en la cadena
            AVFilterInOut* m = avfilter_inout_alloc();
            if (m == nullptr) {
                return fail(ErrorCode::Internal, "sin memoria para la entrada del matte");
            }
            m->name = av_strdup("matte");
            m->filter_ctx = matte_src_;
            outputs->next = m;
        }
        inputs->name = av_strdup("out");
        inputs->filter_ctx = sink_;
        AVFilterInOut* in_raw = inputs.release();
        AVFilterInOut* out_raw = outputs.release();
        const int r = avfilter_graph_parse_ptr(graph_.get(), desc.c_str(), &in_raw, &out_raw, nullptr);
        avfilter_inout_free(&in_raw);
        avfilter_inout_free(&out_raw);
        if (r < 0) {
            return fail(ErrorCode::InvalidArgument, "cadena de filtros no válida («" + desc + "»): " + av_err(r));
        }
        if (const int c = avfilter_graph_config(graph_.get(), nullptr); c < 0) {
            return fail(ErrorCode::Internal, "no se pudo configurar el grafo: " + av_err(c));
        }
        return {};
    }

    Result<void> send(const AVPacket* pkt) {
        // Como el CLI, un paquete que no se puede decodificar se salta y se sigue con el resto.
        if (const int r = avcodec_send_packet(dec_.get(), pkt); r < 0 && r != AVERROR_EOF) {
            ++decode_errors_;
            hw_to_software();
            return {};
        }
        while (true) {
            const int r = avcodec_receive_frame(dec_.get(), frame_.get());
            if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) {
                return {};
            }
            if (r < 0) {
                ++decode_errors_;
                hw_to_software();
                return {};
            }
            if (hw_fmt_ != AV_PIX_FMT_NONE && frame_->format == hw_fmt_) {
                // Un fotograma dañado: la GPU lo oculta a su manera, distinta del software.
                const bool damaged = frame_->decode_error_flags != 0 || (frame_->flags & AV_FRAME_FLAG_CORRUPT) != 0;
                if (damaged || !download()) {
                    av_frame_unref(frame_.get());
                    hw_to_software();
                    return {};
                }
            }
            // Lo que hace el CLI: la marca «mejor estimada», ya desplazada con ts_offset.
            frame_->pts = frame_->best_effort_timestamp == AV_NOPTS_VALUE
                              ? AV_NOPTS_VALUE
                              : frame_->best_effort_timestamp + ts_offset_;
            // Tras el salto previo, el primer fotograma tiene que quedar en o antes de su instante;
            // si no, faltaría lo de en medio: se vuelve a empezar sin saltar (next()).
            if (preroll_pts_) {
                if (frame_->pts == AV_NOPTS_VALUE || frame_->pts > *preroll_pts_) {
                    av_frame_unref(frame_.get());
                    overshoot_ = true;
                    return {};
                }
                preroll_pts_.reset();
            }
            if (!graph_) {
                if (auto g = build_graph(*frame_); !g) {
                    return g;
                }
            }
            if (const int a = av_buffersrc_add_frame_flags(src_, frame_.get(), 0); a < 0 && a != AVERROR_EOF) {
                return fail(ErrorCode::Internal, "error en el grafo de filtros: " + av_err(a));
            }
            av_frame_unref(frame_.get());
        }
    }

    // Un fotograma más del matte al grafo o, al final de la secuencia, su fin.
    Result<void> feed_matte() {
        auto next = matte_->next();
        if (!next) {
            return std::unexpected(next.error());
        }
        const std::unique_ptr<AVFrame, FrameFree> f(*next);
        if (!f) {
            matte_eof_ = true;
            static_cast<void>(av_buffersrc_add_frame_flags(matte_src_, nullptr, 0));
            return {};
        }
        if (const int a = av_buffersrc_add_frame_flags(matte_src_, f.get(), 0); a < 0 && a != AVERROR_EOF) {
            return fail(ErrorCode::Internal, "error en el grafo de filtros (matte): " + av_err(a));
        }
        return {};
    }

    // Desde el principio (tras el salto previo pasado o una GPU que falló), con lo que quede activo.
    Result<void> reopen() {
        overshoot_ = false;
        hw_reopen_ = false;
        hw_used_ = hw_used_ && !hw_failed_;
        graph_.reset();
        src_ = nullptr;
        sink_ = nullptr;
        matte_src_ = nullptr;
        matte_eof_ = false;
        if (spec_.matte) { // la secuencia del matte, también desde el principio
            auto feed = detail::MatteFeed::open(*spec_.matte);
            if (!feed) {
                return std::unexpected(feed.error());
            }
            matte_.emplace(std::move(*feed));
        }
        dec_.reset();
        fmt_.reset();
        decode_errors_ = 0;
        input_done_ = false;
        eof_sent_ = false;
        return open();
    }

    RgbaFrame take() {
        RgbaFrame f;
        f.width = out_->width;
        f.height = out_->height;
        f.hardware = hw_used_;
        const auto row = static_cast<std::size_t>(out_->width) * 4;
        f.pixels.resize(row * static_cast<std::size_t>(out_->height));
        for (int y = 0; y < out_->height; ++y) {
            std::memcpy(f.pixels.data() + row * static_cast<std::size_t>(y),
                        out_->data[0] + static_cast<std::ptrdiff_t>(y) * out_->linesize[0], row);
        }
        av_frame_unref(out_.get());
        return f;
    }

    DecodeSpec spec_;
    InputOptions opts_;
    std::unique_ptr<AVFormatContext, FormatCloser> fmt_;
    std::unique_ptr<AVCodecContext, CodecFree> dec_;
    std::unique_ptr<AVFilterGraph, GraphFree> graph_;
    std::unique_ptr<AVPacket, PacketFree> pkt_;
    std::unique_ptr<AVFrame, FrameFree> frame_;
    std::unique_ptr<AVFrame, FrameFree> out_;
    AVFilterContext* src_ = nullptr;
    AVFilterContext* sink_ = nullptr;
    std::optional<detail::MatteFeed> matte_; // la secuencia PNG del matte (DecodeSpec::matte)
    AVFilterContext* matte_src_ = nullptr;
    bool matte_eof_ = false;
    int stream_ = -1;
    std::int64_t ts_offset_ = 0; // en la base de tiempos del stream
    std::size_t decode_errors_ = 0;
    std::size_t count_ = 0;
    std::optional<std::int64_t> preroll_pts_; // el instante del salto previo, en la base del stream
    bool overshoot_ = false;                  // el salto previo se pasó: hay que reabrir sin él
    bool preroll_failed_ = false;
    AVPixelFormat hw_fmt_ = AV_PIX_FMT_NONE; // el de la GPU, si se decodifica por hardware
    std::unique_ptr<AVFrame, FrameFree> hw_tmp_; // el fotograma bajado de la GPU
    std::unique_ptr<AVFrame, FrameFree> hw_sw_;  // y en el formato del software
    bool hw_used_ = false;   // algún fotograma vino de la GPU
    bool hw_failed_ = false; // la GPU falló: por software desde ahí
    bool hw_reopen_ = false; // y hay que volver a empezar
    std::size_t skip_ = 0;   // fotogramas de la cadena que no se devuelven (ya salieron de la GPU)
    bool input_done_ = false;
    bool eof_sent_ = false;
    bool finished_ = false;
};

} // namespace

DecodeSpec export_decode_spec(const core::TimelineClip& clip, const std::filesystem::path& path, int fps) {
    const std::string p = core::path_to_utf8(path);
    DecodeSpec spec;
    spec.input_args = core::ffmpeg_input_args(clip, p, fps);
    const bool color_pre = !core::is_still_clip(clip) && color_untagged(path);
    spec.chain = (color_pre ? std::string(kAssumeBt709Filter) + "," : std::string()) + core::ffmpeg_trim_filter(clip, fps) +
                 ",setpts=PTS-STARTPTS";
    return spec;
}

Result<std::size_t> decode_frames(const DecodeSpec& spec, const std::function<bool(const RgbaFrame&)>& on_frame) {
    Decoder d(spec);
    if (auto r = d.start(); !r) {
        return std::unexpected(r.error());
    }
    std::size_t n = 0;
    while (true) {
        auto f = d.next();
        if (!f) {
            return std::unexpected(f.error());
        }
        if (!*f) {
            return n;
        }
        ++n;
        if (!on_frame(**f)) {
            return n;
        }
    }
}

Result<std::optional<RgbaFrame>> frame_at(const std::filesystem::path& path, double t) {
    // extract_frame: `t = max(0.0, float(at_time))` (un NaN da 0) y `f"{t:.3f}"`.
    const double ss = std::max(0.0, t);
    const DecodeSpec spec{{"-ss", std::format("{:.3f}", ss), "-i", core::path_to_utf8(path)}, ""};
    std::optional<RgbaFrame> out;
    auto n = decode_frames(spec, [&](const RgbaFrame& f) {
        out = f;
        return false; // -frames:v 1
    });
    if (!n) {
        return std::unexpected(n.error());
    }
    return out;
}

namespace {

double parse_double(const std::string& text) {
    double v = 0.0;
    std::from_chars(text.data(), text.data() + text.size(), v);
    return v;
}

} // namespace

std::int64_t export_frame_tick(double at, int fps) {
    // Acotado a [0, 1e12] s para que el entero no se desborde (el CLI ya rechaza más de 1e9).
    // render_frame: `-ss {max(0.0, float(at)):.3f}` a la SALIDA del grafo da el fotograma
    // compuesto más cercano (medido con el ffmpeg del PATH; un empate va al siguiente).
    const double ss = parse_double(std::format("{:.3f}", std::isnan(at) ? 0.0 : std::clamp(at, 0.0, 1e12)));
    return static_cast<std::int64_t>(std::floor(ss * fps + 0.5));
}

namespace {

// La cadena de un clip hasta el overlay y el pts (en ticks de 1/F) de cada fotograma que sale.
struct ClipChain {
    DecodeSpec spec;
    bool reverse = false;
    std::optional<double> speed;
    double offset = 0.0;

    // pts del m-ésimo fotograma que sale de la cadena; el primero tiene 0 antes del
    // desplazamiento, así que STARTPTS no resta nada.
    [[nodiscard]] std::int64_t pts_of(std::int64_t m) const {
        const std::int64_t scaled =
            speed ? static_cast<std::int64_t>(static_cast<double>(m) / *speed) : m; // setpts=PTS/sp
        return static_cast<std::int64_t>(static_cast<double>(scaled) + offset);     // +start/TB
    }
};

// `preroll`: con el salto previo (ClipFrames); export_frame_at, la referencia, decodifica desde 0.
Result<ClipChain> clip_chain(const core::TimelineClip& clip, const std::filesystem::path& path, int fps,
                             bool preroll, const core::MatteSpec* matte) {
    // compose.py, la cadena de cada clip:
    //   [k:v]{color_pre}{trim},setpts=PTS-STARTPTS,{cropscale},fps=F{spd},…,setpts=PTS-STARTPTS+{start:.3f}/TB
    // con spd = video_speed_filters (vacío en un still): «reverse» y `setpts=PTS/{sp:.6f}`. Las
    // setpts evalúan en doble y truncan a entero (D2TS de vf_setpts.c), en ticks de 1/F.
    ClipChain c;
    const bool still = core::is_still_clip(clip);
    c.reverse = !still && clip.reverse;
    if (const double sp = core::clip_speed(clip); !still && std::abs(sp - 1.0) > 1e-3) {
        c.speed = parse_double(std::format("{:.6f}", sp));
    }
    const double start = parse_double(std::format("{:.3f}", std::max(0.0, clip.start)));
    c.offset = start / (1.0 / static_cast<double>(fps)); // `{start:.3f}/TB`, con TB = 1/F
    if (!(c.offset < 1e15)) { // un pts que no cabe en un entero de 64 bits: el clip nunca se ve
        return fail(ErrorCode::InvalidArgument, std::format("el inicio del clip {} está fuera de rango", clip.id));
    }
    c.spec = export_decode_spec(clip, path, fps);
    // El salto previo: 1 s antes del tramo (el fps=F de la cadena mira los fotogramas de alrededor
    // de cada tick y el trim tira todo lo anterior). No en stills, GIF ni secuencias de imágenes.
    if (preroll && !still && !std::ranges::contains(c.spec.input_args, std::string("-ignore_loop")) &&
        !std::ranges::contains(c.spec.input_args, std::string("-start_number"))) {
        const double in = core::ffmpeg_trim_window(clip, fps).first;
        if (const double at = parse_double(std::format("{:.3f}", in)) - 1.0; at > 0 && at < 1e9) {
            c.spec.preroll_us = static_cast<std::int64_t>(std::llround(at * 1e6));
        }
    }
    // «Eliminar fondo»: en el espacio del material, antes del encuadre (_bg_source_chain).
    if (clip.bg_removal && core::bg_capable(clip)) {
        const Json bg = core::normalize_bg(*clip.bg_removal);
        if (matte != nullptr && core::bg_auto_active(bg)) {
            // El matte de la caché, subido al tamaño del material: el de detect.dims (aquí, el del
            // primer fotograma) o, si no se sabe, el del meta.json. Sin ninguno de los dos, el
            // export no aplica nada (ni el croma).
            std::int64_t iw = 0;
            std::int64_t ih = 0;
            if (auto first = frame_at(path, 0.0); first && first->has_value()) {
                iw = (*first)->width;
                ih = (*first)->height;
            }
            const std::int64_t probed_h = iw > 0 && ih > 0 ? ih : 0; // el contorno usa detect.dims(path)[1]
            if (iw <= 0 || ih <= 0) {
                iw = matte->width;
                ih = matte->height;
            }
            if (iw > 0 && ih > 0) {
                c.spec.matte = *matte;
                c.spec.chain += core::matte_source_chain(bg, iw, ih, probed_h, fps);
            }
        } else if (core::bg_chroma_active(bg)) {
            // El desenfoque del borde y el grosor del contorno van en px del alto del material
            // (detect.dims). Sin saberlo, el croma toma 512 y el contorno 0 (no se dibuja).
            std::optional<std::int64_t> height;
            if (!core::chroma_alpha_ffmpeg(bg["chroma"], 512).empty() || core::outline_params(bg["outline"], 1e9)) {
                auto first = frame_at(path, 0.0);
                if (first && first->has_value() && (*first)->height > 0) {
                    height = (*first)->height;
                }
            }
            c.spec.chain += "," + core::chroma_source_chain(bg["chroma"], height.value_or(512));
            // El contorno del sujeto (outline_ffmpeg_steps), detrás de él y antes del encuadre.
            if (const std::string outline = core::outline_source_chain(bg["outline"], height.value_or(0));
                !outline.empty()) {
                c.spec.chain += "," + outline;
            }
        }
    }
    c.spec.chain += std::format(",fps={}", fps); // el fps=F que va tras el cropscale (un GIF a 10 fps → F)
    return c;
}

// El tramo de fx_bake.prepare_input: la misma cadena hasta el croma y, en vez del fps=F y la velocidad
// del overlay, `{spd},fps=F` (la velocidad ANTES del fps: el fotograma n es el instante local n/F).
// Sale un fotograma por tick desde 0, así que el n-ésimo tiene pts n.
Result<ClipChain> bake_input_chain(const core::TimelineClip& clip, const std::filesystem::path& path, int fps,
                                   bool preroll, const core::MatteSpec* matte) {
    core::TimelineClip at_zero = clip;
    at_zero.start = 0.0;
    auto chain = clip_chain(at_zero, path, fps, preroll, matte);
    if (!chain) {
        return chain;
    }
    ClipChain c = std::move(*chain);
    const std::string tail = std::format(",fps={}", fps);
    if (c.spec.chain.ends_with(tail)) {
        c.spec.chain.resize(c.spec.chain.size() - tail.size());
    }
    if (c.reverse) {
        c.spec.chain += ",reverse";
    }
    if (c.speed) {
        c.spec.chain += std::format(",setpts=PTS/{:.6f}", *c.speed);
    }
    c.spec.chain += tail;
    c.reverse = false;
    c.speed.reset();
    c.offset = 0.0;
    return c;
}

} // namespace

Result<std::optional<RgbaFrame>> bake_input_frame_at(const core::TimelineClip& clip, const std::filesystem::path& path,
                                                     int fps, std::int64_t n, const core::MatteSpec* matte) {
    auto chain = bake_input_chain(clip, path, fps, false, matte);
    if (!chain) {
        return std::unexpected(chain.error());
    }
    std::optional<RgbaFrame> out;
    std::int64_t m = 0;
    auto count = decode_frames(chain->spec, [&](const RgbaFrame& f) {
        out = f; // pasado el final, se queda el último
        return m++ < n;
    });
    if (!count) {
        return std::unexpected(count.error());
    }
    return out;
}

Result<std::optional<RgbaFrame>> export_frame_at(const core::TimelineClip& clip, const std::filesystem::path& path,
                                                 int fps, std::int64_t tick, const core::MatteSpec* matte) {
    auto chain = clip_chain(clip, path, fps, false, matte);
    if (!chain) {
        return std::unexpected(chain.error());
    }
    const ClipChain& c = *chain;
    // El overlay, en el tick `tick` del fondo, toma el primer fotograma con ese pts o, si no hay,
    // el último anterior; pasado el final, el último (`eof_action=repeat`).
    std::optional<RgbaFrame> out;
    if (!c.reverse) {
        std::int64_t m = 0;
        auto count = decode_frames(c.spec, [&](const RgbaFrame& f) {
            const std::int64_t p = c.pts_of(m++);
            if (p > tick) {
                if (!out) {
                    out = f; // empieza después del tick: no se vería (el clip aún no está)
                }
                return false;
            }
            out = f;
            return p < tick;
        });
        if (!count) {
            return std::unexpected(count.error());
        }
        return out;
    }
    // Al revés, el filtro reverse suelta los fotogramas del último al primero con los pts de
    // siempre: el m-ésimo que sale es el (N−1−m)-ésimo del tramo. Dos pasadas (contar y tomar
    // uno) en vez de guardarlos todos, que en 4K no caben en memoria.
    auto total = decode_frames(c.spec, [](const RgbaFrame&) { return true; });
    if (!total) {
        return std::unexpected(total.error());
    }
    const auto n = static_cast<std::int64_t>(*total);
    if (n == 0) {
        return out;
    }
    std::int64_t chosen = n - 1;
    for (std::int64_t m = 0; m < n; ++m) {
        const std::int64_t p = c.pts_of(m);
        if (p >= tick) {
            chosen = p == tick || m == 0 ? m : m - 1;
            break;
        }
    }
    const std::int64_t wanted = n - 1 - chosen;
    std::int64_t i = 0;
    auto again = decode_frames(c.spec, [&](const RgbaFrame& f) {
        if (i++ == wanted) {
            out = f;
            return false;
        }
        return true;
    });
    if (!again) {
        return std::unexpected(again.error());
    }
    return out;
}

// --- ClipFrames ------------------------------------------------------------------------------

namespace {

// Un Decoder que va por delante en su propio hilo, con unos pocos fotogramas en cola: así el decode
// de cada clip corre a la vez que lo que hace quien los pide (y que los demás clips).
class Prefetch {
public:
    explicit Prefetch(DecodeSpec spec) : decoder_(std::move(spec)) {}
    Prefetch(const Prefetch&) = delete;
    Prefetch& operator=(const Prefetch&) = delete;
    Prefetch(Prefetch&&) = delete;
    Prefetch& operator=(Prefetch&&) = delete;
    ~Prefetch() {
        {
            const std::scoped_lock lock(mutex_);
            stop_ = true;
        }
        wake_.notify_all();
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    // Abre el material aquí (los errores de apertura, al momento) y arranca el hilo.
    Result<void> start() {
        if (auto r = decoder_.start(); !r) {
            return r;
        }
        thread_ = std::thread([this] { run(); });
        return {};
    }

    // Como Decoder::next: el siguiente fotograma de la cadena (espera a que esté) o nada al final.
    // Un error llega después de los fotogramas que salieron antes que él.
    Result<std::optional<RgbaFrame>> next() {
        std::unique_lock lock(mutex_);
        wake_.wait(lock, [&] { return !queue_.empty() || done_; });
        if (!queue_.empty()) {
            std::optional<RgbaFrame> f(std::move(queue_.front()));
            queue_.pop_front();
            wake_.notify_all();
            return f;
        }
        if (error_) {
            return std::unexpected(*error_);
        }
        return std::optional<RgbaFrame>{};
    }

private:
    static constexpr std::size_t kAhead = 3;

    void run() {
        while (true) {
            {
                std::unique_lock lock(mutex_);
                wake_.wait(lock, [&] { return stop_ || queue_.size() < kAhead; });
                if (stop_) {
                    return;
                }
            }
            Result<std::optional<RgbaFrame>> f = std::optional<RgbaFrame>{};
            try {
                f = decoder_.next();
            } catch (const std::exception& e) { // memoria: un error, no un cierre del proceso
                f = fail(ErrorCode::Internal, std::format("error inesperado al decodificar: {}", e.what()));
            }
            const std::scoped_lock lock(mutex_);
            if (!f || !*f) {
                if (!f) {
                    error_ = f.error();
                }
                done_ = true;
                wake_.notify_all();
                return;
            }
            queue_.push_back(std::move(**f));
            wake_.notify_all();
        }
    }

    Decoder decoder_; // tras start(), solo lo usa el hilo
    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<RgbaFrame> queue_;
    std::optional<Error> error_;
    bool done_ = false;
    bool stop_ = false;
};

} // namespace

struct ClipFrames::Impl {
    core::TimelineClip clip;
    std::filesystem::path path;
    int fps = 30;
    std::optional<core::MatteSpec> matte;
    ClipChain chain;
    std::unique_ptr<Prefetch> decoder;
    std::int64_t m = 0;                 // fotogramas sacados de la cadena
    std::int64_t last = -1;             // el último tick pedido
    std::optional<RgbaFrame> current;   // el último con pts < tick
    std::optional<RgbaFrame> lookahead; // el siguiente
    std::int64_t lookahead_pts = 0;
    bool ended = false;
    // Al revés: cuántos fotogramas da el tramo (se cuenta una vez) y una ventana de fotogramas
    // seguidos del tramo, en su orden, desde el índice `window_from`.
    std::optional<std::int64_t> reverse_total;
    std::size_t reverse_frame_bytes = 0;
    std::int64_t window_from = 0;
    std::vector<RgbaFrame> window;
    static constexpr std::size_t kReverseWindowBytes = std::size_t{256} * 1024 * 1024;
    std::size_t reverse_block = 0; // fotogramas por bloque fijados (set_reverse_block); 0 = por bytes

    // El fotograma del tick al revés: el m-ésimo que suelta el filtro reverse es el (N−1−m)-ésimo
    // del tramo, como en export_frame_at. Al reproducir, los índices del tramo bajan: cada vez se
    // decodifica un bloque que TERMINA en el pedido (tantos como quepan en kReverseWindowBytes), así
    // que los siguientes ticks salen de él sin decodificar.
    Result<const RgbaFrame*> reverse_at(std::int64_t tick) {
        if (!reverse_total) {
            std::size_t bytes = 0;
            auto total = decode_frames(chain.spec, [&](const RgbaFrame& f) {
                bytes = std::max(bytes, f.pixels.size());
                return true;
            });
            if (!total) {
                return std::unexpected(total.error());
            }
            reverse_total = static_cast<std::int64_t>(*total);
            reverse_frame_bytes = std::max<std::size_t>(bytes, 1);
        }
        const std::int64_t n = *reverse_total;
        if (n == 0) {
            return nullptr;
        }
        // El primero con pts ≥ tick, como el bucle de export_frame_at, pero por bisección: pts_of no
        // decrece, y así reproducir un clip largo al revés no recorre el tramo en cada tick.
        const auto ks = std::views::iota(std::int64_t{0}, n);
        const auto it = std::ranges::partition_point(ks, [&](std::int64_t i) { return chain.pts_of(i) < tick; });
        const std::int64_t k = it == ks.end() ? n : *it;
        const std::int64_t chosen = k == n ? n - 1 : chain.pts_of(k) == tick || k == 0 ? k : k - 1;
        const std::int64_t wanted = n - 1 - chosen;
        if (wanted < window_from || wanted >= window_from + static_cast<std::int64_t>(window.size())) {
            const auto fit = static_cast<std::int64_t>(
                std::max<std::size_t>(1, reverse_block > 0 ? reverse_block : kReverseWindowBytes / reverse_frame_bytes));
            window.clear();
            window_from = std::max<std::int64_t>(0, wanted - fit + 1);
            std::int64_t i = 0;
            auto got = decode_frames(chain.spec, [&](const RgbaFrame& f) {
                if (i >= window_from) {
                    window.push_back(f);
                }
                return i++ < wanted;
            });
            if (!got) {
                return std::unexpected(got.error());
            }
            if (wanted >= window_from + static_cast<std::int64_t>(window.size())) {
                // El tramo dio menos fotogramas que al contarlos: no debería pasar.
                return fail(ErrorCode::Io, std::format("el clip {} no da el fotograma {} al revés", clip.id, wanted));
            }
        }
        return &window[static_cast<std::size_t>(wanted - window_from)];
    }

    Result<void> restart() {
        decoder.reset(); // el anterior termina su hilo antes de abrir otro
        decoder = std::make_unique<Prefetch>(chain.spec);
        m = 0;
        current.reset();
        lookahead.reset();
        ended = false;
        return decoder->start();
    }

    Result<void> advance() { // el siguiente fotograma de la cadena a `lookahead`
        auto f = decoder->next();
        if (!f) {
            return std::unexpected(f.error());
        }
        if (!*f) {
            ended = true;
            lookahead.reset();
            return {};
        }
        lookahead = std::move(**f);
        lookahead_pts = chain.pts_of(m++);
        return {};
    }
};

ClipFrames::ClipFrames(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
ClipFrames::ClipFrames(ClipFrames&&) noexcept = default;
ClipFrames& ClipFrames::operator=(ClipFrames&&) noexcept = default;
ClipFrames::~ClipFrames() = default;

Result<ClipFrames> ClipFrames::open(const core::TimelineClip& clip, const std::filesystem::path& path, int fps,
                                    const core::MatteSpec* matte, bool hwaccel) {
    auto chain = clip_chain(clip, path, fps, true, matte);
    if (!chain) {
        return std::unexpected(chain.error());
    }
    chain->spec.hwaccel = hwaccel;
    auto impl = std::make_unique<Impl>();
    impl->clip = clip;
    impl->path = path;
    impl->fps = fps;
    if (matte != nullptr) {
        impl->matte = *matte;
    }
    impl->chain = std::move(*chain);
    if (!impl->chain.reverse) {
        if (auto r = impl->restart(); !r) {
            return std::unexpected(r.error());
        }
    }
    return ClipFrames(std::move(impl));
}

Result<ClipFrames> ClipFrames::open_bake_input(const core::TimelineClip& clip, const std::filesystem::path& path,
                                               int fps, const core::MatteSpec* matte, bool hwaccel) {
    auto chain = bake_input_chain(clip, path, fps, true, matte);
    if (!chain) {
        return std::unexpected(chain.error());
    }
    chain->spec.hwaccel = hwaccel;
    auto impl = std::make_unique<Impl>();
    impl->clip = clip;
    impl->path = path;
    impl->fps = fps;
    if (matte != nullptr) {
        impl->matte = *matte;
    }
    impl->chain = std::move(*chain);
    if (auto r = impl->restart(); !r) {
        return std::unexpected(r.error());
    }
    return ClipFrames(std::move(impl));
}

void ClipFrames::set_reverse_block(std::size_t frames) {
    impl_->reverse_block = std::max<std::size_t>(frames, 1);
    impl_->window.clear(); // el bloque que hubiera se vuelve a decodificar con el tamaño nuevo
    impl_->window_from = 0;
}

Result<const RgbaFrame*> ClipFrames::at(std::int64_t tick) {
    Impl& s = *impl_;
    if (s.chain.reverse) {
        return s.reverse_at(tick);
    }
    if (tick < s.last) { // hacia atrás: se empieza de nuevo
        if (auto r = s.restart(); !r) {
            return std::unexpected(r.error());
        }
    }
    s.last = tick;
    if (!s.lookahead && !s.ended) {
        if (auto r = s.advance(); !r) {
            return std::unexpected(r.error());
        }
    }
    // Como export_frame_at: el primero con pts = tick o, si no hay, el último anterior; si el
    // primero ya va después, ese.
    while (s.lookahead && s.lookahead_pts < tick) {
        s.current = std::move(s.lookahead);
        s.lookahead.reset();
        if (auto r = s.advance(); !r) {
            return std::unexpected(r.error());
        }
    }
    if (s.lookahead && s.lookahead_pts == tick) {
        return &*s.lookahead;
    }
    if (s.current) {
        return &*s.current;
    }
    return s.lookahead ? &*s.lookahead : nullptr;
}

std::string md5_hex(std::span<const std::uint8_t> data) {
    std::array<std::uint8_t, 16> sum{};
    av_md5_sum(sum.data(), data.data(), data.size());
    std::string out;
    out.reserve(32);
    for (const auto b : sum) {
        out += std::format("{:02x}", b);
    }
    return out;
}

} // namespace gc::media
