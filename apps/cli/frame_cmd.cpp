#include "frame_cmd.hpp"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <format>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "gc/core/export_input.hpp"
#include "gc/core/io.hpp"
#include "gc/export/frame.hpp"
#include "gc/export/project.hpp"
#include "gc/media/decode.hpp"
#include "gc/media/probe.hpp"
#include "gc/render/png.hpp"

namespace genclip {

namespace {

int fail_with(std::string_view message) {
    std::cerr << "error: " << message << '\n';
    return 1;
}

} // namespace

int cmd_frame(std::span<const std::string> args) {
    std::vector<std::string> pos;
    std::optional<double> at;
    std::optional<std::string> out;
    bool skip = false;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "--at" && i + 1 < args.size()) {
            const std::string& v = args[++i];
            double t = 0.0;
            const auto [end, ec] = std::from_chars(v.data(), v.data() + v.size(), t);
            if (ec != std::errc{} || end != v.data() + v.size() || !std::isfinite(t)) {
                return fail_with(std::format("--at necesita un número de segundos, no «{}»", v));
            }
            if (std::abs(t) > 1e9) { // ~31 años: fuera de cualquier timeline (y del rango de un entero)
                return fail_with(std::format("--at {} está fuera de cualquier timeline", v));
            }
            at = t;
        } else if (a == "-o" && i + 1 < args.size()) {
            out = args[++i];
        } else if (a == "--omitir") {
            skip = true;
        } else {
            pos.push_back(a);
        }
    }
    if (pos.size() != 2 || !at || !out) {
        std::cerr << "uso: genclip frame <raíz de video-yt> <proyecto> --at <s> -o <salida.png> [--omitir]\n";
        return 2;
    }
    auto loaded = gc::exporter::load_project(gc::core::path_from_utf8(pos[0]), pos[1]);
    if (!loaded) {
        return fail_with(loaded.error().to_string());
    }
    // Como compose.py, antes de preparar nada: sin clips, «La timeline está vacía», y el fondo dura
    // `total`, así que un fotograma en round(total·F) o después no existe. (FrameRenderer lo
    // comprueba igual; aquí solo se adelanta.)
    const gc::core::Timeline& timeline = loaded->timeline;
    const int fps = timeline.fps > 0 ? static_cast<int>(timeline.fps) : 30;
    const double total = gc::core::export_total_duration(timeline);
    const std::int64_t tick = gc::media::export_frame_tick(*at, fps);
    if (total > 0 && static_cast<double>(tick) >= std::round(total * fps)) {
        return fail_with(std::format("el instante {:.3f} s queda fuera de la timeline ({:.3f} s)",
                                     static_cast<double>(tick) / fps, total));
    }
    gc::media::silence_ffmpeg_log();
    // Lo mismo que el export en ese fotograma (gc_export): el material que falta y lo que se omite,
    // como avisos por stderr.
    gc::exporter::FrameOptions options;
    options.omit_unsupported = skip;
    options.on_warning = [](const std::string& message) { std::cerr << "aviso: " << message << '\n'; };
    auto renderer = gc::exporter::FrameRenderer::open(*loaded, options);
    if (!renderer) {
        return fail_with(renderer.error().to_string());
    }
    auto frame = renderer->at(*at);
    if (!frame) {
        return fail_with(frame.error().to_string());
    }
    if (auto ok = gc::render::write_png(frame->image, gc::core::path_from_utf8(*out)); !ok) {
        return fail_with(ok.error().to_string());
    }
    std::cout << std::format("{} ({}x{}, t = {:.3f} s, {} capas)\n", *out, renderer->width(), renderer->height(),
                             frame->t, frame->layers);
    return 0;
}

} // namespace genclip
