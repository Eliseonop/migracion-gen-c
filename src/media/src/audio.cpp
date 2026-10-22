#include "gc/media/audio.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <climits>
#include <cmath>
#include <format>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>

#include "gc/core/io.hpp"
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
        // Como en el vídeo: el CLI desplaza los paquetes con −start_time del contenedor y, con -ss,
        // salta a ese instante y lo resta también (lo de antes sale negativo y el atrim lo quita).
        const std::int64_t start = fmt_->start_time != AV_NOPTS_VALUE ? fmt_->start_time : 0;
        const std::int64_t seek = opts_.seek_us.value_or(0);
        if (opts_.seek_us) {
            const std::int64_t ts = seek + start;
            // Si falla, el CLI solo avisa y sigue desde el principio.
            static_cast<void>(avformat_seek_file(fmt_.get(), -1, INT64_MIN, ts, ts, 0));
        }
        ts_offset_ = av_rescale_q(-(seek + start), AV_TIME_BASE_Q, st->time_base);
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
    [[nodiscard]] const InputOptions& options() const { return opts_; }

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

// insert_trim de ffmpeg_filter.c: un atrim con la duración y el inicio en µs (puestos con
// av_opt_set_int, como el CLI), o nada si no hay ninguno de los dos.
Result<AVFilterContext*> make_trim(AVFilterGraph* g, std::optional<std::int64_t> duration_us,
                                   std::optional<std::int64_t> start_us) {
    if (!duration_us && !start_us) {
        return nullptr;
    }
    AVFilterContext* trim = avfilter_graph_alloc_filter(g, avfilter_get_by_name("atrim"), nullptr);
    if (trim == nullptr) {
        return fail(ErrorCode::Internal, "falta el filtro atrim");
    }
    int r = 0;
    if (duration_us) {
        r = av_opt_set_int(trim, "durationi", *duration_us, AV_OPT_SEARCH_CHILDREN);
    }
    if (r >= 0 && start_us) {
        r = av_opt_set_int(trim, "starti", *start_us, AV_OPT_SEARCH_CHILDREN);
    }
    if (r < 0 || avfilter_init_str(trim, nullptr) < 0) {
        return fail(ErrorCode::Internal, "no se pudo preparar el recorte del audio");
    }
    return trim;
}

// Lo que el CLI pone a la salida: un aformat con lo que pide el codificador y el `-t` de la
// salida, que es un atrim y además el corte del codificador (check_recording_time de
// ffmpeg_enc.c: un bloque que empieza en o después de `-t` ya no sale).
struct OutputSpec {
    std::string format;                      // los argumentos del aformat
    std::optional<std::int64_t> duration_us; // -t
};

// Ejecuta los pasos de `graph` (el último termina en «[aout]») con la salida `spec` y pasa a
// `sink` bloques de `channels` canales entrelazados. Los filtros se crean en el orden del CLI
// (configure_filtergraph: el grafo, luego cada entrada con su recorte y al final la salida), que
// decide en qué enlace se inserta el aresample automático: delante o detrás de un recorte.
Result<std::int64_t> run_audio_graph(const std::vector<std::vector<std::string>>& inputs,
                                     const std::vector<std::string>& graph, const OutputSpec& spec, int channels,
                                     const AudioSink& sink) {
    if (graph.empty()) {
        return fail(ErrorCode::InvalidArgument, "el grafo de audio está vacío");
    }
    std::unique_ptr<AVFilterGraph, GraphFree> g(avfilter_graph_alloc());
    if (!g) {
        return fail(ErrorCode::Internal, "sin memoria para el grafo de audio");
    }
    std::string desc;
    for (const auto& step : graph) {
        desc += (desc.empty() ? "" : ";") + step;
    }
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
        // Con -ss o -t antes de -i, el recorte de la entrada (con el seek exacto, desde 0).
        const InputOptions& io_opts = f.source->options();
        auto trim = make_trim(g.get(), io_opts.duration_us,
                              io_opts.seek_us ? std::optional<std::int64_t>(0) : std::nullopt);
        if (!trim) {
            return std::unexpected(trim.error());
        }
        AVFilterContext* head = src;
        if (*trim != nullptr) {
            if (const int r = avfilter_link(src, 0, *trim, 0); r < 0) {
                return fail(ErrorCode::Internal, "no se pudo conectar la entrada del grafo de audio: " + av_err(r));
            }
            head = *trim;
        }
        if (const int r = avfilter_link(head, 0, io->filter_ctx, static_cast<unsigned>(io->pad_idx)); r < 0) {
            return fail(ErrorCode::Internal, "no se pudo conectar la entrada del grafo de audio: " + av_err(r));
        }
        f.src = src;
        f.done = fr == nullptr;
        feeds.push_back(std::move(f));
    }
    // La salida, como configure_output_audio_filter: abuffersink, aformat y el recorte del -t.
    AVFilterContext* out = avfilter_graph_alloc_filter(g.get(), avfilter_get_by_name("abuffersink"), "out");
    if (out == nullptr || avfilter_init_str(out, nullptr) < 0) {
        return fail(ErrorCode::Internal, "no se pudo preparar la salida del grafo de audio");
    }
    AVFilterContext* format = nullptr;
    if (avfilter_graph_create_filter(&format, avfilter_get_by_name("aformat"), "format_out", spec.format.c_str(),
                                     nullptr, g.get()) < 0) {
        return fail(ErrorCode::Internal, "no se pudo preparar el formato de salida del audio");
    }
    auto out_trim = make_trim(g.get(), spec.duration_us, std::nullopt);
    if (!out_trim) {
        return std::unexpected(out_trim.error());
    }
    std::vector<AVFilterContext*> chain{format};
    if (*out_trim != nullptr) {
        chain.push_back(*out_trim);
    }
    chain.push_back(out);
    if (const int r = avfilter_link(outs->filter_ctx, static_cast<unsigned>(outs->pad_idx), chain[0], 0); r < 0) {
        return fail(ErrorCode::Internal, "no se pudo conectar la salida del grafo de audio: " + av_err(r));
    }
    for (std::size_t i = 0; i + 1 < chain.size(); ++i) {
        if (const int r = avfilter_link(chain[i], 0, chain[i + 1], 0); r < 0) {
            return fail(ErrorCode::Internal, "no se pudo conectar la salida del grafo de audio: " + av_err(r));
        }
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
            if (spec.duration_us && got->pts != AV_NOPTS_VALUE &&
                av_compare_ts(got->pts, av_buffersink_get_time_base(out), *spec.duration_us, AV_TIME_BASE_Q) >= 0) {
                av_frame_unref(got.get());
                return true; // el corte del codificador
            }
            const int n = got->nb_samples;
            const auto ch = static_cast<std::size_t>(channels);
            block.resize(static_cast<std::size_t>(n) * ch);
            if (av_sample_fmt_is_planar(static_cast<AVSampleFormat>(got->format)) != 0) {
                for (std::size_t c = 0; c < ch; ++c) {
                    const auto* plane = reinterpret_cast<const float*>(got->extended_data[c]);
                    for (std::size_t i = 0; i < static_cast<std::size_t>(n); ++i) {
                        block[(i * ch) + c] = plane[i];
                    }
                }
            } else {
                const auto* data = reinterpret_cast<const float*>(got->extended_data[0]);
                std::copy_n(data, block.size(), block.begin());
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

// Un `-t` en µs, como av_parse_time del CLI.
std::int64_t to_us(double seconds) { return std::llround(seconds * 1e6); }

Result<std::vector<float>> collect(const std::function<Result<std::int64_t>(const AudioSink&)>& run) {
    std::vector<float> all;
    auto n = run([&](std::span<const float> block) {
        all.insert(all.end(), block.begin(), block.end());
        return true;
    });
    if (!n) {
        return std::unexpected(n.error());
    }
    return all;
}

// El `{:.3f}` de Python con `max(0.0, x)` delante (un NaN se queda en 0).
std::string seconds_arg(double s) { return std::format("{:.3f}", s > 0.0 ? s : 0.0); }

} // namespace

Result<std::int64_t> mix_audio(const std::vector<std::vector<std::string>>& inputs,
                               const std::vector<std::string>& graph, double total, const AudioSink& sink) {
    // Lo que el CLI pone a la salida para el codificador AAC (`-ar 48000`, estéreo, float planar).
    const OutputSpec spec{
        std::format("sample_fmts=fltp:sample_rates={}:channel_layouts=stereo", kExportSampleRate), to_us(total)};
    return run_audio_graph(inputs, graph, spec, kExportChannels, sink);
}

Result<std::vector<float>> mix_audio(const std::vector<std::vector<std::string>>& inputs,
                                     const std::vector<std::string>& graph, double total) {
    return collect([&](const AudioSink& sink) { return mix_audio(inputs, graph, total, sink); });
}

Result<std::vector<float>> decode_mono(const std::vector<std::string>& input, int rate,
                                       std::optional<double> max_seconds) {
    if (rate <= 0) {
        return fail(ErrorCode::InvalidArgument, std::format("frecuencia de muestreo no válida: {}", rate));
    }
    // Sin -af, el grafo simple del CLI es «anull»; a la salida, lo que pide pcm_f32le con -ac y -ar.
    const OutputSpec spec{std::format("sample_fmts=flt:sample_rates={}:channel_layouts=mono", rate),
                          max_seconds ? std::optional(to_us(*max_seconds)) : std::nullopt};
    return collect(
        [&](const AudioSink& sink) { return run_audio_graph({input}, {"[0:a]anull[aout]"}, spec, 1, sink); });
}

Result<core::Beats> detect_beats(const std::filesystem::path& path) {
    auto mono = decode_mono({"-i", core::path_to_utf8(path)}, core::kBeatSampleRate, core::kBeatMaxSeconds);
    if (!mono) {
        return std::unexpected(mono.error());
    }
    return core::detect_beats(*mono);
}

Result<core::Envelope> sample_rms_envelope(const std::filesystem::path& path, double src_start, double duration,
                                           int rate) {
    const double dur = std::max(0.05, std::isnan(duration) ? 0.0 : duration);
    auto mono = decode_mono({"-ss", seconds_arg(src_start), "-t", std::format("{:.3f}", dur), "-i",
                             core::path_to_utf8(path)},
                            core::kRmsSampleRate);
    if (!mono) {
        return std::unexpected(mono.error());
    }
    return core::rms_envelope(*mono, duration, rate);
}

} // namespace gc::media
