#include "gc/export/frame.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "gc/core/export_input.hpp"
#include "gc/core/font_metrics.hpp"
#include "gc/core/frame.hpp"
#include "gc/core/lru_bytes.hpp"
#include "gc/core/timeline_rules.hpp"
#include "gc/media/decode.hpp"
#include "gc/render/compose.hpp"
#include "gc/render/text.hpp"
#include "prepare.hpp"

namespace gc::exporter {

using namespace gc::core;

struct FrameRenderer::Impl {
    Impl(detail::Prepared p, render::Canvas c, render::TextFonts f, std::size_t cache_bytes) :
        prepared(std::move(p)), canvas(std::move(c)), fonts(std::move(f)), cache(cache_bytes) {}

    detail::Prepared prepared;
    render::Canvas canvas;
    render::TextFonts fonts;
    core::LruBytes<std::int64_t, Frame> cache; // los fotogramas ya compuestos, por tick
    std::int64_t cache_hits = 0;
    std::map<std::string, media::ClipFrames> readers; // los del fotograma anterior
    std::set<std::string> warned;
    std::function<void(const std::string&)> on_warning;
    bool skip = false;
    bool hwaccel = false;
    int width = 0;
    int height = 0;
    int fps = 0;
    double duration = 0;
    std::int64_t frames = 0;

    void warn(const std::string& message) {
        if (warned.insert(message).second && on_warning) {
            on_warning(message);
        }
    }
};

FrameRenderer::FrameRenderer(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
FrameRenderer::FrameRenderer(FrameRenderer&&) noexcept = default;
FrameRenderer& FrameRenderer::operator=(FrameRenderer&&) noexcept = default;
FrameRenderer::~FrameRenderer() = default;

int FrameRenderer::width() const noexcept { return impl_->width; }
int FrameRenderer::height() const noexcept { return impl_->height; }
int FrameRenderer::fps() const noexcept { return impl_->fps; }
double FrameRenderer::duration() const noexcept { return impl_->duration; }
std::int64_t FrameRenderer::frames() const noexcept { return impl_->frames; }
std::int64_t FrameRenderer::cache_hits() const noexcept { return impl_->cache_hits; }
std::size_t FrameRenderer::cache_bytes() const noexcept { return impl_->cache.bytes(); }

Result<FrameRenderer> FrameRenderer::open(const LoadedProject& project, const FrameOptions& options) {
    const Timeline& full = project.timeline;
    // compose.py: W y H pares.
    const int w = static_cast<int>(full.width - full.width % 2);
    const int h = static_cast<int>(full.height - full.height % 2);
    const int fps = full.fps > 0 ? static_cast<int>(full.fps) : 30;
    const double total = export_total_duration(full);
    if (total <= 0) {
        return fail(ErrorCode::InvalidArgument, "la timeline está vacía: añade al menos un clip");
    }
    std::set<std::string> warned;
    const auto warn = [&](const std::string& message) {
        if (warned.insert(message).second && options.on_warning) {
            options.on_warning(message);
        }
    };
    auto prepared = detail::prepare(project, full, w, h, fps, options.omit_unsupported, warn);
    if (!prepared) {
        return std::unexpected(prepared.error());
    }
    auto canvas = render::Canvas::make(w, h);
    if (!canvas) {
        return std::unexpected(canvas.error());
    }
    // Las fuentes del sistema y las de video-yt (backend/app/fonts: Anton), para las máscaras de
    // texto y los textos.
    auto impl = std::make_unique<Impl>(std::move(*prepared), std::move(*canvas),
                                       render::TextFonts(FontDirs{default_system_font_dir(), project.root / "app" / "fonts"}),
                                       options.cache_bytes);
    impl->warned = std::move(warned);
    impl->on_warning = options.on_warning;
    impl->skip = options.omit_unsupported;
    impl->hwaccel = options.hwaccel;
    impl->width = w;
    impl->height = h;
    impl->fps = fps;
    impl->duration = total;
    // El fondo de render_frame dura `total`: un fotograma en round(total·F) o después no existe.
    impl->frames = static_cast<std::int64_t>(std::round(total * fps));
    return FrameRenderer(std::move(impl));
}

Result<Frame> FrameRenderer::at(double seconds) { return tick(media::export_frame_tick(seconds, impl_->fps)); }

Result<Frame> FrameRenderer::tick(std::int64_t k) {
    Impl& m = *impl_;
    const int fps = m.fps;
    const double t = static_cast<double>(k) / fps;
    if (k < 0 || k >= m.frames) {
        return fail(ErrorCode::InvalidArgument,
                    std::format("el instante {:.3f} s queda fuera de la timeline ({:.3f} s)", t, m.duration));
    }
    if (const Frame* cached = m.cache.get(k); cached != nullptr) {
        ++m.cache_hits;
        return *cached; // sus avisos ya salieron la primera vez
    }
    // Los motion graphics que se ven en t: un error (o, con omit_unsupported, un aviso).
    for (const TimelineClip& clip : m.prepared.motion) {
        const double start = std::max(0.0, clip.start);
        if (t >= start && t < start + clip_timeline_duration(clip)) {
            const std::string msg = std::format("el render aún no dibuja motion graphics (clip {})", clip.id);
            if (!m.skip) {
                return fail(ErrorCode::Unsupported, msg);
            }
            m.warn(msg + ": se omite");
        }
    }

    // El fotograma de cada clip en t, decodificado una vez (también da sus dimensiones). Las
    // pasadas fantasma (kGhostSuffix) usan el material de su clip.
    std::map<std::string, render::Image> images;
    std::optional<Error> media_error;
    const auto frame_of = [&](const TimelineClip& clip) -> const render::Image* {
        const std::string id = detail::material_id(clip.id);
        if (auto it = images.find(id); it != images.end()) {
            return &it->second;
        }
        if (!m.prepared.paths.contains(id)) {
            return nullptr;
        }
        auto r = m.readers.find(id);
        if (r == m.readers.end()) {
            auto opened = detail::open_reader(m.prepared, id, fps, m.hwaccel);
            if (!opened) {
                media_error = opened.error();
                return nullptr;
            }
            r = m.readers.emplace(id, std::move(*opened)).first;
        }
        auto img = detail::read_image(m.prepared, id, r->second, k, fps);
        if (!img) {
            media_error = img.error();
            return nullptr;
        }
        return &images.emplace(id, std::move(*img)).first->second;
    };

    const Timeline& timeline = m.prepared.timeline;
    auto layers = evaluate(timeline, t, FrameSpec{static_cast<double>(m.width), static_cast<double>(m.height), kRenderLayers},
                           [&](const TimelineClip& clip) -> std::optional<MediaSize> {
                               const auto* img = frame_of(clip);
                               if (img == nullptr) {
                                   return std::nullopt;
                               }
                               return MediaSize{static_cast<double>(img->width), static_cast<double>(img->height)};
                           });
    if (media_error) {
        return std::unexpected(*media_error);
    }
    if (!layers) {
        return std::unexpected(layers.error());
    }
    // Los textos no son capas del compuesto: van encima, como el .ass global del export.
    std::erase_if(*layers, [](const FrameLayer& layer) { return layer.clip->kind == "text"; });
    if (m.skip) {
        std::erase_if(*layers, [&m](const FrameLayer& layer) {
            const auto reason = render::unsupported_reason(layer, &m.fonts);
            if (reason) {
                m.warn(std::format("{} (clip {}): se omite", *reason, layer.clip->id));
            }
            return reason.has_value();
        });
    }
    // Las capas, con los textos de las pistas con vídeo encima en su sitio, y el resto encima.
    std::vector<std::string> warnings;
    if (auto ok = render::compose_with_buried_texts(
            m.canvas, timeline, *layers, k,
            [&](const FrameLayer& layer) -> Result<const render::Image*> {
                const auto* img = frame_of(*layer.clip);
                if (img == nullptr) {
                    return fail(ErrorCode::NotFound, media_error ? media_error->to_string()
                                                                 : std::format("sin material para el clip {}", layer.clip->id));
                }
                return img;
            },
            m.fonts, m.skip, &warnings);
        !ok) {
        return std::unexpected(ok.error());
    }
    if (auto ok = render::draw_timeline_texts(m.canvas, timeline, k, m.fonts, m.skip, &warnings); !ok) {
        return std::unexpected(ok.error());
    }
    for (const auto& message : warnings) {
        m.warn(message + ": se omite");
    }
    // Fuera los lectores de los clips que no salen en este fotograma (cada uno guarda un
    // decodificador). Si vuelve a verse, su lector se abre de nuevo.
    std::erase_if(m.readers, [&](const auto& reader) { return !images.contains(reader.first); });
    Frame frame{m.canvas.snapshot(), k, t, layers->size()};
    if (m.cache.max_bytes() > 0) {
        m.cache.set(k, frame, frame.image.pixels.size());
    }
    return frame;
}

} // namespace gc::exporter
