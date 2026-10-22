#pragma once

// Segmentos POR REFERENCIA y seguimiento de caras cacheado (espejo de backend/app/segments.py).
//
// «Crear clip» del Clip Editor guarda cada rango marcado sobre un vídeo del material como un
// material nuevo SIN archivo propio: apunta al archivo del original (`filename`) con
// in_point/out_point en segundos de ese archivo. Sin render: una escritura en projects.json.
//
// El seguimiento de caras de un material (`face_track`) se guarda en el propio material, en
// tiempo del archivo, y siembra su `reframe`; no se vuelve a analizar mientras el rango no
// cambie. El detector (YuNet, F6) y la sonda de duración (gc_media) se inyectan.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "gc/core/error.hpp"
#include "gc/core/ids.hpp"
#include "gc/core/json.hpp"
#include "gc/core/model.hpp"
#include "gc/core/projects_store.hpp"

namespace gc::core {

inline constexpr double kMinSegment = 0.3; // s: por debajo no merece la pena un clip

// find_clip: el material de vídeo por su índice o su id (los dos como texto).
[[nodiscard]] const ClipInfo* find_material_clip(const Project& project, std::string_view ident);

// file_range: [in, out] del material en segundos de SU archivo. Sin in/out_point, el archivo es
// el clip: de 0 a su duración (start/end pueden ser tiempos del vídeo de YouTube).
[[nodiscard]] std::pair<double, double> material_file_range(const ClipInfo& clip);

// normalize_ranges: los rangos pedidos ([{start, end, label?, description?…}]) validados y
// acotados a [0, file_dur] (file_dur ≤ 0: sin tope), con start y end redondeados a ms y el resto
// de claves tal cual. Errores con el texto de Python: «Segmento N: start/end no válidos.»,
// «Segmento N: demasiado corto (mínimo 0.3s).» y «No hay segmentos.».
[[nodiscard]] Result<Json> normalize_segment_ranges(const Json& raw, double file_dur);

// _mmss: «m:ss» de unos segundos redondeados (al par). Error con infinito o NaN.
[[nodiscard]] Result<std::string> segment_mmss(double seconds);

// La duración del archivo (youtube_audio.probe_duration); nada si no se puede sondear.
using DurationProbe = std::function<std::optional<double>(const std::filesystem::path&)>;

// create_segments: un material por referencia por cada rango del vídeo `source_ident` del
// proyecto (con la etiqueta «<origen> · m:ss–m:ss» si el rango no trae una) y los devuelve ya
// guardados, en el orden del proyecto. Su padre es el del origen (o el origen).
[[nodiscard]] Result<std::vector<ClipInfo>> create_segments(ProjectsStore& store, std::string_view pid,
                                                            std::string_view source_ident, const Json& raw,
                                                            const DurationProbe& probe, IdSource& ids);

// seed_keyframes: el seguimiento denso → pocos keyframes estables: ventanas de ~1 s (como mucho
// ~12), la mediana de cada una, una zona muerta de 0,025 y los extremos anclados.
[[nodiscard]] std::vector<Keyframe> seed_face_keyframes(std::span<const TrackPoint> track, double start,
                                                        double end);

// cached_track: el seguimiento guardado del material si es del mismo rango (±0,05 s).
[[nodiscard]] const FaceTrack* cached_face_track(const ClipInfo& clip, double start, double end);

// Lo que da el detector de caras (detect.face_track) en [start, end] del archivo.
struct FaceDetection {
    std::int64_t width = 0;
    std::int64_t height = 0;
    double fps = 25.0;
    std::int64_t samples = 0;
    std::vector<TrackPoint> track;
};
using FaceDetector =
    std::function<Result<FaceDetection>(const std::filesystem::path&, double start, double end, int samples)>;

struct FaceTrackResult {
    FaceTrack track;
    bool from_cache = false;
};

// face_track_material: las caras del material en [start, end] (por defecto, su rango), del caché
// salvo `force`. Si el rango es el del material, se guardan en él con el reframe sembrado.
[[nodiscard]] Result<FaceTrackResult> face_track_material(ProjectsStore& store, std::string_view pid,
                                                          std::string_view ident, std::optional<double> start,
                                                          std::optional<double> end, int samples, bool force,
                                                          const FaceDetector& detect,
                                                          const std::function<std::string()>& now);

} // namespace gc::core
