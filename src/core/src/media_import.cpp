#include "gc/core/media_import.hpp"

#include <algorithm>
#include <fstream>
#include <functional>
#include <system_error>

#include "gc/core/io.hpp"

#include "py.hpp"

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
