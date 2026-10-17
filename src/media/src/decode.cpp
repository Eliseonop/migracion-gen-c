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
#include <string_view>
#include <thread>
#include <vector>

#include "gc/core/bg.hpp"
#include "gc/core/export_input.hpp"
#include "gc/core/io.hpp"
#include "gc/core/timeline_rules.hpp"
#include "gc/media/probe.hpp"
#include "input_args.hpp"

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

using detail::av_err;
using detail::InputOptions;
using detail::parse_input_args;

// `-t` de una entrada: el CLI le pone a su salida un `trim` con la duración en µs.
// En texto, una duración sin unidad son segundos: el valor va con «us».
std::string duration_trim(std::int64_t us) { return std::format("trim=duration={}us,", us); }

class Decoder {
public:
    explicit Decoder(DecodeSpec spec) : spec_(std::move(spec)) {}

    Result<void> start() {
        auto opts = parse_input_args(spec_.input_args);
        if (!opts) {
            return std::unexpected(opts.error());
        }
        opts_ = std::move(*opts);
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
                if (auto r = reopen_without_preroll(); !r) {
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

    Result<void> send(const AVPacket* pkt) {
        // Como el CLI, un paquete que no se puede decodificar se salta y se sigue con el resto.
        if (const int r = avcodec_send_packet(dec_.get(), pkt); r < 0 && r != AVERROR_EOF) {
            ++decode_errors_;
            return {};
        }
        while (true) {
            const int r = avcodec_receive_frame(dec_.get(), frame_.get());
            if (r == AVERROR(EAGAIN) || r == AVERROR_EOF) {
                return {};
            }
            if (r < 0) {
                ++decode_errors_;
                return {};
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

    Result<void> reopen_without_preroll() {
        overshoot_ = false;
        preroll_failed_ = true;
        graph_.reset();
        src_ = nullptr;
        sink_ = nullptr;
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
    int stream_ = -1;
    std::int64_t ts_offset_ = 0; // en la base de tiempos del stream
    std::size_t decode_errors_ = 0;
    std::size_t count_ = 0;
    std::optional<std::int64_t> preroll_pts_; // el instante del salto previo, en la base del stream
    bool overshoot_ = false;                  // el salto previo se pasó: hay que reabrir sin él
    bool preroll_failed_ = false;
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
                             bool preroll) {
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
    // «Eliminar fondo» por croma: en el espacio del material, antes del encuadre
    // (_bg_source_chain). El matte automático aún no.
    if (clip.bg_removal && core::bg_capable(clip)) {
        const Json bg = core::normalize_bg(*clip.bg_removal);
        if (core::bg_auto_active(bg)) {
            return fail(ErrorCode::Unsupported, std::format("el matte automático del clip {} aún no se aplica", clip.id));
        }
        if (core::bg_chroma_active(bg)) {
            // El desenfoque del borde va en px del alto del material (detect.dims; 512 si no se sabe).
            std::int64_t height = 512;
            if (!core::chroma_alpha_ffmpeg(bg["chroma"], 512).empty()) {
                auto first = frame_at(path, 0.0);
                if (first && first->has_value() && (*first)->height > 0) {
                    height = (*first)->height;
                }
            }
            c.spec.chain += "," + core::chroma_source_chain(bg["chroma"], height);
        }
    }
    c.spec.chain += std::format(",fps={}", fps); // el fps=F que va tras el cropscale (un GIF a 10 fps → F)
    return c;
}

} // namespace

Result<std::optional<RgbaFrame>> export_frame_at(const core::TimelineClip& clip, const std::filesystem::path& path,
                                                 int fps, std::int64_t tick) {
    auto chain = clip_chain(clip, path, fps, false);
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
    ClipChain chain;
    std::unique_ptr<Prefetch> decoder;
    std::int64_t m = 0;                 // fotogramas sacados de la cadena
    std::int64_t last = -1;             // el último tick pedido
    std::optional<RgbaFrame> current;   // el último con pts < tick
    std::optional<RgbaFrame> lookahead; // el siguiente
    std::int64_t lookahead_pts = 0;
    bool ended = false;
    std::optional<RgbaFrame> reversed; // al revés: el del último tick (export_frame_at)

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

Result<ClipFrames> ClipFrames::open(const core::TimelineClip& clip, const std::filesystem::path& path, int fps) {
    auto chain = clip_chain(clip, path, fps, true);
    if (!chain) {
        return std::unexpected(chain.error());
    }
    auto impl = std::make_unique<Impl>();
    impl->clip = clip;
    impl->path = path;
    impl->fps = fps;
    impl->chain = std::move(*chain);
    if (!impl->chain.reverse) {
        if (auto r = impl->restart(); !r) {
            return std::unexpected(r.error());
        }
    }
    return ClipFrames(std::move(impl));
}

Result<const RgbaFrame*> ClipFrames::at(std::int64_t tick) {
    Impl& s = *impl_;
    if (s.chain.reverse) {
        // Al revés hace falta el final del tramo: de momento, como export_frame_at.
        auto f = export_frame_at(s.clip, s.path, s.fps, tick);
        if (!f) {
            return std::unexpected(f.error());
        }
        s.reversed = std::move(*f);
        return s.reversed ? &*s.reversed : nullptr;
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
