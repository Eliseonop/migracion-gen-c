#pragma once

// Tramos de alto interés del heatmap («Most Replayed») de un vídeo de YouTube, espejo de
// heatmap._pick_segments de video-yt. El heatmap lo da yt-dlp (campo `heatmap` de la
// información del vídeo); aquí solo se convierte en tramos recortables.

#include <cstdint>
#include <span>
#include <vector>

namespace gc::core {

// Un punto del heatmap: el intervalo [start_time, end_time] y su valor (0–1).
struct HeatPoint {
    double start_time = 0.0;
    double end_time = 0.0;
    double value = 0.0;
};

// Un tramo (el Segment de schemas.py sin label ni description). index empieza en 1.
struct HeatSegment {
    std::int64_t index = 0;
    double start = 0.0;
    double end = 0.0;
    double score = 0.0;
    double duration = 0.0;
};

// Los puntos con valor ≥ min_score, agrupados si son seguidos (por start_time); cada grupo
// con `padding` s a cada lado (dentro de [0, duration]), como mucho `max_duration` s desde su
// inicio y el pico como score. Se queda con los `max_clips` de más score (el corte de una
// lista de Python: uno negativo quita los últimos) y los ordena por tiempo.
[[nodiscard]] std::vector<HeatSegment> pick_heatmap_segments(std::span<const HeatPoint> heatmap, double duration,
                                                             double min_score, std::int64_t max_clips,
                                                             double max_duration, double padding);

} // namespace gc::core
