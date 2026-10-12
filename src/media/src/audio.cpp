#include "gc/media/audio.hpp"

#include <array>
#include <charconv>
#include <climits>
#include <cmath>
#include <format>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>

#include "input_args.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libavutil/mathematics.h>
#include <libavutil/opt.h>
}

namespace gc::media {

namespace {

using detail::av_err;
using detail::InputOptions;

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

using FramePtr = std::unique_ptr<AVFrame, FrameFree>;

// Una entrada «[k:a]» del grafo: el mejor stream de audio de un archivo, decodificado como el CLI.
class AudioSource {
public:
    explicit AudioSource(InputOptions opts) : opts_(std::move(opts)) {}

    Result<void> open() {
        AVDictionary* dict = nullptr;
        av_dict_set(&dict, "scan_all_pmts", "1", AV_DICT_DONT_OVERWRITE);
        for (const auto& [k, v] : opts_.demuxer) {
            av_dict_set(&dict, k.c_str(), v.c_str(), 0);
        }
        AVFormatContext* raw = nullptr;
        const int rc = avformat_open_input(&raw, opts_.path.c_str(), nullptr, &dict);
        av_dict_free(&dict);
        if (rc < 0) {
            return fail(ErrorCode::Io, "no se pudo abrir el material: " + av_err(rc), opts_.path);
        }
        fmt_.reset(raw);
        if (const int r = avformat_find_stream_info(fmt_.get(), nullptr); r < 0) {
            return fail(ErrorCode::Io, "no se pudo leer el material: " + av_err(r), opts_.path);
        }
        const AVCodec* codec = nullptr;
        stream_ = av_find_best_stream(fmt_.get(), AVMEDIA_TYPE_AUDIO, -1, -1, &codec, 0);
        if (stream_ < 0 || codec == nullptr) {
            return fail(ErrorCode::NotFound, "el material no tiene audio", opts_.path);
        }
        const AVStream* st = fmt_->streams[stream_];
        // Como en el vídeo: el CLI desplaza los paquetes con −start_time del contenedor.
        const std::int64_t start = fmt_->start_time != AV_NOPTS_VALUE ? fmt_->start_time : 0;
        ts_offset_ = av_rescale_q(-start, AV_TIME_BASE_Q, st->time_base);
        dec_.reset(avcodec_alloc_context3(codec));
        if (!dec_ || avcodec_parameters_to_context(dec_.get(), st->codecpar) < 0) {
            return fail(ErrorCode::Internal, "no se pudo preparar el decodificador de audio");
        }
        dec_->pkt_timebase = st->time_base;
        if (const int r = avcodec_open2(dec_.get(), codec, nullptr); r < 0) {
            return fail(ErrorCode::Io, "no se pudo abrir el decodificador de audio: " + av_err(r), opts_.path);
        }
        pkt_.reset(av_packet_alloc());
        return {};
    }

    // El siguiente fotograma decodificado, con las marcas como el CLI; nullptr al final.
    Result<FramePtr> next() {
        FramePtr frame(av_frame_alloc());
        while (true) {
            const int r = avcodec_receive_frame(dec_.get(), frame.get());
            if (r == 0) {
                process_ts(*frame);
                return frame;
            }
            if (r == AVERROR_EOF) {
                return FramePtr{};
            }
            if (r != AVERROR(EAGAIN)) {
                ++errors_; // como el CLI: un fallo de decodificación se salta
                continue;
            }
            if (flushed_) {
                return FramePtr{};
            }
            const int rr = av_read_frame(fmt_.get(), pkt_.get());
            if (rr < 0) {
                flushed_ = true;
                static_cast<void>(avcodec_send_packet(dec_.get(), nullptr));
                continue;
            }
            if (pkt_->stream_index == stream_) {
                if (pkt_->pts != AV_NOPTS_VALUE) {
                    pkt_->pts += ts_offset_;
                }
                if (pkt_->dts != AV_NOPTS_VALUE) {
                    pkt_->dts += ts_offset_;
                }
                if (avcodec_send_packet(dec_.get(), pkt_.get()) < 0) {
                    ++errors_;
                }
            }
            av_packet_unref(pkt_.get());
        }
    }

    [[nodiscard]] const std::string& path() const { return opts_.path; }

private:
    // audio_ts_process y audio_samplerate_update de ffmpeg_dec.c: la base de tiempos interna es
    // 1/frecuencia (o la del stream si es más fina y múltiplo), las marcas que faltan se predicen y
    // se pasan a 1/frecuencia con av_rescale_delta, que no acumula redondeos entre fotogramas.
    void process_ts(AVFrame& frame) {
        frame.pts = frame.best_effort_timestamp;
        frame.time_base = fmt_->streams[stream_]->time_base;
        const AVRational tb_filter{1, frame.sample_rate};
        if (frame.sample_rate != last_rate_) {
            const int prev = last_tb_.den;
            const std::int64_t gcd = av_gcd(prev, frame.sample_rate);
            AVRational tb_new{1, frame.sample_rate};
            if (prev / gcd < INT_MAX / frame.sample_rate) {
                tb_new = {1, static_cast<int>(prev / gcd * frame.sample_rate)};
            }
            if (frame.time_base.num == 1 && frame.time_base.den > tb_new.den && frame.time_base.den % tb_new.den == 0) {
                tb_new = frame.time_base;
            }
            if (last_pts_ != AV_NOPTS_VALUE) {
                last_pts_ = av_rescale_q(last_pts_, last_tb_, tb_new);
            }
            last_dur_ = av_rescale_q(last_dur_, last_tb_, tb_new);
            last_tb_ = tb_new;
            last_rate_ = frame.sample_rate;
        }
        const AVRational tb = last_tb_;
        const std::int64_t pred = last_pts_ == AV_NOPTS_VALUE ? 0 : last_pts_ + last_dur_;
        if (frame.pts == AV_NOPTS_VALUE) {
            frame.pts = pred;
            frame.time_base = tb;
        } else if (last_pts_ != AV_NOPTS_VALUE &&
                   frame.pts > av_rescale_q_rnd(pred, tb, frame.time_base, AV_ROUND_UP)) {
            rescale_delta_ = AV_NOPTS_VALUE; // un hueco en las marcas: se empieza de nuevo
        }
        last_pts_ = av_rescale_q(frame.pts, frame.time_base, tb);
        last_dur_ = av_rescale_q(frame.nb_samples, tb_filter, tb);
        frame.pts = av_rescale_delta(frame.time_base, frame.pts, tb_filter, frame.nb_samples, &rescale_delta_, tb_filter);
        frame.time_base = tb_filter;
        frame.duration = frame.nb_samples;
    }

    InputOptions opts_;
    std::unique_ptr<AVFormatContext, FormatCloser> fmt_;
    std::unique_ptr<AVCodecContext, CodecFree> dec_;
    std::unique_ptr<AVPacket, PacketFree> pkt_;
    int stream_ = -1;
    std::int64_t ts_offset_ = 0;
    bool flushed_ = false;
    std::size_t errors_ = 0;
    // Estado de las marcas (ffmpeg_dec.c).
    AVRational last_tb_{1, 1};
    int last_rate_ = 0;
    std::int64_t last_pts_ = AV_NOPTS_VALUE;
    std::int64_t last_dur_ = 0;
    std::int64_t rescale_delta_ = AV_NOPTS_VALUE;
};

// El índice k de un nombre de entrada «k:a».
std::optional<std::size_t> input_index(std::string_view name) {
    if (!name.ends_with(":a")) {
        return std::nullopt;
    }
    std::size_t k = 0;
    const auto [end, ec] = std::from_chars(name.data(), name.data() + name.size() - 2, k);
    if (ec != std::errc{} || end != name.data() + name.size() - 2) {
        return std::nullopt;
    }
    return k;
}

struct Feed {
    std::unique_ptr<AudioSource> source;
    AVFilterContext* src = nullptr;
    FramePtr pending; // el primero, que se decodificó para saber los parámetros
    bool done = false;
};

} // namespace

Result<std::int64_t> mix_audio(const std::vector<std::vector<std::string>>& inputs,
                               const std::vector<std::string>& graph, double total, const AudioSink& sink) {
    if (graph.empty()) {
        return fail(ErrorCode::InvalidArgument, "el grafo de audio está vacío");
    }
    std::unique_ptr<AVFilterGraph, GraphFree> g(avfilter_graph_alloc());
    if (!g) {
        return fail(ErrorCode::Internal, "sin memoria para el grafo de audio");
    }
    // Lo que el CLI pone a la salida para el codificador AAC (`-ar 48000`, estéreo, float planar)
    // y el `-t` de la salida (un atrim con la duración en µs: el CLI la pone con av_opt_set_int;
    // escrita como texto hace falta el sufijo «us», porque un número suelto son segundos).
    std::string desc;
    for (const auto& step : graph) {
        desc += step + ";";
    }
    desc += std::format("[aout]aformat=sample_fmts=fltp:sample_rates={}:channel_layouts=stereo,atrim=duration={}us",
                        kExportSampleRate, std::llround(total * 1e6));
    AVFilterInOut* open_in = nullptr;
    AVFilterInOut* open_out = nullptr;
    if (const int r = avfilter_graph_parse2(g.get(), desc.c_str(), &open_in, &open_out); r < 0) {
        avfilter_inout_free(&open_in);
        avfilter_inout_free(&open_out);
        return fail(ErrorCode::InvalidArgument, "grafo de audio no válido: " + av_err(r));
    }
    std::unique_ptr<AVFilterInOut, InOutFree> ins(open_in);
    std::unique_ptr<AVFilterInOut, InOutFree> outs(open_out);
    if (outs == nullptr || outs->next != nullptr) {
        return fail(ErrorCode::InvalidArgument, "el grafo de audio debe tener una sola salida");
    }

    // Una fuente por cada entrada abierta del grafo, con los parámetros de su primer fotograma.
    std::vector<Feed> feeds;
    for (AVFilterInOut* io = ins.get(); io != nullptr; io = io->next) {
        const auto k = input_index(io->name != nullptr ? io->name : "");
        if (!k || *k >= inputs.size()) {
            return fail(ErrorCode::InvalidArgument, std::format("entrada del grafo de audio desconocida: «{}»",
                                                                io->name != nullptr ? io->name : ""));
        }
        auto opts = detail::parse_input_args(inputs[*k]);
        if (!opts) {
            return std::unexpected(opts.error());
        }
        Feed f;
        f.source = std::make_unique<AudioSource>(std::move(*opts));
        if (auto r = f.source->open(); !r) {
            return std::unexpected(r.error());
        }
        auto first = f.source->next();
        if (!first) {
            return std::unexpected(first.error());
        }
        f.pending = std::move(*first);
        AVFilterContext* src = avfilter_graph_alloc_filter(g.get(), avfilter_get_by_name("abuffer"), nullptr);
        if (src == nullptr) {
            return fail(ErrorCode::Internal, "falta el filtro abuffer");
        }
        AVBufferSrcParameters* par = av_buffersrc_parameters_alloc();
        if (par == nullptr) {
            return fail(ErrorCode::Internal, "sin memoria para la entrada del grafo de audio");
        }
        const AVFrame* fr = f.pending.get();
        par->time_base = fr != nullptr ? AVRational{1, fr->sample_rate} : AVRational{1, kExportSampleRate};
        par->format = fr != nullptr ? fr->format : AV_SAMPLE_FMT_FLTP;
        par->sample_rate = fr != nullptr ? fr->sample_rate : kExportSampleRate;
        if (fr != nullptr) {
            static_cast<void>(av_channel_layout_copy(&par->ch_layout, &fr->ch_layout));
        } else {
            av_channel_layout_default(&par->ch_layout, kExportChannels);
        }
        const int pr = av_buffersrc_parameters_set(src, par);
        av_channel_layout_uninit(&par->ch_layout);
        av_free(par);
        if (pr < 0 || avfilter_init_str(src, nullptr) < 0) {
            return fail(ErrorCode::Internal, "no se pudo preparar la entrada del grafo de audio", f.source->path());
        }
        if (const int r = avfilter_link(src, 0, io->filter_ctx, static_cast<unsigned>(io->pad_idx)); r < 0) {
            return fail(ErrorCode::Internal, "no se pudo conectar la entrada del grafo de audio: " + av_err(r));
        }
        f.src = src;
        f.done = fr == nullptr;
        feeds.push_back(std::move(f));
    }
    AVFilterContext* out = avfilter_graph_alloc_filter(g.get(), avfilter_get_by_name("abuffersink"), "out");
    if (out == nullptr || avfilter_init_str(out, nullptr) < 0) {
        return fail(ErrorCode::Internal, "no se pudo preparar la salida del grafo de audio");
    }
    if (const int r = avfilter_link(outs->filter_ctx, static_cast<unsigned>(outs->pad_idx), out, 0); r < 0) {
        return fail(ErrorCode::Internal, "no se pudo conectar la salida del grafo de audio: " + av_err(r));
    }
    if (const int r = avfilter_graph_config(g.get(), nullptr); r < 0) {
        return fail(ErrorCode::Internal, "no se pudo configurar el grafo de audio: " + av_err(r));
    }
    // Una entrada sin audio decodificable se cierra al momento.
    for (Feed& f : feeds) {
        if (f.done) {
            static_cast<void>(av_buffersrc_add_frame_flags(f.src, nullptr, 0));
        }
    }

    std::int64_t samples = 0;
    bool stop = false;
    FramePtr got(av_frame_alloc());
    std::vector<float> block;
    const auto drain = [&]() -> Result<bool> { // true: el grafo terminó
        while (!stop) {
            const int r = av_buffersink_get_frame(out, got.get());
            if (r == AVERROR(EAGAIN)) {
                return false;
            }
            if (r == AVERROR_EOF) {
                return true;
            }
            if (r < 0) {
                return fail(ErrorCode::Internal, "error en el grafo de audio: " + av_err(r));
            }
            const int n = got->nb_samples;
            block.resize(static_cast<std::size_t>(n) * kExportChannels);
            const auto* left = reinterpret_cast<const float*>(got->extended_data[0]);
            const auto* right = reinterpret_cast<const float*>(got->extended_data[1]);
            for (int i = 0; i < n; ++i) {
                block[static_cast<std::size_t>(i) * 2] = left[i];
                block[(static_cast<std::size_t>(i) * 2) + 1] = right[i];
            }
            av_frame_unref(got.get());
            samples += n;
            if (!sink(block)) {
                stop = true;
            }
        }
        return true;
    };

    // Como el CLI, se alimenta primero la entrada que el grafo está esperando (la que más
    // peticiones fallidas acumula); si ninguna, por turnos.
    std::size_t turn = 0;
    while (!stop) {
        Feed* chosen = nullptr;
        unsigned most = 0;
        for (Feed& f : feeds) {
            if (!f.done && av_buffersrc_get_nb_failed_requests(f.src) >= most) {
                if (chosen == nullptr || av_buffersrc_get_nb_failed_requests(f.src) > most) {
                    most = av_buffersrc_get_nb_failed_requests(f.src);
                    chosen = &f;
                }
            }
        }
        if (chosen == nullptr) {
            break;
        }
        if (most == 0) { // nadie espera: por turnos
            for (std::size_t i = 0; i < feeds.size(); ++i) {
                Feed& f = feeds[(turn + i) % feeds.size()];
                if (!f.done) {
                    chosen = &f;
                    turn = (turn + i + 1) % feeds.size();
                    break;
                }
            }
        }
        FramePtr frame = chosen->pending ? std::move(chosen->pending) : FramePtr{};
        if (!frame) {
            auto next = chosen->source->next();
            if (!next) {
                return std::unexpected(next.error());
            }
            frame = std::move(*next);
        }
        const int a = av_buffersrc_add_frame_flags(chosen->src, frame.get(), AV_BUFFERSRC_FLAG_PUSH);
        if (frame == nullptr || a == AVERROR_EOF) {
            chosen->done = true; // fin del archivo, o el grafo ya no quiere más de esta entrada
            if (frame != nullptr) {
                static_cast<void>(av_buffersrc_add_frame_flags(chosen->src, nullptr, 0));
            }
        } else if (a < 0) {
            return fail(ErrorCode::Internal, "error en el grafo de audio: " + av_err(a), chosen->source->path());
        }
        auto finished = drain();
        if (!finished) {
            return std::unexpected(finished.error());
        }
        if (*finished) {
            return samples;
        }
    }
    for (Feed& f : feeds) {
        static_cast<void>(av_buffersrc_add_frame_flags(f.src, nullptr, 0));
    }
    if (auto finished = drain(); !finished) {
        return std::unexpected(finished.error());
    }
    return samples;
}

Result<std::vector<float>> mix_audio(const std::vector<std::vector<std::string>>& inputs,
                                     const std::vector<std::string>& graph, double total) {
    std::vector<float> all;
    auto n = mix_audio(inputs, graph, total, [&](std::span<const float> block) {
        all.insert(all.end(), block.begin(), block.end());
        return true;
    });
    if (!n) {
        return std::unexpected(n.error());
    }
    return all;
}

} // namespace gc::media
