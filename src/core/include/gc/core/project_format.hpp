#pragma once

// El formato de salida del proyecto en el editor (lib/projectFormat.js y FPS_CHOICES de
// lib/projectFps.js de video-yt): relación de aspecto × resolución (el lado corto) y fps. Lo usa la
// fila del reproductor (EdViewerTools.jsx). El mismo cálculo del lado de Python (timeline_ops.size_for)
// está en edit_ops.cpp.

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace gc::core {

struct AspectChoice {
    std::string_view id;
    int rw;
    int rh;
};

inline constexpr std::array<AspectChoice, 6> kProjectAspects{{
    {"16:9", 16, 9},
    {"9:16", 9, 16},
    {"1:1", 1, 1},
    {"4:3", 4, 3},
    {"3:4", 3, 4},
    {"4:5", 4, 5},
}};

struct ResolutionChoice {
    int id; // los píxeles del lado corto
    std::string_view label;
};

inline constexpr std::array<ResolutionChoice, 4> kProjectResolutions{{
    {480, "480p"},
    {720, "720p"},
    {1080, "1080p"},
    {2160, "4K"},
}};

inline constexpr std::array<int, 5> kFpsChoices{24, 25, 30, 50, 60};
inline constexpr int kFormatDimMin = 144;
inline constexpr int kFormatDimMax = 4096;

struct FrameSize {
    std::int64_t w;
    std::int64_t h;
    friend bool operator==(const FrameSize&, const FrameSize&) = default;
};

// evenDim: par (H.264/yuv420p) y dentro de [144, 4096]; NaN vale 0 (y se queda en 144).
[[nodiscard]] std::int64_t format_even_dim(double n);
// sizeForRatio: rw:rh con `short_side` px en el lado corto; el largo se limita a 4096.
[[nodiscard]] FrameSize size_for_ratio(double rw, double rh, double short_side);
// aspectOf: el id de kProjectAspects a menos del 1 % de la proporción, o «custom».
[[nodiscard]] std::string aspect_of(double w, double h);
// resolutionOf: el lado corto si es una de kProjectResolutions; si no, 0 («custom» en JS).
[[nodiscard]] int resolution_of(double w, double h);
// withAspect: otra proporción con el mismo lado corto (un id desconocido no cambia nada).
[[nodiscard]] FrameSize with_aspect(std::string_view aspect_id, std::int64_t w, std::int64_t h);
// withResolution: otro lado corto con la misma proporción.
[[nodiscard]] FrameSize with_resolution(double short_side, std::int64_t w, std::int64_t h);
// withMediaAspect («Original»): la proporción del medio con el lado corto actual.
[[nodiscard]] FrameSize with_media_aspect(double mw, double mh, std::int64_t w, std::int64_t h);

} // namespace gc::core
