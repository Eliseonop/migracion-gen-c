#pragma once

// El export de una timeline a MP4, como compose.render de video-yt: el audio entero con el grafo
// de build_command (gc::media::mix_audio), y por fotograma, evaluar (gc::core::evaluate),
// componer las capas y los textos (gc::render) y codificar (gc::media::Mp4Writer). Mismo tamaño
// (par), fps y duración que el export: N = ⌈total_µs · fps / 10⁶⌉ fotogramas, como su `-t`.
// En tubería: un hilo evalúa y decodifica en orden, varios componen y otro codifica en orden.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

#include "gc/core/error.hpp"
#include "gc/export/project.hpp"

namespace gc::exporter {

struct RenderOptions {
    // Lo que el render aún no sabe dibujar se salta con un aviso en vez de ser un error.
    bool omit_unsupported = false;
    std::string video_encoder; // "" = elegirlo como gpu.py (Mp4Writer)
    int threads = 0;           // hilos que componen; 0 = según los núcleos (de 1 a 8)
    // Tras cada fotograma: cuántos van y cuántos hay.
    std::function<void(std::int64_t done, std::int64_t total)> on_progress;
    // Cada aviso distinto, una vez (material que falta, capas que se omiten).
    std::function<void(const std::string& message)> on_warning;
};

// Segundos por fase, sumados entre hilos (componer va en varios a la vez, así que la suma puede
// pasar del total). El decode va dentro de evaluate (allí se piden los fotogramas) y la
// conversión a YUV, que hacen los hilos que componen, dentro de encode.
struct RenderTimes {
    double audio = 0;
    double decode = 0;
    double evaluate = 0;
    double compose = 0;
    double text = 0;
    double encode = 0;
};

struct RenderStats {
    int width = 0;
    int height = 0;
    int fps = 0;
    double duration = 0; // s, export_total_duration
    std::int64_t frames = 0;
    std::string video_encoder;
    int threads = 0;    // hilos que compusieron
    double seconds = 0; // lo que tardó, en total
    RenderTimes times;
};

// Renderiza la timeline del proyecto a `out`. Error si la timeline está vacía, si falta algo que
// el render aún no sabe dibujar (salvo con omit_unsupported) o si falla el decode o el encode.
[[nodiscard]] Result<RenderStats> render_project(const LoadedProject& project, const std::filesystem::path& out,
                                                 const RenderOptions& options = {});

} // namespace gc::exporter
