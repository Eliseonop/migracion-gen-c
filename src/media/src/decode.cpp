#include "gc/media/decode.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstring>
#include <format>
#include <memory>
#include <optional>
#include <string_view>

#include "gc/core/export_input.hpp"
#include "gc/core/io.hpp"
#include "gc/media/probe.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavformat/avformat.h>
#include <libavutil/error.h>
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

std::string av_err(int code) {
    std::array<char, AV_ERROR_MAX_STRING_SIZE> buf{};
    av_strerror(code, buf.data(), buf.size());
    return buf.data();
}

// Los argumentos de entrada de ffmpeg, separados en lo que entiende cada pieza.
struct InputOptions {
    std::string path;
    std::string decoder;                   // -c:v
    std::optional<std::int64_t> duration_us; // -t, en µs (como av_parse_time del CLI)
    std::optional<std::int64_t> seek_us;     // -ss antes de -i: seek exacto
    std::vector<std::pair<std::string, std::string>> demuxer; // -loop, -framerate…
};

Result<InputOptions> parse_input_args(const std::vector<std::string>& args) {
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
            if (ec != std::errc{} || end != v.data() + v.size() || !std::isfinite(s)) {
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

// `-t` de una entrada: el CLI le pone a su salida un `trim` con la duración en µs.
std::string duration_trim(std::int64_t us) { return std::format("trim=durationi={},", us); }

class Decoder {
public:
    Decoder(const DecodeSpec& spec, const std::function<bool(const RgbaFrame&)>& on_frame)
        : spec_(spec), on_frame_(on_frame) {}

    Result<std::size_t> run() {
        auto opts = parse_input_args(spec_.input_args);
        if (!opts) {
            return std::unexpected(opts.error());
        }
        opts_ = std::move(*opts);
        if (auto r = open(); !r) {
            return std::unexpected(r.error());
        }
        std::unique_ptr<AVPacket, PacketFree> pkt(av_packet_alloc());
        std::unique_ptr<AVFrame, FrameFree> frame(av_frame_alloc());
        while (!done_) {
            const int rr = av_read_frame(fmt_.get(), pkt.get());
            if (rr < 0) {
                break; // EOF o error de lectura: se vacía lo que quede
            }
            if (pkt->stream_index == stream_) {
                if (auto r = send(pkt.get(), frame.get()); !r) {
                    return std::unexpected(r.error());
                }
            }
            av_packet_unref(pkt.get());
        }
        if (!done_) {
            if (auto r = send(nullptr, frame.get()); !r) { // vacía el decodificador
                return std::unexpected(r.error());
            }
        }
        if (!done_ && src_ != nullptr) {
            // Fin del grafo. Si falla, lo dirá el drain() de abajo.
            static_cast<void>(av_buffersrc_add_frame_flags(src_, nullptr, 0));
            if (auto r = drain(); !r) {
                return std::unexpected(r.error());
            }
        }
        return count_;
    }

private:
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
        AVStream* st = fmt_->streams[stream_];
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
        dec_.reset(avcodec_alloc_context3(codec));
        if (!dec_ || avcodec_parameters_to_context(dec_.get(), st->codecpar) < 0) {
            return fail(ErrorCode::Internal, "no se pudo preparar el decodificador");
        }
        dec_->pkt_timebase = st->time_base;
        dec_->thread_count = 0; // automático, como el CLI
        if (const int r = avcodec_open2(dec_.get(), codec, nullptr); r < 0) {
            return fail(ErrorCode::Io, "no se pudo abrir el decodificador: " + av_err(r), opts_.path);
        }
        return {};
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
        // Lo que el CLI pone delante (seek exacto y -t), la cadena del export y el formato de salida.
        std::string desc = (opts_.seek_us ? std::string("trim=starti=0,") : std::string()) +
                           (opts_.duration_us ? duration_trim(*opts_.duration_us) : std::string()) + spec_.chain;
        if (!desc.empty() && desc.back() != ',') {
            desc += ',';
        }
        desc += "format=rgba";
        std::unique_ptr<AVFilterInOut, InOutFree> outputs(avfilter_inout_alloc());
        std::unique_ptr<AVFilterInOut, InOutFree> inputs(avfilter_inout_alloc());
        outputs->name = av_strdup("in");
        outputs->filter_ctx = src_;
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

    Result<void> send(const AVPacket* pkt, AVFrame* frame) {
        // Como el CLI, un paquete que no se puede decodificar se salta y se sigue con el resto.
        if (const int r = avcodec_send_packet(dec_.get(), pkt); r < 0 && r != AVERROR_EOF) {
            ++decode_errors_;
            return {};
        }
        while (!done_) {
            const int r = avcodec_receive_frame(dec_.get(), frame);
            if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) {
                return {};
            }
            if (r < 0) {
                ++decode_errors_;
                return {};
            }
            // Lo que hace el CLI: la marca «mejor estimada», ya desplazada con ts_offset.
            frame->pts = frame->best_effort_timestamp == AV_NOPTS_VALUE ? AV_NOPTS_VALUE
                                                                         : frame->best_effort_timestamp + ts_offset_;
            if (!graph_) {
                if (auto g = build_graph(*frame); !g) {
                    return g;
                }
            }
            if (const int a = av_buffersrc_add_frame_flags(src_, frame, 0); a < 0 && a != AVERROR_EOF) {
                return fail(ErrorCode::Internal, "error en el grafo de filtros: " + av_err(a));
            }
            av_frame_unref(frame);
            if (auto d = drain(); !d) {
                return d;
            }
        }
        return {};
    }

    Result<void> drain() {
        std::unique_ptr<AVFrame, FrameFree> out(av_frame_alloc());
        while (!done_) {
            const int r = av_buffersink_get_frame(sink_, out.get());
            if (r == AVERROR(EAGAIN)) {
                return {};
            }
            if (r == AVERROR_EOF) {
                done_ = true; // el trim ya terminó: no hace falta leer más
                return {};
            }
            if (r < 0) {
                return fail(ErrorCode::Internal, "error en el grafo de filtros: " + av_err(r));
            }
            RgbaFrame f;
            f.width = out->width;
            f.height = out->height;
            const auto row = static_cast<std::size_t>(out->width) * 4;
            f.pixels.resize(row * static_cast<std::size_t>(out->height));
            for (int y = 0; y < out->height; ++y) {
                std::memcpy(f.pixels.data() + row * static_cast<std::size_t>(y),
                            out->data[0] + static_cast<std::ptrdiff_t>(y) * out->linesize[0], row);
            }
            av_frame_unref(out.get());
            ++count_;
            if (!on_frame_(f)) {
                done_ = true;
            }
        }
        return {};
    }

    const DecodeSpec& spec_;
    const std::function<bool(const RgbaFrame&)>& on_frame_;
    InputOptions opts_;
    std::unique_ptr<AVFormatContext, FormatCloser> fmt_;
    std::unique_ptr<AVCodecContext, CodecFree> dec_;
    std::unique_ptr<AVFilterGraph, GraphFree> graph_;
    AVFilterContext* src_ = nullptr;
    AVFilterContext* sink_ = nullptr;
    int stream_ = -1;
    std::int64_t ts_offset_ = 0; // en la base de tiempos del stream
    std::size_t decode_errors_ = 0;
    std::size_t count_ = 0;
    bool done_ = false;
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
    return Decoder(spec, on_frame).run();
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
