#pragma once

// Dónde cae lo que se arrastra en la timeline del editor (espejo de timelineAlign.js,
// dropIntent.js, magnetic.magneticCut, noOverlap.clampGroupDelta y beats.snapTargets de
// video-yt): el imán con los bordes de otras pistas, marcadores y beats; la pista o la pista
// nueva bajo el puntero; el hueco donde entra; y el tope de un grupo contra sus vecinos.
// Solo decide: quien llama aplica el resultado.

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "gc/core/json.hpp"
#include "gc/core/model.hpp"
#include "gc/core/timeline_ui.hpp"

namespace gc::core {

// --- Imán (timelineAlign.js) ---------------------------------------------------------------

inline constexpr double kAlignSnapPx = 8;

// Lo que cuenta de un clip para alinear: su pista y sus bordes.
struct AlignClip {
    std::string id;
    std::string track_id;
    double start = 0;
    double end = 0;
};
enum class AlignEdge { Start, End, Both };

[[nodiscard]] double align_threshold_sec(double pps, double px = kAlignSnapPx);
[[nodiscard]] AlignClip as_align_clip(const TimelineClip& clip);
[[nodiscard]] std::vector<AlignClip> align_others(const ClipList& clips,
                                                  std::span<const std::string> exclude);
// Beats del clip en segundos de timeline (dentro de su recorte, uno de cada `every`).
[[nodiscard]] std::vector<double> clip_beat_times(const TimelineClip& clip);
// Marcadores y beats como puntos de imán (sin los beats de `exclude` ni de clips desactivados).
[[nodiscard]] std::vector<AlignClip> snap_targets(const std::vector<Json>& markers, const ClipList& clips,
                                                  std::span<const std::string> exclude = {});

// Instantes donde un borde de lo que se mueve coincide con un borde de otra pista.
[[nodiscard]] std::vector<double> timeline_align_hits(std::span<const AlignClip> moving,
                                                      std::span<const AlignClip> others, double threshold,
                                                      AlignEdge which = AlignEdge::Both);
[[nodiscard]] std::optional<double> nearest_align_time(double time, std::string_view track_id,
                                                       std::span<const AlignClip> others, double threshold);

struct SnapResult {
    double delta = 0; // desplazamiento extra (snap_group_delta) o el total (snap_clip_group)
    std::vector<double> times;
};
[[nodiscard]] SnapResult snap_group_delta(std::span<const AlignClip> moving,
                                          std::span<const AlignClip> others, double threshold);

struct SnapMove {
    double start = 0;
    std::vector<double> times;
};
// `track_id` vacío = la del clip.
[[nodiscard]] SnapMove snap_clip_move(const TimelineClip& orig, double delta,
                                      std::span<const AlignClip> others, double threshold,
                                      std::string_view track_id = {});

struct SnapTrim {
    ClipPatch patch;
    std::vector<double> times;
    double delta = 0;
};
[[nodiscard]] SnapTrim snap_clip_trim(const TimelineClip& orig, TrimEdge edge, double delta,
                                      std::span<const AlignClip> others, double threshold);
[[nodiscard]] SnapResult snap_clip_group(std::span<const TimelineClip* const> origs, double delta,
                                         std::span<const AlignClip> others, double threshold);

// --- Grupo sin montar (noOverlap.clampGroupDelta) ----------------------------------------

// `delta` acotado para que el grupo se pare contra el primer vecino que no se mueve. Un clip que ya
// estaba solapado o en una pista magnética no frena.
[[nodiscard]] double clamp_group_delta(const ClipList& clips, std::span<const TimelineClip* const> origs,
                                       double delta, const std::vector<TimelineTrack>& tracks);

// --- Dónde cae (dropIntent.js y magnetic.magneticCut) -------------------------------------

inline constexpr double kBoundaryPx = 6;
inline constexpr double kStickPx = 48;

// Una fila tal como se ve: px del contenido, en orden de pantalla.
struct TrackRow {
    std::string id;
    double top = 0;
    double bottom = 0;
};
// Pista nueva encima o debajo de `target_id` (sin `target_id`: la primera de su bloque).
struct TrackSlot {
    std::optional<std::string> target_id;
    bool below = false;
    bool operator==(const TrackSlot&) const = default;
};
struct Zone {
    std::optional<std::string> track_id = {};
    std::optional<TrackSlot> slot = {};
    double line_y = 0;
};

[[nodiscard]] const TimelineClip* clip_at_time(const ClipList& clips, std::string_view track_id, double t,
                                               std::span<const std::string> exclude = {});
[[nodiscard]] const TimelineClip* clip_colliding_with(const ClipList& clips, std::string_view track_id,
                                                      double start, double duration,
                                                      std::span<const std::string> exclude = {});
// Zona bajo el puntero para un clip de `kind` (video, text o audio).
[[nodiscard]] Zone zone_at(const std::vector<TimelineTrack>& tracks, std::span<const TrackRow> geo,
                           std::string_view kind, double y, double boundary_px = kBoundaryPx,
                           std::string_view origin_id = {}, bool origin_alone = false);
// Inicio en la pista: libre, ahí (pegado al vecino a menos de `snap_sec`); ocupado, el hueco más
// cercano si está a ≤ `stick_sec`; si no, nada.
[[nodiscard]] std::optional<double> place_on_track(const ClipList& clips, std::string_view track_id,
                                                   double desired, double duration,
                                                   std::span<const std::string> exclude = {},
                                                   double stick_sec = 0, double snap_sec = 0);
struct LiftTarget {
    std::optional<std::string> track_id = {};
    std::optional<TrackSlot> slot = {};
};
// La pista vecina libre de su tipo o una nueva pegada a `from_id`.
[[nodiscard]] std::optional<LiftTarget> lift_target(const std::vector<TimelineTrack>& tracks,
                                                    const ClipList& clips, std::string_view from_id,
                                                    double start, double duration,
                                                    std::span<const std::string> exclude = {});

struct MagneticCut {
    double start = 0;
    std::size_t index = 0;
};
// Corte de una pista magnética donde entra lo soltado en `time`.
[[nodiscard]] MagneticCut magnetic_cut(const ClipList& clips, std::string_view track_id, double time,
                                       std::span<const std::string> exclude = {});

struct DropQuery {
    std::string_view kind; // video, text o audio
    double y = 0;
    double time = 0; // inicio del fantasma
    double duration = 0;
    std::optional<double> pointer_time = {};
    std::span<const std::string> exclude = {};
    std::string_view origin_id;
    bool origin_alone = false;
    bool replace = false; // Alt
    double pps = 60;
    double snap_sec = 0;
};
struct Drop {
    std::optional<std::string> track_id = {};
    std::optional<TrackSlot> slot = {};
    double line_y = 0;
    double start = 0;
    std::optional<std::string> replace_id = {};
    bool insert = false;
    bool lifted = false;
};
[[nodiscard]] std::optional<Drop> drop_target(const std::vector<TimelineTrack>& tracks, const ClipList& clips,
                                              std::span<const TrackRow> geo, const DropQuery& q);

// Índice del array `tracks` para una pista nueva de `kind` encima o debajo de su grupo.
[[nodiscard]] std::size_t new_track_index(const std::vector<TimelineTrack>& tracks, std::string_view kind,
                                          bool below);
// Hueco entre filas en pantalla (0 = encima de la primera); -1 si `slot` no existe.
[[nodiscard]] long slot_boundary(const std::vector<TimelineTrack>& tracks, const TrackSlot& slot);
// Índice del array para una pista nueva en `slot`.
[[nodiscard]] std::size_t slot_index(const std::vector<TimelineTrack>& tracks, const TrackSlot& slot);

} // namespace gc::core
