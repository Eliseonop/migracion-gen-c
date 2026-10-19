#pragma once

// Material del proyecto soltado en la timeline (espejo de dragPayload de MaterialClipGrid.jsx,
// makeClip de editorModel.js, placeAsset de VideoEditor.jsx y segmentRange/materialDuration/
// hasFaceTrack de clipExtract.js). Trabaja sobre JSON, como el JS: el clip sale tal como lo
// guardaría el editor y se valida al meterlo en la timeline.

#include <optional>
#include <string>
#include <string_view>

#include "gc/core/ids.hpp"
#include "gc/core/json.hpp"

namespace gc::core {

// {in, out} del material si es un segmento (in_point < out_point).
[[nodiscard]] std::optional<std::pair<double, double>> segment_range(const Json& item);
// El tramo si es un segmento; si no, end − start o `duration`.
[[nodiscard]] double material_duration(const Json& item);
[[nodiscard]] bool has_face_track(const Json& item);
// laneKindForAsset: la pista donde cae el material (video, text o audio).
[[nodiscard]] std::string lane_kind_for_asset(std::string_view asset_kind);

// Lo que viaja al arrastrar una tarjeta de material (`asset_kind`: clips, images, audios o sfx).
[[nodiscard]] Json drag_payload(std::string_view asset_kind, const Json& item);
// Clip nuevo de vídeo, imagen o audio a partir de un material.
[[nodiscard]] Json make_clip_json(std::string_view asset_kind, const Json& item, std::string_view track_id,
                                  double start, const Json& duration, IdSource& ids);
// El clip que crea soltar `payload` en `track_id` a `start`; `fit_duration` es la del clip al que
// reemplaza (un vídeo o audio más largo se recorta; una imagen toma esa duración).
[[nodiscard]] Json place_asset_clip(const Json& payload, std::string_view track_id, double start,
                                    std::optional<double> fit_duration, IdSource& ids);

} // namespace gc::core
