#pragma once

// Congelar fotograma (#11), la parte de freeze.py: el fotograma de un clip de vídeo como imagen
// del proyecto. La op que parte el vídeo e inserta la imagen es core::ops::FreezeFrame.
//
// extract_still: el fotograma en `at_time` (s del ARCHIVO) a la resolución NATIVA, como el
// `ffmpeg -ss {t:.3f} -i <ruta> -frames:v 1 -vf format=rgb24` de extract_still (con
// `ASSUME_BT709_FILTER` delante si el material no declara su color, como el export); pasado el
// último fotograma, el último (su `-sseof -0.2 … -update 1`). Sin alfa: el PNG es opaco (rgb24).

#include <filesystem>
#include <string>
#include <string_view>

#include "gc/core/clip_media.hpp"
#include "gc/core/error.hpp"
#include "gc/core/ids.hpp"
#include "gc/core/model.hpp"
#include "gc/core/projects_store.hpp"

namespace gc::exporter {

inline constexpr double kFreezeDuration = 3.0; // FREEZE_DUR (como CapCut)

// PNG (bytes) del fotograma. Error con el texto de video-yt si no sale ninguno.
[[nodiscard]] Result<std::string> extract_still(const std::filesystem::path& path, double at_time, bool untagged);

// freeze_frame_image: el fotograma `src_time` del material de `clip` como imagen del proyecto
// (`<nombre>_congelado_{t:.2f}s.png`, etiqueta «<clip> · congelado {t:.2f}s», origen «freeze» y
// fuente «generated»). Errores: «Solo se puede congelar un fotograma de un clip de vídeo.» y «No se
// encuentra el archivo del clip.».
[[nodiscard]] Result<core::ImageInfo> freeze_frame_image(core::ProjectsStore& store, std::string_view project_id,
                                                         const core::TimelineClip& clip, double src_time,
                                                         const core::MediaDirs& dirs, core::IdSource& ids);

} // namespace gc::exporter
