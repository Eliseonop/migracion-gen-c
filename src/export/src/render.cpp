#include "gc/export/render.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <exception>
#include <format>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "gc/core/audio_export.hpp"
#include "gc/core/clip_adjust.hpp"
#include "gc/core/export_input.hpp"
#include "gc/core/font_metrics.hpp"
#include "gc/core/frame.hpp"
#include "gc/core/io.hpp"
#include "gc/core/settings.hpp"
#include "gc/core/timeline_rules.hpp"
#include "gc/media/audio.hpp"
#include "gc/media/decode.hpp"
#include "gc/media/encode.hpp"
#include "gc/media/probe.hpp"
#include "gc/render/canvas.hpp"
#include "gc/render/compose.hpp"
#include "gc/render/text.hpp"

namespace gc::exporter {

namespace {

namespace fs = std::filesystem;
using namespace gc::core;

bool has_media(const TimelineClip& clip) { return clip.kind == "video" || clip.kind == "image"; }

// La calidad de los ajustes del export (export_settings.encoder_quality).
std::pair<int, std::string> encoder_quality(const Json& settings) {
    const auto exp =
        normalize_export_settings(settings.is_object() && settings.contains("export") ? settings["export"] : Json());
    const std::string q = exp ? exp->value("quality", "standard") : "standard";
    if (q == "draft") {
        return {28, "veryfast"};
    }
    if (q == "high") {
        return {18, "medium"};
    }
    return {23, "fast"};
}

// Suma a `slot` lo que tarda su ámbito.
class Lap {
public:
    explicit Lap(double& slot) : slot_(slot) {}
    Lap(const Lap&) = delete;
    Lap& operator=(const Lap&) = delete;
    Lap(Lap&&) = delete;
    Lap& operator=(Lap&&) = delete;
    ~Lap() { slot_ += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0_).count(); }

private:
    double& slot_;
    std::chrono::steady_clock::time_point t0_ = std::chrono::steady_clock::now();
};

} // namespace

Result<RenderStats> render_project(const LoadedProject& project, const fs::path& out, const RenderOptions& options) {
    const auto started = std::chrono::steady_clock::now();
    std::set<std::string> warned;
    const auto warn = [&](const std::string& message) {
        if (warned.insert(message).second && options.on_warning) {
            options.on_warning(message);
        }
    };
    const bool skip = options.omit_unsupported;

    const Timeline& full = project.timeline; // con todos los clips: la duración y el audio
    RenderStats stats;
    stats.width = static_cast<int>(full.width - full.width % 2);
    stats.height = static_cast<int>(full.height - full.height % 2);
    stats.fps = full.fps > 0 ? static_cast<int>(full.fps) : 30;
    const int w = stats.width;
    const int h = stats.height;
    const int fps = stats.fps;
    stats.duration = export_total_duration(full);
    if (stats.duration <= 0) {
        return fail(ErrorCode::InvalidArgument, "la timeline está vacía: añade al menos un clip");
    }
    // `-t total` a la salida con `-r fps`: los fotogramas con pts < total (en µs, como el CLI).
    const auto total_us = static_cast<std::int64_t>(std::llround(stats.duration * 1e6));
    stats.frames = ((total_us * fps) + 999'999) / 1'000'000;
    const std::int64_t frames = stats.frames;

    // El material de cada clip (como _clip_path + media_exists) y si tiene audio (_has_audio).
    std::map<std::string, fs::path> paths;
    std::map<std::string, bool> audio_of;
    for (const auto& c : full.clips) {
        const auto p = clip_path(project, *c);
        if (p && fs::is_regular_file(*p)) {
            paths[c->id] = *p;
        }
    }
    const auto exists = [&](const TimelineClip& c) { return paths.contains(c.id); };
    const auto has_audio = [&](const TimelineClip& c) {
        auto [it, fresh] = audio_of.try_emplace(c.id, false);
        if (fresh && paths.contains(c.id)) {
            it->second = media::probe_media(paths.at(c.id)).has_audio;
        }
        return it->second;
    };

    RenderTimes& ph = stats.times;
    // El audio entero, como el export (el mismo PCM que el CLI con el grafo de build_command).
    std::vector<float> pcm;
    {
        const Lap lap(ph.audio);
        auto plan = export_inputs(full, exists, has_audio);
        auto graph = export_audio_graph(full, exists, has_audio);
        if (!plan || !graph) {
            return std::unexpected(plan ? graph.error() : plan.error());
        }
        if (!graph->empty()) {
            std::vector<std::vector<std::string>> inputs;
            for (const TimelineClip* c : plan->files) {
                inputs.push_back(ffmpeg_input_args(*c, path_to_utf8(paths.at(c->id)), fps));
            }
            auto mixed = media::mix_audio(inputs, *graph, stats.duration);
            if (!mixed) {
                return std::unexpected(mixed.error());
            }
            pcm = std::move(*mixed);
        }
    }

    // Lo que se dibuja: sin los clips cuyo material no existe (como el export) ni los motion
    // graphics, que el render aún no sabe dibujar.
    Timeline timeline = full;
    std::optional<std::string> motion;
    std::erase_if(timeline.clips, [&](const Cow<TimelineClip>& c) {
        if (c->kind == "motion") {
            if (!c->disabled && !motion) {
                motion = c->id;
            }
            return true;
        }
        if (has_media(*c) && !paths.contains(c->id)) {
            warn(std::format("sin material para el clip {} ({}): no se dibuja", c->id, c->filename));
            return true;
        }
        return false;
    });
    if (motion) {
        const std::string msg = std::format("el render aún no dibuja motion graphics (clip {})", *motion);
        if (!skip) {
            return fail(ErrorCode::Unsupported, msg);
        }
        warn(msg + ": se omite");
    }

    const auto [crf, preset] = encoder_quality(project.settings);
    media::EncodeSettings settings;
    settings.width = w;
    settings.height = h;
    settings.fps = fps;
    settings.crf = crf;
    settings.preset = preset;
    settings.audio = !pcm.empty();
    settings.video_encoder = options.video_encoder;
    auto writer = media::Mp4Writer::open(out, settings);
    if (!writer) {
        return std::unexpected(writer.error());
    }
    stats.video_encoder = writer->video_encoder();

    // En tubería: este hilo evalúa cada fotograma en orden y saca el material de cada clip (los
    // lectores avanzan con los ticks); `workers` hilos componen (cada uno con su lienzo) y otro
    // codifica en orden. Como mucho `in_flight` fotogramas a la vez entre las tres partes.
    const unsigned hw = std::max(1U, std::thread::hardware_concurrency());
    const int workers = options.threads > 0 ? options.threads : static_cast<int>(std::clamp(hw, 3U, 10U) - 2);
    const int in_flight = (2 * workers) + 2;

    struct Job {
        std::int64_t tick = 0;
        std::vector<FrameLayer> layers;
        std::map<std::string, render::Image> images; // el material de cada clip (id sin «__adj»)
    };
    std::mutex mutex;              // protege todo lo de abajo
    std::condition_variable wake;  // cualquier cambio de estado
    std::deque<Job> jobs;          // evaluados, por componer
    std::map<std::int64_t, std::vector<std::uint8_t>> done; // compuestos, por codificar
    std::int64_t queued = 0;       // fotogramas en vuelo (de evaluar a codificar)
    std::optional<Error> failure;  // el primer error de cualquier hilo: todos paran
    bool producing = true;
    RenderTimes worker_times;      // compose y texto de todos los hilos
    const auto fail_all = [&](Error e) {
        const std::scoped_lock lock(mutex);
        if (!failure) {
            failure = std::move(e);
        }
        wake.notify_all();
    };
    const auto warn_locked = [&](const std::string& message) {
        const std::scoped_lock lock(mutex);
        warn(message);
    };

    const render::TextFonts fonts(FontDirs{default_system_font_dir(), project.root / "app" / "fonts"});
    const auto compose_worker = [&] {
        auto canvas = render::Canvas::make(w, h);
        if (!canvas) {
            fail_all(canvas.error());
            return;
        }
        RenderTimes mine;
        while (true) {
            Job job;
            {
                std::unique_lock lock(mutex);
                wake.wait(lock, [&] { return failure || !jobs.empty() || !producing; });
                if (failure || (jobs.empty() && !producing)) {
                    break;
                }
                job = std::move(jobs.front());
                jobs.pop_front();
            }
            std::optional<Lap> lap(std::in_place, mine.compose);
            if (skip) {
                std::erase_if(job.layers, [&](const FrameLayer& layer) {
                    const auto reason = render::unsupported_reason(layer);
                    if (reason) {
                        warn_locked(std::format("{} (clip {}): se omite", *reason, layer.clip->id));
                    }
                    return reason.has_value();
                });
            }
            const auto source = [&](const FrameLayer& layer) -> Result<const render::Image*> {
                const std::string& id = layer.clip->id;
                const auto it =
                    job.images.find(id.ends_with(kGhostSuffix) ? id.substr(0, id.size() - kGhostSuffix.size()) : id);
                if (it == job.images.end()) {
                    return fail(ErrorCode::NotFound, std::format("sin material para el clip {}", id));
                }
                return &it->second;
            };
            if (auto ok = render::compose_into(*canvas, job.layers, source); !ok) {
                fail_all(ok.error());
                break;
            }
            lap.emplace(mine.text);
            std::vector<std::string> warnings;
            if (auto ok = render::draw_timeline_texts(*canvas, timeline, job.tick, fonts, skip, &warnings); !ok) {
                fail_all(ok.error());
                break;
            }
            for (const auto& message : warnings) {
                warn_locked(message + ": se omite");
            }
            // El fotograma es opaco: los píxeles del lienzo tal cual.
            const auto px = canvas->opaque_pixels();
            std::vector<std::uint8_t> rgba(px.begin(), px.end());
            lap.reset();
            const std::scoped_lock lock(mutex);
            done.emplace(job.tick, std::move(rgba));
            wake.notify_all();
        }
        const std::scoped_lock lock(mutex);
        worker_times.compose += mine.compose;
        worker_times.text += mine.text;
    };

    // El codificador: los fotogramas en orden, con el audio que les corresponde.
    const auto encode_worker = [&] {
        for (std::int64_t k = 0; k < frames; ++k) {
            std::vector<std::uint8_t> rgba;
            {
                std::unique_lock lock(mutex);
                wake.wait(lock, [&] { return failure || done.contains(k); });
                if (failure) {
                    return;
                }
                auto node = done.extract(k);
                rgba = std::move(node.mapped());
            }
            const Lap lap(ph.encode);
            if (auto ok = writer->video(rgba); !ok) {
                fail_all(ok.error());
                return;
            }
            if (!pcm.empty()) {
                const auto from = static_cast<std::size_t>((k * 48000) / fps) * 2;
                const auto to = std::min(pcm.size(), static_cast<std::size_t>(((k + 1) * 48000) / fps) * 2);
                if (from < to) {
                    if (auto ok = writer->audio(std::span(pcm).subspan(from, to - from)); !ok) {
                        fail_all(ok.error());
                        return;
                    }
                }
            }
            {
                const std::scoped_lock lock(mutex);
                --queued;
                wake.notify_all();
            }
            if (options.on_progress) {
                options.on_progress(k + 1, frames);
            }
        }
    };

    // Una excepción en un hilo (memoria) es un error del render, no un cierre del proceso.
    const auto guarded = [&](const auto& body) {
        return [&fail_all, &body] {
            try {
                body();
            } catch (const std::exception& e) {
                fail_all(Error{ErrorCode::Internal, std::format("error inesperado en el render: {}", e.what())});
            }
        };
    };
    std::vector<std::thread> threads;
    threads.reserve(static_cast<std::size_t>(workers) + 1);
    for (int i = 0; i < workers; ++i) {
        threads.emplace_back(guarded(compose_worker));
    }
    threads.emplace_back(guarded(encode_worker));

    // Un lector por clip, que avanza con los fotogramas (las pasadas fantasma usan el de su clip).
    // Hasta cuándo se ve cada material (el fin de su clip; las pasadas fantasma duran lo mismo):
    // pasado ese instante, su lector se cierra. Si se volviera a pedir, se abriría de nuevo.
    std::map<std::string, media::ClipFrames> readers;
    std::map<std::string, double> ends;
    for (const auto& c : timeline.clips) {
        const double end = std::max(0.0, c->start) + clip_timeline_duration(*c);
        if (auto [it, fresh] = ends.try_emplace(c->id, end); !fresh) {
            it->second = std::max(it->second, end);
        }
    }
    for (std::int64_t tick = 0; tick < frames; ++tick) {
        {
            std::unique_lock lock(mutex);
            wake.wait(lock, [&] { return failure || queued < in_flight; });
            if (failure) {
                break;
            }
            ++queued;
        }
        Job job;
        job.tick = tick;
        std::optional<Error> media_error;
        const auto frame_of = [&](const TimelineClip& clip) -> const render::Image* {
            const Lap lap(ph.decode);
            const std::string id =
                clip.id.ends_with(kGhostSuffix) ? clip.id.substr(0, clip.id.size() - kGhostSuffix.size()) : clip.id;
            if (auto it = job.images.find(id); it != job.images.end()) {
                return &it->second;
            }
            const auto p = paths.find(id);
            if (p == paths.end()) {
                return nullptr;
            }
            auto r = readers.find(id);
            if (r == readers.end()) {
                auto opened = media::ClipFrames::open(clip, p->second, fps);
                if (!opened) {
                    media_error = opened.error();
                    return nullptr;
                }
                r = readers.emplace(id, std::move(*opened)).first;
            }
            auto got = r->second.at(tick);
            if (!got || *got == nullptr) {
                media_error =
                    got ? Error{ErrorCode::Io, std::format("el clip {} no da fotograma en el {}", id, tick)} : got.error();
                return nullptr;
            }
            const auto& f = **got;
            return &job.images.emplace(id, render::Image{f.width, f.height, f.pixels}).first->second;
        };
        const double t = static_cast<double>(tick) / fps;
        std::optional<Lap> lap(std::in_place, ph.evaluate);
        auto layers = evaluate(timeline, t, FrameSpec{static_cast<double>(w), static_cast<double>(h), kRenderLayers},
                               [&](const TimelineClip& clip) -> std::optional<MediaSize> {
                                   const auto* img = frame_of(clip);
                                   if (img == nullptr) {
                                       return std::nullopt;
                                   }
                                   return MediaSize{static_cast<double>(img->width), static_cast<double>(img->height)};
                               });
        lap.reset();
        if (media_error || !layers) {
            fail_all(media_error ? *media_error : layers.error());
            break;
        }
        // Los textos no son capas del compuesto: van encima, como el .ass global del export.
        std::erase_if(*layers, [](const FrameLayer& layer) { return layer.clip->kind == "text"; });
        // Lo que se compone ya está en job.images (evaluate pidió las dimensiones de cada capa con
        // material); por si acaso, se pide aquí, donde están los lectores, y no al componer.
        for (const FrameLayer& layer : *layers) {
            if (!layer.blits.empty()) {
                static_cast<void>(frame_of(*layer.clip));
            }
        }
        if (media_error) {
            fail_all(*media_error);
            break;
        }
        job.layers = std::move(*layers);
        {
            const std::scoped_lock lock(mutex);
            jobs.push_back(std::move(job));
            wake.notify_all();
        }
        // Fuera los lectores de los clips que ya no se verán (cada uno guarda un decodificador).
        const double next_t = static_cast<double>(tick + 1) / fps;
        std::erase_if(readers, [&](const auto& reader) {
            const auto e = ends.find(reader.first);
            return e != ends.end() && e->second <= next_t;
        });
    }
    {
        const std::scoped_lock lock(mutex);
        producing = false;
        wake.notify_all();
    }
    for (auto& th : threads) {
        th.join();
    }
    ph.compose = worker_times.compose;
    ph.text = worker_times.text;
    if (failure) {
        return std::unexpected(*failure);
    }
    {
        const Lap lap(ph.encode);
        // Lo que quede de audio (si dura más que el último fotograma).
        if (!pcm.empty()) {
            const auto from = static_cast<std::size_t>((frames * 48000) / fps) * 2;
            if (from < pcm.size()) {
                if (auto ok = writer->audio(std::span(pcm).subspan(from)); !ok) {
                    return std::unexpected(ok.error());
                }
            }
        }
        if (auto ok = writer->finish(); !ok) {
            return std::unexpected(ok.error());
        }
    }
    stats.threads = workers;
    stats.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    return stats;
}

} // namespace gc::exporter
