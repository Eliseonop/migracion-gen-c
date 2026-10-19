#pragma once

// Lo que hace la timeline del editor al arrastrar, recortar y seleccionar (espejo de
// frontend/src/features/editor/editorModel.js de video-yt). Es la lógica de la UI, no de las
// ops: usa la duración de JS (clipDur, sin redondear; la figura tampoco cambia de velocidad) y
// redondea como `+x.toFixed(3)`.

#include <limits>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "gc/core/cow.hpp"
#include "gc/core/model.hpp"

namespace gc::core {

using ClipList = std::vector<Cow<TimelineClip>>;

// x.toFixed(digits) de JS (|x| < 1e21): redondeo del valor exacto, con el empate lejos del cero.
[[nodiscard]] std::string js_to_fixed(double x, int digits);
// +x.toFixed(digits).
[[nodiscard]] double js_fixed(double x, int digits);
[[nodiscard]] double js_fixed3(double x);

// clipSpeed, clipDur y clipEnd de editorModel.js.
[[nodiscard]] double ui_clip_speed(const TimelineClip& clip);
[[nodiscard]] double ui_clip_duration(const TimelineClip& clip);
[[nodiscard]] double ui_clip_end(const TimelineClip& clip);
// trackKindForClip: la pista que admite el clip (vídeo, imagen, figura y ajuste van en vídeo).
[[nodiscard]] std::string lane_kind(std::string_view clip_kind);
// isGeneratedDurationClip: texto, imagen, figura o ajuste (su duración no la marca un archivo).
[[nodiscard]] bool has_free_duration(const TimelineClip& clip);

// Clips de una pista, de izquierda a derecha (empate por id).
[[nodiscard]] std::vector<const TimelineClip*> clips_on_track_sorted(const ClipList& clips,
                                                                     std::string_view track_id);

// Primer inicio ≥ `desired` donde cabe un clip de `duration` sin solapar (insertar, duplicar).
[[nodiscard]] double free_start_on_track(const ClipList& clips, std::string_view track_id, double desired,
                                         double duration, std::span<const std::string> exclude = {});
// Inicio más cercano a `desired` que no solapa: al arrastrar, el clip se pega al vecino.
[[nodiscard]] double clamp_start_no_overlap(const ClipList& clips, std::string_view track_id, double desired,
                                            double duration, std::span<const std::string> exclude = {});

struct TrimBounds {
    double left = 0.0;
    double right = std::numeric_limits<double>::infinity();
};
enum class TrimEdge { Left, Right };

// Hasta dónde puede crecer el clip por cada lado sin tocar a sus vecinos.
[[nodiscard]] TrimBounds trim_bounds(const ClipList& clips, const TimelineClip& clip);
// Acota el desplazamiento de un borde para que no invada al vecino.
[[nodiscard]] double clamp_trim_delta(const ClipList& clips, const TimelineClip& clip, TrimEdge edge,
                                      double delta);

// Campos que cambia un recorte (los que faltan no cambian).
struct ClipPatch {
    std::optional<double> start = {};
    std::optional<double> in_point = {};
    std::optional<double> out_point = {};
    std::optional<double> source_duration = {};

    void apply(TimelineClip& clip) const;
};
inline constexpr double kMinClipDuration = 0.15; // GEN_MIN_DUR

// trimClipPatch: mover un borde `delta` s de timeline (sin mirar a los vecinos).
[[nodiscard]] ClipPatch trim_clip_patch(const TimelineClip& clip, TrimEdge edge, double delta);

// Ids de `from` a `to` en el orden de la pista (incluidos). Sin `to`, nada; sin `from`, solo `to`.
[[nodiscard]] std::vector<std::string> range_select_on_track(const ClipList& clips, std::string_view track_id,
                                                             std::string_view from_id,
                                                             std::string_view to_id);

struct ClipSelection {
    std::vector<std::string> ids; // el primero es la referencia (p. ej. igualar duración)
    std::optional<std::string> anchor;
};
struct SelectMods {
    bool additive = false;   // Ctrl/Cmd: suma o quita, solo del mismo tipo
    bool range = false;      // Shift: rango en la pista; en otra pista, suma al grupo
    bool keep_group = false; // clic en un clip ya marcado de un grupo
};
// nextClipSelection: la selección tras hacer clic en `clicked_id`.
[[nodiscard]] ClipSelection next_clip_selection(const ClipList& clips, const ClipSelection& current,
                                                std::string_view clicked_id, SelectMods mods);

// emptiedTracks: de `candidates`, las pistas sin clips que se pueden quitar (sin bloquear y sin
// dejar su tipo sin ninguna pista libre).
[[nodiscard]] std::vector<std::string> emptied_tracks(const std::vector<TimelineTrack>& tracks,
                                                      const ClipList& clips,
                                                      std::span<const std::string> candidates);
// buildTrack de VideoEditor.jsx: una pista nueva de `kind` numerada tras las que hay (V3, A2…), con
// el id «<nombre>-<sufijo>».
[[nodiscard]] TimelineTrack build_track(std::string_view kind, const std::vector<TimelineTrack>& tracks,
                                        std::string_view id_suffix);

// groupMoveFromOrig: los inicios de un grupo desplazado `delta` desde los originales (id, start).
// Nadie queda antes de 0.
[[nodiscard]] std::vector<std::pair<std::string, double>>
group_move_starts(std::span<const std::pair<std::string, double>> originals, double delta);

} // namespace gc::core
