#include "render_cmd.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <format>
#include <iostream>
#include <map>
#include <optional>
#include <string_view>
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
#include "project_media.hpp"

namespace genclip {

namespace {

namespace fs = std::filesystem;
using namespace gc::core;

int fail_with(std::string_view message) {
    std::cerr << "error: " << message << '\n';
    return 1;
}

bool has_media(const TimelineClip& clip) { return clip.kind == "video" || clip.kind == "image"; }

// La calidad de los ajustes del export (export_settings.encoder_quality).
std::pair<int, std::string> encoder_quality(const gc::Json& settings) {
    const auto exp = normalize_export_settings(settings.is_object() && settings.contains("export") ? settings["export"]
                                                                                                   : gc::Json());
    const std::string q = exp ? exp->value("quality", "standard") : "standard";
    if (q == "draft") {
        return {28, "veryfast"};
    }
    if (q == "high") {
        return {18, "medium"};
    }
    return {23, "fast"};
}

} // namespace

int cmd_render(std::span<const std::string> args) {
    std::vector<std::string> pos;
    std::optional<std::string> out;
    std::string encoder;
    bool skip = false;
    bool progress = false;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "-o" && i + 1 < args.size()) {
            out = args[++i];
        } else if (a == "--codificador" && i + 1 < args.size()) {
            encoder = args[++i];
        } else if (a == "--omitir") {
            skip = true;
        } else if (a == "--progress") {
            progress = true;
        } else {
            pos.push_back(a);
        }
    }
    if (pos.size() != 2 || !out) {
        std::cerr << "uso: genclip render <raíz de video-yt> <proyecto> -o <salida.mp4> [--progress] [--omitir] "
                     "[--codificador <nombre>]\n";
        return 2;
    }
    gc::media::silence_ffmpeg_log();
    auto loaded = load_project(path_from_utf8(pos[0]), pos[1]);
    if (!loaded) {
        return fail_with(loaded.error().to_string());
    }
    LoadedProject& lp = *loaded;
    const Timeline full = lp.timeline; // con todos los clips: la duración y el audio
    const int w = static_cast<int>(full.width - full.width % 2);
    const int h = static_cast<int>(full.height - full.height % 2);
    const int fps = full.fps > 0 ? static_cast<int>(full.fps) : 30;
    const double total = export_total_duration(full);
    if (total <= 0) {
        return fail_with("la timeline está vacía: añade al menos un clip");
    }
    // `-t total` a la salida con `-r fps`: los fotogramas con pts < total (en µs, como el CLI).
    const auto total_us = static_cast<std::int64_t>(std::llround(total * 1e6));
    const std::int64_t frames = ((total_us * fps) + 999'999) / 1'000'000;

    // El material de cada clip (como _clip_path + media_exists) y si tiene audio (_has_audio).
    std::map<std::string, fs::path> paths;
    std::map<std::string, bool> audio_of;
    for (const auto& c : full.clips) {
        const auto p = clip_path(lp, *c);
        if (p && fs::is_regular_file(*p)) {
            paths[c->id] = *p;
        }
    }
    const auto exists = [&](const TimelineClip& c) { return paths.contains(c.id); };
    const auto has_audio = [&](const TimelineClip& c) {
        auto [it, fresh] = audio_of.try_emplace(c.id, false);
        if (fresh && paths.contains(c.id)) {
            it->second = gc::media::probe_media(paths.at(c.id)).has_audio;
        }
        return it->second;
    };

    // El audio entero, como el export (el mismo PCM que el CLI con el grafo de build_command).
    std::vector<float> pcm;
    {
        auto plan = export_inputs(full, exists, has_audio);
        auto graph = export_audio_graph(full, exists, has_audio);
        if (!plan || !graph) {
            return fail_with((plan ? graph.error() : plan.error()).to_string());
        }
        if (!graph->empty()) {
            std::vector<std::vector<std::string>> inputs;
            for (const TimelineClip* c : plan->files) {
                inputs.push_back(ffmpeg_input_args(*c, path_to_utf8(paths.at(c->id)), fps));
            }
            auto mixed = gc::media::mix_audio(inputs, *graph, total);
            if (!mixed) {
                return fail_with(mixed.error().to_string());
            }
            pcm = std::move(*mixed);
        }
    }

    // Lo que se dibuja: sin los clips cuyo material no existe (como el export) ni los motion
    // graphics, que el render aún no sabe dibujar.
    Timeline timeline = full;
    bool motion = false;
    std::erase_if(timeline.clips, [&](const gc::Cow<TimelineClip>& c) {
        if (c->kind == "motion") {
            motion = motion || !c->disabled;
            return true;
        }
        if (has_media(*c) && !paths.contains(c->id)) {
            std::cerr << std::format("aviso: sin material para el clip {} ({}): no se dibuja\n", c->id, c->filename);
            return true;
        }
        return false;
    });
    if (motion && !skip) {
        return fail_with("el render aún no dibuja motion graphics");
    }

    const auto [crf, preset] = encoder_quality(lp.settings);
    gc::media::EncodeSettings settings;
    settings.width = w;
    settings.height = h;
    settings.fps = fps;
    settings.crf = crf;
    settings.preset = preset;
    settings.audio = !pcm.empty();
    settings.video_encoder = encoder;
    auto writer = gc::media::Mp4Writer::open(path_from_utf8(*out), settings);
    if (!writer) {
        return fail_with(writer.error().to_string());
    }

    // Un lector por clip, que avanza con los fotogramas (las pasadas fantasma usan el de su clip).
    std::map<std::string, gc::media::ClipFrames> readers;
    std::map<std::string, gc::render::Image> images;
    std::optional<gc::Error> media_error;
    std::int64_t tick = 0;
    const auto frame_of = [&](const TimelineClip& clip) -> const gc::render::Image* {
        const std::string id =
            clip.id.ends_with(kGhostSuffix) ? clip.id.substr(0, clip.id.size() - kGhostSuffix.size()) : clip.id;
        if (auto it = images.find(id); it != images.end()) {
            return &it->second;
        }
        const auto p = paths.find(id);
        if (p == paths.end()) {
            return nullptr;
        }
        auto r = readers.find(id);
        if (r == readers.end()) {
            auto opened = gc::media::ClipFrames::open(clip, p->second, fps);
            if (!opened) {
                media_error = opened.error();
                return nullptr;
            }
            r = readers.emplace(id, std::move(*opened)).first;
        }
        auto got = r->second.at(tick);
        if (!got || *got == nullptr) {
            media_error = got ? gc::Error{gc::ErrorCode::Io, std::format("el clip {} no da fotograma en el {}", id, tick)}
                              : got.error();
            return nullptr;
        }
        const auto& f = **got;
        return &images.emplace(id, gc::render::Image{f.width, f.height, f.pixels}).first->second;
    };

    const gc::render::TextFonts fonts(FontDirs{default_system_font_dir(), lp.root / "app" / "fonts"});
    std::map<std::string, bool> warned;
    const auto warn = [&](const std::string& message) {
        if (!warned[message]) {
            warned[message] = true;
            std::cerr << "aviso: " << message << ": se omite\n";
        }
    };
    const auto started = std::chrono::steady_clock::now();
    int last_pct = -1;
    for (tick = 0; tick < frames; ++tick) {
        images.clear();
        const double t = static_cast<double>(tick) / fps;
        auto layers = evaluate(timeline, t, FrameSpec{static_cast<double>(w), static_cast<double>(h), kRenderLayers},
                               [&](const TimelineClip& clip) -> std::optional<MediaSize> {
                                   const auto* img = frame_of(clip);
                                   if (img == nullptr) {
                                       return std::nullopt;
                                   }
                                   return MediaSize{static_cast<double>(img->width), static_cast<double>(img->height)};
                               });
        if (media_error) {
            return fail_with(media_error->to_string());
        }
        if (!layers) {
            return fail_with(layers.error().to_string());
        }
        std::erase_if(*layers, [](const FrameLayer& layer) { return layer.clip->kind == "text"; });
        if (skip) {
            std::erase_if(*layers, [&](const FrameLayer& layer) {
                const auto reason = gc::render::unsupported_reason(layer);
                if (reason) {
                    warn(std::format("{} (clip {})", *reason, layer.clip->id));
                }
                return reason.has_value();
            });
        }
        auto image = gc::render::compose_frame(*layers, w, h,
                                               [&](const FrameLayer& layer) -> gc::Result<const gc::render::Image*> {
                                                   const auto* img = frame_of(*layer.clip);
                                                   if (img == nullptr) {
                                                       return gc::fail(gc::ErrorCode::NotFound,
                                                                       std::format("sin material para el clip {}",
                                                                                   layer.clip->id));
                                                   }
                                                   return img;
                                               });
        if (!image) {
            return fail_with(image.error().to_string());
        }
        auto canvas = gc::render::Canvas::make(w, h);
        if (!canvas) {
            return fail_with(canvas.error().to_string());
        }
        if (auto ok = canvas->draw_image(*image, SourceCrop{0, 0, static_cast<double>(w), static_cast<double>(h)},
                                         Affine{});
            !ok) {
            return fail_with(ok.error().to_string());
        }
        std::vector<std::string> warnings;
        if (auto ok = gc::render::draw_timeline_texts(*canvas, timeline, tick, fonts, skip, &warnings); !ok) {
            return fail_with(ok.error().to_string());
        }
        for (const auto& message : warnings) {
            warn(message);
        }
        const auto frame = canvas->snapshot();
        if (auto ok = writer->video(frame.pixels); !ok) {
            return fail_with(ok.error().to_string());
        }
        // El audio que corresponde a este fotograma.
        if (!pcm.empty()) {
            const auto from = static_cast<std::size_t>((tick * 48000) / fps) * 2;
            const auto to = std::min(pcm.size(), static_cast<std::size_t>(((tick + 1) * 48000) / fps) * 2);
            if (from < to) {
                if (auto ok = writer->audio(std::span(pcm).subspan(from, to - from)); !ok) {
                    return fail_with(ok.error().to_string());
                }
            }
        }
        const int pct = static_cast<int>((tick + 1) * 100 / frames);
        if (progress && pct != last_pct) {
            last_pct = pct;
            std::cout << std::format("progreso: {}%\n", pct) << std::flush;
        }
    }
    // Lo que quede de audio (si dura más que el último fotograma).
    if (!pcm.empty()) {
        const auto from = static_cast<std::size_t>((frames * 48000) / fps) * 2;
        if (from < pcm.size()) {
            if (auto ok = writer->audio(std::span(pcm).subspan(from)); !ok) {
                return fail_with(ok.error().to_string());
            }
        }
    }
    if (auto ok = writer->finish(); !ok) {
        return fail_with(ok.error().to_string());
    }
    const double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
    std::cout << std::format("{} ({}x{}, {} fps, {:.3f} s, {} fotogramas, {}, {:.1f} s de render)\n", *out, w, h, fps,
                             total, frames, writer->video_encoder(), secs);
    return 0;
}

} // namespace genclip
