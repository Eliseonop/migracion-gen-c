#pragma once

// El export de una timeline a MP4, como compose.render de video-yt: el audio entero con el grafo
// de build_command (gc::media::mix_audio), y por fotograma, evaluar (gc::core::evaluate),
// componer las capas y los textos (gc::render) y codificar (gc::media::Mp4Writer). Mismo tamaño
// (par), fps y duración que el export: N = ⌈total_µs · fps / 10⁶⌉ fotogramas, como su `-t`.
// En tubería: un hilo evalúa y decodifica en orden, varios componen y otro codifica en orden.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <string>

#include "gc/core/error.hpp"
#include "gc/export/project.hpp"

namespace gc::exporter {

struct RenderOptions {
    // Lo que el render aún no sabe dibujar se salta con un aviso en vez de ser un error.
    bool omit_unsupported = false;
    std::string video_encoder; // "" = elegirlo como gpu.py (Mp4Writer)
    int threads = 0;           // hilos que componen; 0 = según los núcleos (de 1 a 8)
    // Las capas quietas de abajo no se redibujan (render::LayerCache). Da el mismo vídeo; false,
    // para comprobarlo.
    bool layer_cache = true;
    // Decode por hardware (media::DecodeSpec::hwaccel): los mismos fotogramas que por software, con
    // menos CPU en los códecs que la GPU decodifica (H.264, HEVC, VP9, AV1). Video-yt no lo usa.
    bool hwaccel = false;
    // Tras cada fotograma: cuántos van y cuántos hay.
    std::function<void(std::int64_t done, std::int64_t total)> on_progress;
    // Si devuelve true (se mira antes de evaluar cada fotograma), el render para y devuelve las
    // estadísticas con `cancelled`: el MP4 queda a medias y lo borra quien lo pidió (el editor).
    std::function<bool()> cancelled;
    // Cada aviso distinto, una vez (material que falta, capas que se omiten).
    std::function<void(const std::string& message)> on_warning;
    // Cada fotograma ya compuesto, con los textos (RGBA premultiplicado y opaco, width · height · 4),
    // antes de pasarlo a YUV. Se llama desde los hilos que componen, en cualquier orden; los
    // píxeles valen solo durante la llamada. Para los tests: el codificador por software no da
    // siempre los mismos bytes (OpenH264 con varios hilos), y esto sí.
    std::function<void(std::int64_t tick, std::span<const std::uint8_t> rgba)> on_composed;
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
    std::int64_t layer_cache_hits = 0; // fotogramas cuyas capas quietas de abajo no se redibujaron
    double seconds = 0; // lo que tardó, en total
    RenderTimes times;
    bool cancelled = false; // paró a petición (RenderOptions::cancelled): el MP4 está a medias
};

// Renderiza la timeline del proyecto a `out`. Error si la timeline está vacía, si falta algo que
// el render aún no sabe dibujar (salvo con omit_unsupported) o si falla el decode o el encode.
[[nodiscard]] Result<RenderStats> render_project(const LoadedProject& project, const std::filesystem::path& out,
                                                 const RenderOptions& options = {});

} // namespace gc::exporter
