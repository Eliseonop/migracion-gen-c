#pragma once

// El extractor del Clip Editor (frontend/src/features/editor/clipExtract.js y setMark/hasMarkRange de
// motionTarget.js de video-yt): Z y X marcan el inicio y el fin sobre la timeline del Clip Editor, las
// marcas se arrastran en la regla y «Crear clip» guarda ese tramo en el material por referencia, con
// create_segments (segments.hpp). segmentRange, materialDuration y hasFaceTrack, del mismo archivo,
// están en material_drop.hpp.

#include <expected>
#include <limits>
#include <optional>
#include <string>
#include <string_view>

#include "gc/core/json.hpp"
#include "gc/core/timeline_ui.hpp"

namespace gc::core {

// Una marca de rango: sus dos extremos en segundos de la timeline (EMPTY_MARK: los dos vacíos).
struct ClipMark {
    std::optional<double> in = {};
    std::optional<double> out = {};
    friend bool operator==(const ClipMark&, const ClipMark&) = default;
};
enum class MarkEnd { In, Out };

// num() de clipExtract.js para un valor de JSON: Number(v) si es finito; null y "" no cuentan.
[[nodiscard]] std::optional<double> mark_number(const Json& v);
// La marca de un {in, out} de JSON con ese num() (lo que no es un objeto, vacía).
[[nodiscard]] ClipMark mark_from_json(const Json& mark);
[[nodiscard]] Json mark_to_json(const ClipMark& mark);

// setMark (las teclas): ese extremo en `t` (≥ 0, a ms con Math.round). Si el rango queda al revés o
// vacío, se descarta el OTRO extremo, como en Premiere o Resolve.
[[nodiscard]] ClipMark set_mark(const ClipMark& mark, MarkEnd which, double t);
// hasMarkRange: las dos marcas puestas y la entrada antes que la salida.
[[nodiscard]] bool has_mark_range(const ClipMark& mark);
// dragMark (arrastrar en la regla): nunca descarta el otro extremo; se para a kMinSegment de él.
// `t` se acota a [0, duration] (una duración no finita o ≤ 0, sin tope).
[[nodiscard]] ClipMark drag_mark(const ClipMark& mark, MarkEnd which, double t,
                                 double duration = std::numeric_limits<double>::infinity());

// El tramo marcado en segundos del ARCHIVO del vídeo (a ms), y el clip de la timeline que lo contiene.
struct SourceRange {
    const TimelineClip* clip = nullptr;
    double start = 0.0;
    double end = 0.0;
};
// markToSourceRange: los dos extremos tienen que caer en el mismo clip de vídeo (la timeline del Clip
// Editor puede tener cortes). Si no, el error que ve el usuario.
[[nodiscard]] std::expected<SourceRange, std::string> mark_to_source_range(const ClipList& clips,
                                                                           const ClipMark& mark);

// segmentLabel: el nombre por defecto del clip extraído, «<título> · m:ss–m:ss».
[[nodiscard]] std::string segment_label(std::string_view title, double start, double end);

// DESC_MODES del modal «Crear clip»: qué descripción lleva el clip nuevo.
enum class SegmentDescMode { Manual, Source, None };
[[nodiscard]] std::string_view desc_mode_id(SegmentDescMode mode);
// El modo de su id («manual», «source» o «none»); lo demás escribe la descripción, como el JS.
[[nodiscard]] SegmentDescMode desc_mode_from(std::string_view id);
// segmentDescription: la descripción final según el modo, sin espacios a los lados.
[[nodiscard]] std::string segment_description(SegmentDescMode mode, std::string_view description,
                                              std::string_view source_description);

} // namespace gc::core
