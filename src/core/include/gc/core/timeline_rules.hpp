#pragma once

// Reglas de la timeline que comparten edición, render y UI:
//   - familias de clip ↔ pista       (clip_kind.py)
//   - velocidad y duración            (clip_speed.py)
//   - pila de capas vídeo/texto       (track_stack.py)
//   - validación y solapes            (timeline_ops.validate_timeline/track_overlaps)

#include <cstddef>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "gc/core/error.hpp"
#include "gc/core/model.hpp"

namespace gc::core {

// --- Familias de clip ↔ pista ---------------------------------------------------------

// Pista en la que vive un tipo de clip ("video", "audio" o "text"); nullopt si el
// tipo no existe. image/shape/motion/adjustment viven en pistas de vídeo.
[[nodiscard]] std::optional<std::string_view> track_kind_for_clip(std::string_view clip_kind);

// Error si el tipo de clip no existe (el ValueError de Python).
[[nodiscard]] Result<bool> clip_fits_track(std::string_view clip_kind, std::string_view track_kind);

// --- Velocidad y duración ------------------------------------------------------------------

inline constexpr double kSpeedMin = 0.1;
inline constexpr double kSpeedMax = 10.0;

// Velocidad efectiva: 1 en texto/imagen/ajuste; si no, `speed` acotada a [0.1, 10].
[[nodiscard]] double clip_speed(const TimelineClip& clip);
// Tramo de fuente usado: round(out - in, 3), nunca negativo.
[[nodiscard]] double clip_source_duration(const TimelineClip& clip);
// Lo que ocupa en la timeline: round(fuente / velocidad, 3).
[[nodiscard]] double clip_timeline_duration(const TimelineClip& clip);
// Variante sin redondeo de timeline_ops._clip_tl_dur (la de los avisos de solape).
[[nodiscard]] double clip_span_duration(const TimelineClip& clip);
// Fin del clip en la timeline (start + clip_timeline_duration).
[[nodiscard]] double clip_end(const TimelineClip& clip);

// --- Pila de capas -----------------------------------------------------------------------
// Vídeo y texto forman UNA pila en el orden de timeline.tracks (0 = fondo, la
// última = delante). En pantalla se ve invertida, con el audio debajo.

[[nodiscard]] bool is_stack_track(const TimelineTrack& track);
// track_id → capa de la pila (0 = fondo). El audio no aparece. Con ids repetidos gana la
// última pista (con su capa), como el dict de Python y el Map de JS.
[[nodiscard]] std::unordered_map<std::string, std::size_t> stack_layers(const std::vector<TimelineTrack>& tracks);
// Pistas de texto con alguna pista de vídeo por encima en la pila. Sus textos se
// componen en su capa, detrás de ese vídeo.
[[nodiscard]] std::set<std::string> buried_text_track_ids(const std::vector<TimelineTrack>& tracks);
// Índices de `tracks` en el orden de pantalla: la pila invertida y después el audio.
[[nodiscard]] std::vector<std::size_t> display_order(const std::vector<TimelineTrack>& tracks);
// Hueco para una pista nueva sin cambiar lo que se ve.
[[nodiscard]] std::size_t default_track_index(const std::vector<TimelineTrack>& tracks, std::string_view kind);
// Inserta en `index` (acotado) o, sin índice, en su sitio por defecto.
void insert_track(std::vector<TimelineTrack>& tracks, TimelineTrack track,
                  std::optional<std::size_t> index = std::nullopt);
// Pistas con `track_id` encima ("above") o debajo ("below") de `target_id`, tal como
// se ven en pantalla y dentro de su grupo (pila o audio).
[[nodiscard]] Result<std::vector<TimelineTrack>> reorder_tracks(const std::vector<TimelineTrack>& tracks,
                                                                std::string_view track_id,
                                                                std::string_view target_id,
                                                                std::string_view place);

// --- Validación ------------------------------------------------------------------------

// Pares de clips que se pisan dentro de una pista (solapes heredados de proyectos viejos).
[[nodiscard]] std::vector<std::pair<std::string, std::string>> track_overlaps(const Timeline& timeline,
                                                                             std::string_view track_id);
// "solape en V1: c1 ↔ c2"
[[nodiscard]] std::vector<std::string> track_overlap_warnings(const Timeline& timeline, std::string_view track_id);
// Incidencias DURAS (referencias rotas, rangos imposibles). Vacío = sana. Error si
// algún clip tiene un tipo desconocido.
[[nodiscard]] Result<std::vector<std::string>> validate_timeline(const Timeline& timeline);

} // namespace gc::core
