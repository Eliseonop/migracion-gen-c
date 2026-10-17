#include "render_cmd.hpp"

#include <charconv>
#include <cstdint>
#include <format>
#include <iostream>
#include <optional>
#include <string_view>
#include <system_error>
#include <vector>

#include "gc/core/io.hpp"
#include "gc/export/project.hpp"
#include "gc/export/render.hpp"
#include "gc/media/probe.hpp"

namespace genclip {

namespace {

int fail_with(std::string_view message) {
    std::cerr << "error: " << message << '\n';
    return 1;
}

} // namespace

int cmd_render(std::span<const std::string> args) {
    std::vector<std::string> pos;
    std::optional<std::string> out;
    gc::exporter::RenderOptions options;
    bool progress = false;
    bool times = false;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "-o" && i + 1 < args.size()) {
            out = args[++i];
        } else if (a == "--codificador" && i + 1 < args.size()) {
            options.video_encoder = args[++i];
        } else if (a == "--hilos" && i + 1 < args.size()) {
            const std::string& v = args[++i];
            int n = 0;
            const auto [end, ec] = std::from_chars(v.data(), v.data() + v.size(), n);
            if (ec != std::errc{} || end != v.data() + v.size() || n < 1 || n > 64) {
                return fail_with(std::format("--hilos necesita un número de 1 a 64, no «{}»", v));
            }
            options.threads = n;
        } else if (a == "--omitir") {
            options.omit_unsupported = true;
        } else if (a == "--progress") {
            progress = true;
        } else if (a == "--tiempos") {
            times = true;
        } else {
            pos.push_back(a);
        }
    }
    if (pos.size() != 2 || !out) {
        std::cerr << "uso: genclip render <raíz de video-yt> <proyecto> -o <salida.mp4> [--progress] [--omitir] "
                     "[--codificador <nombre>] [--hilos <n>] [--tiempos]\n";
        return 2;
    }
    gc::media::silence_ffmpeg_log();
    auto loaded = gc::exporter::load_project(gc::core::path_from_utf8(pos[0]), pos[1]);
    if (!loaded) {
        return fail_with(loaded.error().to_string());
    }
    int last_pct = -1;
    if (progress) {
        options.on_progress = [&](std::int64_t done, std::int64_t total) {
            if (const auto pct = static_cast<int>(done * 100 / total); pct != last_pct) {
                last_pct = pct;
                std::cout << std::format("progreso: {}%\n", pct) << std::flush;
            }
        };
    }
    options.on_warning = [](const std::string& message) { std::cerr << "aviso: " << message << '\n'; };
    auto stats = gc::exporter::render_project(*loaded, gc::core::path_from_utf8(*out), options);
    if (!stats) {
        return fail_with(stats.error().to_string());
    }
    std::cout << std::format("{} ({}x{}, {} fps, {:.3f} s, {} fotogramas, {}, {} hilos, {:.1f} s de render)\n",
                             *out, stats->width, stats->height, stats->fps, stats->duration, stats->frames,
                             stats->video_encoder, stats->threads, stats->seconds);
    if (times) {
        const auto& t = stats->times;
        // Sumados entre hilos. El decode va dentro de evaluate.
        std::cout << std::format("tiempos: audio {:.2f} s · decode {:.2f} s · evaluate {:.2f} s · compose {:.2f} s · "
                                 "texto {:.2f} s · encode {:.2f} s\n",
                                 t.audio, t.decode, t.evaluate, t.compose, t.text, t.encode);
    }
    return 0;
}

} // namespace genclip
