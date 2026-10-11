#include "frame_cmd.hpp"

#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <filesystem>
#include <format>
#include <iostream>
#include <map>
#include <optional>
#include <string_view>
#include <vector>

#include "gc/core/clip_adjust.hpp"
#include "gc/core/clip_media.hpp"
#include "gc/core/collections.hpp"
#include "gc/core/frame.hpp"
#include "gc/core/io.hpp"
#include "gc/core/projects_store.hpp"
#include "gc/core/timeline_rules.hpp"
#include "gc/media/decode.hpp"
#include "gc/media/probe.hpp"
#include "gc/render/compose.hpp"
#include "gc/render/png.hpp"

namespace genclip {

namespace {

namespace fs = std::filesystem;
using namespace gc::core;

int fail_with(std::string_view message) {
    std::cerr << "error: " << message << '\n';
    return 1;
}

// Los clips cuyo material se busca en el disco. Las figuras pasan tal cual (unsupported_reason
// las rechaza) y los motion graphics, sin clip_media_path aún, van aparte.
bool has_media(const TimelineClip& clip) { return clip.kind == "video" || clip.kind == "image"; }

} // namespace

int cmd_frame(std::span<const std::string> args) {
    std::vector<std::string> pos;
    std::optional<double> at;
    std::optional<std::string> out;
    bool skip = false;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& a = args[i];
        if (a == "--at" && i + 1 < args.size()) {
            const std::string& v = args[++i];
            double t = 0.0;
            const auto [end, ec] = std::from_chars(v.data(), v.data() + v.size(), t);
            if (ec != std::errc{} || end != v.data() + v.size() || !std::isfinite(t)) {
                return fail_with(std::format("--at necesita un número de segundos, no «{}»", v));
            }
            at = t;
        } else if (a == "-o" && i + 1 < args.size()) {
            out = args[++i];
        } else if (a == "--omitir") {
            skip = true;
        } else {
            pos.push_back(a);
        }
    }
    if (pos.size() != 2 || !at || !out) {
        std::cerr << "uso: genclip frame <raíz de video-yt> <proyecto> --at <s> -o <salida.png> [--omitir]\n";
        return 2;
    }
    const fs::path root = path_from_utf8(pos[0]);
    // config.py: OUTPUT_DIR = <raíz>/clips, DATA_DIR = <raíz>/data y ASSETS_DIR = VIDEO_YT_ASSETS o
    // <raíz>/../assets; las colecciones, en `material_root` de los ajustes o <assets>/material.
    const char* assets_env = std::getenv("VIDEO_YT_ASSETS"); // NOLINT(concurrency-mt-unsafe): al arrancar
    const fs::path assets = assets_env != nullptr && *assets_env != '\0' ? path_from_utf8(assets_env)
                                                                         : root.parent_path() / "assets";
    const SettingsFile settings(root / "data" / "settings.json");
    const MediaDirs dirs{root / "clips", root / "data", collections_root(settings.load(), assets / "material")};
    const ProjectsStore store(dirs.data_dir / "projects.json", dirs.clips_dir);
    auto project = store.get_project(pos[1]);
    if (!project) {
        return fail_with(project.error().to_string());
    }
    if (!*project) {
        return fail_with(std::format("no existe el proyecto «{}»", pos[1]));
    }
    if (!(*project)->timeline) {
        return fail_with(std::format("el proyecto «{}» no tiene timeline", pos[1]));
    }
    Timeline timeline = *(*project)->timeline;

    // compose.py: W y H pares; `-ss T` a la salida da el fotograma compuesto más cercano a T.
    const int w = static_cast<int>(timeline.width - timeline.width % 2);
    const int h = static_cast<int>(timeline.height - timeline.height % 2);
    const int fps = timeline.fps > 0 ? static_cast<int>(timeline.fps) : 30;
    const std::int64_t tick = gc::media::export_frame_tick(*at, fps);
    const double t = static_cast<double>(tick) / fps;

    // Como el export: solo los clips con material que existe (media_exists de compose.py). Los
    // motion graphics aún no: si alguno se ve en t, error (o, con --omitir, aviso).
    std::map<std::string, fs::path> paths;
    std::optional<std::string> motion;
    std::erase_if(timeline.clips, [&](const gc::Cow<TimelineClip>& c) {
        const TimelineClip& clip = *c;
        if (clip.kind == "motion") {
            const double start = std::max(0.0, clip.start);
            if (!clip.disabled && t >= start && t < start + clip_timeline_duration(clip)) {
                motion = clip.id;
            }
            return true;
        }
        if (!has_media(clip)) {
            return false;
        }
        const auto path = clip_media_path(**project, clip, dirs);
        if (!path || !fs::is_regular_file(*path)) {
            std::cerr << std::format("aviso: sin material para el clip {} ({}): no se dibuja\n", clip.id, clip.filename);
            return true;
        }
        paths[clip.id] = *path;
        return false;
    });
    if (motion && !skip) {
        return fail_with(std::format("el render aún no dibuja motion graphics (clip {})", *motion));
    }
    if (motion) {
        std::cerr << std::format("aviso: el render aún no dibuja motion graphics (clip {}): se omite\n", *motion);
    }

    // El fotograma de cada clip en t, decodificado una vez (también da sus dimensiones). Las
    // pasadas fantasma (kGhostSuffix) usan el material de su clip.
    std::map<std::string, gc::render::Image> frames;
    std::optional<gc::Error> media_error;
    const auto frame_of = [&](const TimelineClip& clip) -> const gc::render::Image* {
        const std::string id =
            clip.id.ends_with(kGhostSuffix) ? clip.id.substr(0, clip.id.size() - kGhostSuffix.size()) : clip.id;
        if (auto it = frames.find(id); it != frames.end()) {
            return &it->second;
        }
        const auto p = paths.find(id);
        if (p == paths.end()) {
            return nullptr;
        }
        auto got = gc::media::export_frame_at(clip, p->second, fps, tick);
        if (!got || !got->has_value()) {
            media_error = got ? gc::Error{gc::ErrorCode::Io, std::format("el clip {} no da fotograma en {}", id, t)}
                              : got.error();
            return nullptr;
        }
        auto& f = **got;
        return &frames.emplace(id, gc::render::Image{f.width, f.height, std::move(f.pixels)}).first->second;
    };

    gc::media::silence_ffmpeg_log();
    auto layers = evaluate(timeline, t, FrameSpec{static_cast<double>(w), static_cast<double>(h), kRenderLayers},
                           [&](const TimelineClip& clip) -> std::optional<MediaSize> {
                               const auto* img = frame_of(clip);
                               if (img == nullptr) {
                                   return std::nullopt;
                               }
                               return MediaSize{static_cast<double>(img->width), static_cast<double>(img->height)};
                           });
    if (media_error) {
        return fail_with(media_error->to_string());
    }
    if (!layers) {
        return fail_with(layers.error().to_string());
    }
    if (skip) {
        std::erase_if(*layers, [](const FrameLayer& layer) {
            const auto reason = gc::render::unsupported_reason(layer);
            if (reason) {
                std::cerr << std::format("aviso: {} (clip {}): se omite\n", *reason, layer.clip->id);
            }
            return reason.has_value();
        });
    }
    auto image = gc::render::compose_frame(*layers, w, h,
                                           [&](const FrameLayer& layer) -> gc::Result<const gc::render::Image*> {
                                               const auto* img = frame_of(*layer.clip);
                                               if (img == nullptr) {
                                                   return gc::fail(gc::ErrorCode::NotFound,
                                                                   std::format("sin material para el clip {}",
                                                                               layer.clip->id));
                                               }
                                               return img;
                                           });
    if (!image) {
        return fail_with(image.error().to_string());
    }
    if (auto ok = gc::render::write_png(*image, path_from_utf8(*out)); !ok) {
        return fail_with(ok.error().to_string());
    }
    std::cout << std::format("{} ({}x{}, t = {:.3f} s, {} capas)\n", *out, w, h, t, layers->size());
    return 0;
}

} // namespace genclip
