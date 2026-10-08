#pragma once

// Lectura y escritura del modelo en JSON, con las mismas reglas que Pydantic v2
// en modo «lax» (las que aplica video-yt al cargar un proyecto):
//
//   float ← número, bool o texto numérico ("1.5", " 1_000 ", "inf")
//   int   ← entero, real sin decimales (5.0), bool o texto entero ("7", "7.0")
//   bool  ← bool, 0/1, 0.0/1.0 o texto (true/false, yes/no, on/off, 1/0, t/f, y/n)
//   str   ← solo texto
//
// Un valor que no encaja, un campo obligatorio ausente o fuera de rango da un
// Error de validación con la ruta del campo ("timeline.clips[3].reframe.zoom").

#include "gc/core/error.hpp"
#include "gc/core/json.hpp"
#include "gc/core/model.hpp"

namespace gc::core {

enum class DumpMode {
    // Igual que model_dump() de Pydantic: todos los campos del modelo, en su orden,
    // con null en los opcionales vacíos. Las claves desconocidas no salen.
    Model,
    // Model + las claves desconocidas que se leyeron (`extra`), al final de cada
    // objeto. Es el modo para reescribir archivos sin perder datos.
    Preserve,
};

// Proyecto desde una entrada cruda de projects.json. Hace lo mismo que
// projects._project_from_dict: rellena el id de los clips sin id con su index y
// migra la timeline al schema actual antes de validar.
[[nodiscard]] Result<Project> project_from_json(const Json& raw);
// Project(**raw) de Python: valida la entrada tal cual, sin rellenar ids ni migrar la
// timeline (lo que hacen set_folder, apply_manifest y save_timeline de projects.py).
[[nodiscard]] Result<Project> project_from_json_as_is(const Json& raw);

// Los materiales sueltos de un proyecto (lo que reciben add_clips, add_audio, add_image y
// add_transcript), sin migración.
[[nodiscard]] Result<ClipInfo> clip_info_from_json(const Json& raw);
[[nodiscard]] Result<AudioInfo> audio_info_from_json(const Json& raw);
[[nodiscard]] Result<ImageInfo> image_info_from_json(const Json& raw);
[[nodiscard]] Result<Transcript> transcript_from_json(const Json& raw);
[[nodiscard]] Json to_json(const ClipInfo& clip, DumpMode mode = DumpMode::Model);
[[nodiscard]] Json to_json(const AudioInfo& audio, DumpMode mode = DumpMode::Model);
[[nodiscard]] Json to_json(const ImageInfo& image, DumpMode mode = DumpMode::Model);
[[nodiscard]] Json to_json(const Transcript& transcript, DumpMode mode = DumpMode::Model);

// Timeline cruda (p. ej. la que manda el editor): migración + validación.
[[nodiscard]] Result<Timeline> timeline_from_json(const Json& raw);

[[nodiscard]] Result<ProjectGroup> project_group_from_json(const Json& raw);

// Un clip o una pista sueltos (p. ej. el `clip` de la op add_clip), sin migración.
[[nodiscard]] Result<TimelineClip> clip_from_json(const Json& raw);
[[nodiscard]] Result<TimelineTrack> track_from_json(const Json& raw);
[[nodiscard]] Result<Keyframe> keyframe_from_json(const Json& raw);
[[nodiscard]] Json to_json(const TimelineClip& clip, DumpMode mode = DumpMode::Model);
[[nodiscard]] Json to_json(const TimelineTrack& track, DumpMode mode = DumpMode::Model);

// data/projects.json completo: {"projects": [...], "groups": [...]}.
[[nodiscard]] Result<ProjectsFile> projects_file_from_json(const Json& raw);

[[nodiscard]] Json to_json(const Project& project, DumpMode mode = DumpMode::Model);
[[nodiscard]] Json to_json(const Timeline& timeline, DumpMode mode = DumpMode::Model);
[[nodiscard]] Json to_json(const ProjectGroup& group, DumpMode mode = DumpMode::Model);
[[nodiscard]] Json to_json(const ProjectsFile& file, DumpMode mode = DumpMode::Model);

} // namespace gc::core
