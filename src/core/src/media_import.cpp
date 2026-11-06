#include "gc/core/media_import.hpp"

#include <algorithm>
#include <fstream>
#include <functional>
#include <span>
#include <system_error>

#include "gc/core/image_probe.hpp"
#include "gc/core/io.hpp"

#include "py.hpp"
#include "py_url.hpp"

namespace gc::core {

namespace {

namespace fs = std::filesystem;

constexpr std::array<std::string_view, 11> kImageExts{".png", ".jpg",  ".jpeg", ".webp", ".gif", ".bmp",
                                                      ".tif", ".tiff", ".avif", ".heic", ".heif"};

template <std::size_t N>
bool has(const std::array<std::string_view, N>& set, std::string_view v) {
    return std::ranges::find(set, v) != set.end();
}

// Path(filename).suffix.lower()
std::string lower_suffix(std::string_view filename) {
    return detail::py_lower(detail::py_path_suffix(filename));
}

// `x or y` de Python con textos: el vacío no cuenta.
std::string or_text(const std::optional<std::string>& v, std::string_view fallback) {
    return v && !v->empty() ? *v : std::string(fallback);
}

// _next_index: el siguiente a max(los index del material, 99999).
std::int64_t next_index(const Project& project) {
    std::int64_t top = 99999;
    for (const ClipInfo& c : project.clips) {
        top = std::max(top, c.index);
    }
    return top + 1;
}

// Lo que comparten import_video e import_audio hasta tener el archivo en su sitio: la carpeta, el
// id y el nombre, y escribirlo. `kind` es la subcarpeta (video o audio).
struct Placed {
    Project project;
    std::string ident;
    std::string dest_name;
    double duration = 0;
};

using Writer = std::function<Result<void>(const fs::path& dest)>;

Result<Placed> place(const ProjectsStore& store,std::string_view pid, std::string_view filename, std::string_view kind,
                     std::string_view ext, const Writer& write, const DurationProbe& probe, IdSource& ids,
                     std::string_view save_error) {
    auto found = store.get_project(pid);
    if (!found) {
        return std::unexpected(found.error());
    }
    if (!*found) {
        return fail(ErrorCode::NotFound, "Proyecto no encontrado.");
    }
    Placed out{std::move(**found), {}, {}, 0};
    if (auto dirs = ensure_project_dirs(project_base(store.clips_dir(), out.project)); !dirs) {
        return std::unexpected(dirs.error());
    }
    out.ident = ids.hex32().substr(0, 12);
    out.dest_name = safe_file_name(detail::py_path_stem(filename)) + "_" + out.ident + std::string(ext);
    const auto dest = resolve_project_media(store.clips_dir(), out.project, kind, out.dest_name);
    if (!dest) {
        return fail(ErrorCode::InvalidArgument, std::string(save_error));
    }
    if (auto written = write(*dest); !written) {
        return std::unexpected(written.error());
    }
    // float(probe_duration(dest)), y 0 si falla.
    out.duration = probe ? probe(*dest).value_or(0.0) : 0.0;
    return out;
}

Writer bytes_writer(std::string_view data) {
    return [data](const fs::path& dest) -> Result<void> {
        std::ofstream f(dest, std::ios::binary | std::ios::trunc);
        f.write(data.data(), static_cast<std::streamsize>(data.size()));
        if (!f.flush()) {
            return fail(ErrorCode::Io, "no se puede escribir " + path_to_utf8(dest));
        }
        return {};
    };
}

Writer copy_writer(const fs::path& src) {
    return [src](const fs::path& dest) -> Result<void> {
        std::error_code ec;
        fs::copy_file(src, dest, fs::copy_options::overwrite_existing, ec);
        if (ec) {
            return fail(ErrorCode::Io, "no se puede copiar " + path_to_utf8(src) + ": " + ec.message());
        }
        return {};
    };
}

bool empty_file(const fs::path& src) {
    std::error_code ec;
    return !fs::is_regular_file(src, ec) || fs::file_size(src, ec) == 0 || ec;
}

Result<ClipInfo> add_video(ProjectsStore& store, std::string_view pid, std::string_view filename, const Writer& write,
                           const DurationProbe& probe, IdSource& ids, const ImportMeta& meta) {
    const std::string ext = lower_suffix(filename);
    auto placed = place(store, pid, filename, "video", ext, write, probe, ids, "No se pudo guardar el vídeo.");
    if (!placed) {
        return std::unexpected(placed.error());
    }
    ClipInfo info;
    info.index = next_index(placed->project);
    info.id = placed->ident;
    info.filename = placed->dest_name;
    info.url = "/api/media/" + placed->project.id + "/video/" + detail::py_quote(placed->dest_name);
    info.start = 0.0;
    info.end = placed->duration;
    info.source_url = meta.source_url;
    info.label = or_text(meta.label, detail::py_path_stem(filename));
    info.description = meta.description;
    info.origin = or_text(meta.origin, "import");
    info.source = or_text(meta.source, "external");
    info.provider = meta.provider;
    info.external_id = meta.external_id;
    info.author = meta.author;
    info.license_info = meta.license_info;
    if (auto added = store.add_clips(pid, {info}, ids); !added) {
        return std::unexpected(added.error());
    }
    return info;
}

Result<AudioInfo> add_audio(ProjectsStore& store, std::string_view pid, std::string_view filename,
                            const Writer& write, const DurationProbe& probe, IdSource& ids) {
    const std::string ext = lower_suffix(filename);
    auto placed = place(store, pid, filename, "audio", ext, write, probe, ids, "No se pudo guardar el audio.");
    if (!placed) {
        return std::unexpected(placed.error());
    }
    AudioInfo info;
    info.id = placed->ident;
    info.filename = placed->dest_name;
    info.url = "/api/media/" + placed->project.id + "/audio/" + detail::py_quote(placed->dest_name);
    info.duration = placed->duration;
    info.label = detail::py_path_stem(filename);
    info.origin = "import";
    info.source = "external";
    if (auto added = store.add_audio(pid, info); !added) {
        return std::unexpected(added.error());
    }
    return info;
}

constexpr std::string_view kBadVideo = "Formato de vídeo no válido. Usa MP4, MOV, MKV, WEBM…";
constexpr std::string_view kBadAudio = "Formato de audio no válido. Usa MP3, WAV, M4A…";
constexpr std::string_view kEmpty = "Archivo vacío.";

} // namespace

Result<ClipInfo> import_video(ProjectsStore& store, std::string_view pid, std::string_view filename,
                              std::string_view data, const DurationProbe& probe, IdSource& ids,
                              const ImportMeta& meta) {
    if (!has(kImportVideoExts, lower_suffix(filename))) {
        return fail(ErrorCode::InvalidArgument, std::string(kBadVideo));
    }
    if (data.empty()) {
        return fail(ErrorCode::InvalidArgument, std::string(kEmpty));
    }
    return add_video(store, pid, filename, bytes_writer(data), probe, ids, meta);
}

Result<ClipInfo> import_video_file(ProjectsStore& store, std::string_view pid, const std::filesystem::path& src,
                                   std::string_view filename, const DurationProbe& probe, IdSource& ids,
                                   const ImportMeta& meta) {
    if (!has(kImportVideoExts, lower_suffix(filename))) {
        return fail(ErrorCode::InvalidArgument, std::string(kBadVideo));
    }
    if (empty_file(src)) {
        return fail(ErrorCode::InvalidArgument, std::string(kEmpty));
    }
    return add_video(store, pid, filename, copy_writer(src), probe, ids, meta);
}

Result<AudioInfo> import_audio(ProjectsStore& store, std::string_view pid, std::string_view filename,
                               std::string_view data, const DurationProbe& probe, IdSource& ids) {
    if (!has(kImportAudioExts, lower_suffix(filename))) {
        return fail(ErrorCode::InvalidArgument, std::string(kBadAudio));
    }
    if (data.empty()) {
        return fail(ErrorCode::InvalidArgument, std::string(kEmpty));
    }
    return add_audio(store, pid, filename, bytes_writer(data), probe, ids);
}

Result<AudioInfo> import_audio_file(ProjectsStore& store, std::string_view pid, const std::filesystem::path& src,
                                    std::string_view filename, const DurationProbe& probe, IdSource& ids) {
    if (!has(kImportAudioExts, lower_suffix(filename))) {
        return fail(ErrorCode::InvalidArgument, std::string(kBadAudio));
    }
    if (empty_file(src)) {
        return fail(ErrorCode::InvalidArgument, std::string(kEmpty));
    }
    return add_audio(store, pid, filename, copy_writer(src), probe, ids);
}

namespace {

constexpr std::string_view kPngMagic = "\x89PNG\r\n\x1a\n";
constexpr std::string_view kBadImage = "Formato no válido. Usa PNG, JPG, WebP o GIF.";
constexpr std::string_view kUnreadable = "No se pudo leer la imagen. Usa PNG, JPG, WebP o GIF.";

std::span<const unsigned char> bytes_of(std::string_view data) {
    return {reinterpret_cast<const unsigned char*>(data.data()), data.size()}; // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast): los mismos bytes
}

// _clean_filename: Path(unquote(filename)).name, sin «?…» ni «#…» y con strip(); «imagen.png» si
// queda vacío.
std::string clean_filename(std::string_view filename) {
    std::string name = detail::py_path_name(detail::py_unquote(filename));
    name = name.substr(0, name.find('?'));
    name = name.substr(0, name.find('#'));
    name = detail::py_strip(name);
    return name.empty() ? std::string("imagen.png") : name;
}

// `Path(name).stem or 'imagen'`
std::string stem_or_image(std::string_view name) {
    const std::string stem = detail::py_path_stem(name);
    return stem.empty() ? std::string("imagen") : stem;
}

} // namespace

Result<ImageInfo> import_image(ProjectsStore& store, std::string_view pid, std::string_view filename,
                               std::string_view data, const ImageCodec& codec, IdSource& ids, const ImportMeta& meta,
                               bool keep_gif) {
    if (data.empty()) {
        return fail(ErrorCode::InvalidArgument, std::string(kEmpty));
    }
    std::string name = clean_filename(filename);
    const std::string ext = lower_suffix(name);
    if (!has(kImageExts, ext)) {
        // (El `ext = guessed` de Python no cuenta: después ya no se mira, el archivo es .png o .gif. Y
        // del nombre solo se usa la raíz, que no cambia con la extensión adivinada.)
        const std::string_view guessed = image_ext_from_magic(bytes_of(data));
        name = stem_or_image(name) + (guessed.empty() ? std::string(".png") : std::string(guessed));
        if (!ext.empty() && guessed.empty()) { // una extensión que no es de imagen ni se adivina
            return fail(ErrorCode::InvalidArgument, std::string(kBadImage));
        }
    }
    const bool is_gif = data.starts_with("GIF87a") || data.starts_with("GIF89a");
    auto found = store.get_project(pid);
    if (!found) {
        return std::unexpected(found.error());
    }
    if (!*found) {
        return fail(ErrorCode::NotFound, "Proyecto no encontrado.");
    }
    const Project& project = **found;
    if (auto dirs = ensure_project_dirs(project_base(store.clips_dir(), project)); !dirs) {
        return std::unexpected(dirs.error());
    }
    const std::string ident = ids.hex32().substr(0, 12);
    const std::string stem = safe_file_name(detail::py_path_stem(name));
    std::optional<GifInfo> gif;
    std::string payload;
    std::string dest_name;
    if (keep_gif && is_gif) {
        dest_name = stem + "_" + ident + ".gif";
        payload = std::string(data);
        gif = probe_gif(bytes_of(data));
    } else {
        // to_working_png: un PNG no se vuelve a codificar (conserva el alfa).
        std::optional<std::string> png = data.starts_with(kPngMagic) ? std::optional(std::string(data))
                                          : codec.to_png                ? codec.to_png(data)
                                                                        : std::nullopt;
        if (!png) {
            return fail(ErrorCode::InvalidArgument, std::string(kUnreadable));
        }
        payload = std::move(*png);
        dest_name = stem + "_" + ident + ".png";
    }
    const auto dest = resolve_project_media(store.clips_dir(), project, "image", dest_name);
    if (!dest) {
        return fail(ErrorCode::InvalidArgument, "No se pudo guardar la imagen.");
    }
    if (auto written = bytes_writer(payload)(*dest); !written) {
        return std::unexpected(written.error());
    }
    std::optional<std::int64_t> w;
    std::optional<std::int64_t> h;
    if (codec.size) {
        if (const auto dims = codec.size(*dest)) {
            w = dims->first;
            h = dims->second;
        }
    }
    if (gif) { // `w or gif_meta.get("width")`: también si probe_size dio 0
        if (!w || *w == 0) {
            w = gif->width;
        }
        if (!h || *h == 0) {
            h = gif->height;
        }
    }
    ImageInfo info;
    info.id = ident;
    info.filename = dest_name;
    info.url = "/api/media/" + project.id + "/image/" + detail::py_quote(dest_name);
    info.width = w;
    info.height = h;
    const std::string name_stem = detail::py_path_stem(name);
    info.label = meta.label && !meta.label->empty() ? meta.label
                 : !name_stem.empty()               ? std::optional(name_stem)
                                                    : std::nullopt;
    info.description = meta.description && !meta.description->empty() ? meta.description : std::nullopt;
    info.origin = or_text(meta.origin, "upload");
    info.source = or_text(meta.source, "external");
    info.provider = meta.provider;
    info.external_id = meta.external_id;
    info.source_url = meta.source_url;
    info.author = meta.author;
    info.license_info = meta.license_info;
    if (gif) {
        info.animated = gif->animated;
        info.frames = gif->frames;
        info.fps = gif->fps;
        info.duration = gif->duration;
        info.loop = gif->loop;
        info.has_alpha = gif->has_alpha;
    }
    if (auto added = store.add_image(pid, info); !added) {
        return std::unexpected(added.error());
    }
    return info;
}

Result<ImageInfo> import_image_file(ProjectsStore& store, std::string_view pid, const std::filesystem::path& src,
                                    std::string_view filename, const ImageCodec& codec, IdSource& ids,
                                    const ImportMeta& meta, bool keep_gif) {
    if (empty_file(src)) {
        return fail(ErrorCode::InvalidArgument, std::string(kEmpty));
    }
    std::error_code ec;
    const auto bytes = fs::file_size(src, ec);
    std::string data(ec ? 0 : static_cast<std::size_t>(bytes), '\0');
    std::ifstream f(src, std::ios::binary);
    if (!f.read(data.data(), static_cast<std::streamsize>(data.size()))) {
        data.clear();
    }
    if (data.empty()) {
        return fail(ErrorCode::InvalidArgument, std::string(kEmpty));
    }
    return import_image(store, pid, filename, data, codec, ids, meta, keep_gif);
}

std::optional<std::string_view> import_kind(std::string_view filename) {
    const std::string ext = lower_suffix(filename);
    if (has(kImportVideoExts, ext)) {
        return "video";
    }
    if (has(kImportAudioExts, ext)) {
        return "audio";
    }
    if (has(kImageExts, ext)) {
        return "image";
    }
    return std::nullopt;
}

} // namespace gc::core
