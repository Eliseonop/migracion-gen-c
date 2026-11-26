#pragma once

// Importar un vídeo, un audio o una imagen local al proyecto como material (espejo de videos.py
// de video-yt: import_video, import_video_file e import_audio; y de images.import_image). El archivo va a la carpeta del
// proyecto como «<nombre saneado>_<id>.<ext>», con su duración de la sonda (0 si falla), y se da
// de alta en projects.json. Los errores llevan el texto que ve el usuario («Formato de vídeo no
// válido. Usa MP4, MOV, MKV, WEBM…», «Archivo vacío.»).

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "gc/core/error.hpp"
#include "gc/core/ids.hpp"
#include "gc/core/model.hpp"
#include "gc/core/projects_store.hpp"
#include "gc/core/segments.hpp"

namespace gc::core {

inline constexpr std::array<std::string_view, 11> kImportVideoExts{
    ".mp4", ".mov", ".mkv", ".webm", ".avi", ".m4v", ".mp4v", ".mpg", ".mpeg", ".wmv", ".flv"};
inline constexpr std::array<std::string_view, 7> kImportAudioExts{".mp3", ".wav", ".m4a", ".aac",
                                                                  ".ogg", ".flac", ".wma"};

// Los datos de procedencia de un vídeo (los argumentos con nombre de import_video). Un texto
// vacío cuenta como no dado, como el `x or …` de Python.
struct ImportMeta {
    std::optional<std::string> label;
    std::optional<std::string> description;
    std::optional<std::string> origin; // «import» si no se da
    std::optional<std::string> source; // «external» si no se da
    std::optional<std::string> provider;
    std::optional<std::string> external_id;
    std::optional<std::string> source_url;
    std::optional<std::string> author;
    std::optional<std::string> license_info;
};

// import_video: los bytes de un vídeo subido (`filename` es su nombre, que da la extensión y el título).
[[nodiscard]] Result<ClipInfo> import_video(ProjectsStore& store, std::string_view pid, std::string_view filename,
                                            std::string_view data, const DurationProbe& probe, IdSource& ids,
                                            const ImportMeta& meta = {});
// Como import_video pero COPIA un archivo del disco (el studio: el original del usuario se queda
// donde está; import_video_file de Python lo mueve porque es una descarga temporal). «Archivo
// vacío.» si no es un archivo o está vacío.
[[nodiscard]] Result<ClipInfo> import_video_file(ProjectsStore& store, std::string_view pid,
                                                 const std::filesystem::path& src, std::string_view filename,
                                                 const DurationProbe& probe, IdSource& ids,
                                                 const ImportMeta& meta = {});
// Como import_video_file pero MUEVE el archivo, como import_video_file de Python con una descarga
// temporal: la grabación de un directo puede pesar gigas. Si no se puede renombrar (otro disco),
// lo copia y borra el original.
[[nodiscard]] Result<ClipInfo> adopt_video_file(ProjectsStore& store, std::string_view pid,
                                                const std::filesystem::path& src, std::string_view filename,
                                                const DurationProbe& probe, IdSource& ids,
                                                const ImportMeta& meta = {});
// import_audio: los bytes de un audio subido (título = su nombre sin la extensión).
[[nodiscard]] Result<AudioInfo> import_audio(ProjectsStore& store, std::string_view pid, std::string_view filename,
                                             std::string_view data, const DurationProbe& probe, IdSource& ids);
[[nodiscard]] Result<AudioInfo> import_audio_file(ProjectsStore& store, std::string_view pid,
                                                  const std::filesystem::path& src, std::string_view filename,
                                                  const DurationProbe& probe, IdSource& ids);

// El tipo de material de un archivo por su extensión («video», «audio» o «image»); nada si no es
// ninguno. Las imágenes (PNG, JPG, WebP, GIF) son las de images.IMAGE_EXTS.
[[nodiscard]] std::optional<std::string_view> import_kind(std::string_view filename);

// Lo que images.py hace con OpenCV, que gc_core no tiene: `to_png` es el cv2.imdecode +
// imencode(".png") de to_working_png (nada si no se puede leer; solo se llama con lo que no es
// ya un PNG) y `size` el probe_size del archivo guardado (nada si no se puede leer). Sin ellos,
// solo entran los PNG y sin tamaño. El de verdad, gc::exporter::image_codec().
struct ImageCodec {
    std::function<std::optional<std::string>(std::string_view data)> to_png;
    std::function<std::optional<std::pair<std::int64_t, std::int64_t>>(const std::filesystem::path& path)> size;
};

// images.import_image: los bytes de una imagen subida como PNG de trabajo en image/ (o, con
// `keep_gif` y datos de GIF, el GIF tal cual con sus metadatos de probe_gif). El nombre se limpia
// (unquote, sin carpeta, sin «?…» ni «#…»); sin extensión de imagen, la de la firma o «.png».
// Errores: «Archivo vacío.», «Formato no válido. Usa PNG, JPG, WebP o GIF.» y «No se pudo leer la
// imagen. Usa PNG, JPG, WebP o GIF.». El origen por defecto es «upload».
[[nodiscard]] Result<ImageInfo> import_image(ProjectsStore& store, std::string_view pid, std::string_view filename,
                                             std::string_view data, const ImageCodec& codec, IdSource& ids,
                                             const ImportMeta& meta = {}, bool keep_gif = false);
// Lee el archivo y import_image (el original se queda donde está). «Archivo vacío.» si no se
// puede leer o está vacío.
[[nodiscard]] Result<ImageInfo> import_image_file(ProjectsStore& store, std::string_view pid,
                                                  const std::filesystem::path& src, std::string_view filename,
                                                  const ImageCodec& codec, IdSource& ids,
                                                  const ImportMeta& meta = {}, bool keep_gif = false);

} // namespace gc::core
