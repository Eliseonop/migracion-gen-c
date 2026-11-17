#include "gc/export/freeze.hpp"

#include <algorithm>
#include <format>
#include <optional>
#include <utility>

#include "gc/core/io.hpp"
#include "gc/core/media_import.hpp"
#include "gc/export/image_import.hpp"
#include "gc/media/decode.hpp"
#include "gc/media/probe.hpp"
#include "gc/render/png.hpp"

namespace gc::exporter {

namespace {

// El primer fotograma (o, con `last`, el último) de lo que sale desde `ss`.
Result<std::optional<media::RgbaFrame>> grab(const std::filesystem::path& path, double ss, bool untagged, bool last) {
    const media::DecodeSpec spec{{"-ss", std::format("{:.3f}", std::max(0.0, ss)), "-i", core::path_to_utf8(path)},
                                 untagged ? media::kAssumeBt709Filter : ""};
    std::optional<media::RgbaFrame> out;
    auto n = media::decode_frames(spec, [&](const media::RgbaFrame& f) {
        out = f;
        return last;
    });
    if (!n) {
        return std::unexpected(n.error());
    }
    return out;
}

} // namespace

Result<std::string> extract_still(const std::filesystem::path& path, double at_time, bool untagged) {
    auto frame = grab(path, at_time, untagged, false);
    if (!frame || !*frame) {
        // Pasado el último fotograma, FFmpeg no devuelve nada: el último que haya (-sseof -0.2).
        frame = grab(path, media::probe_duration(path) - 0.2, untagged, true);
    }
    if (!frame || !*frame) {
        return fail(ErrorCode::InvalidArgument,
                    std::string("No se pudo extraer el fotograma: ") + (frame ? "" : frame.error().message));
    }
    media::RgbaFrame& f = **frame;
    for (std::size_t i = 3; i < f.pixels.size(); i += 4) {
        f.pixels[i] = 255; // format=rgb24: sin alfa
    }
    const render::Image image{f.width, f.height, std::move(f.pixels)};
    auto png = render::encode_png(image);
    if (!png) {
        return std::unexpected(png.error());
    }
    return std::string(png->begin(), png->end());
}

Result<core::ImageInfo> freeze_frame_image(core::ProjectsStore& store, std::string_view project_id,
                                           const core::TimelineClip& clip, double src_time,
                                           const core::MediaDirs& dirs, core::IdSource& ids) {
    if (clip.kind != "video") {
        return fail(ErrorCode::InvalidArgument, "Solo se puede congelar un fotograma de un clip de vídeo.");
    }
    auto project = store.get_project(project_id);
    if (!project) {
        return std::unexpected(project.error());
    }
    if (!*project) {
        return fail(ErrorCode::NotFound, "Proyecto no encontrado.");
    }
    const auto path = core::clip_media_path(**project, clip, dirs);
    std::error_code ec;
    if (!path || !std::filesystem::exists(*path, ec)) {
        return fail(ErrorCode::NotFound, "No se encuentra el archivo del clip.");
    }
    // t = min(max(src_time, 0), max(0, source_duration or src_time))
    const double cap = clip.source_duration != 0.0 ? clip.source_duration : src_time;
    const double t = std::min(std::max(src_time, 0.0), std::max(0.0, cap));
    auto data = extract_still(*path, t, media::color_untagged(*path));
    if (!data) {
        return std::unexpected(data.error());
    }
    const std::string& fname = clip.filename;
    const std::string stem = core::path_to_utf8(core::path_from_utf8(fname.empty() ? "clip" : fname).stem());
    const std::string name = std::format("{}_congelado_{:.2f}s.png", stem, t);
    core::ImportMeta meta;
    meta.label = std::format("{} · congelado {:.2f}s", clip.name.value_or("").empty() ? stem : *clip.name, t);
    meta.origin = "freeze";
    meta.source = "generated";
    return core::import_image(store, project_id, name, *data, image_codec(), ids, meta);
}

} // namespace gc::exporter
