#include "preview_source.hpp"

#include <algorithm>
#include <initializer_list>
#include <utility>

#ifdef GC_STUDIO_PREVIEW
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <optional>
#include <thread>

#include "gc/export/frame.hpp"
#include "gc/export/project.hpp"
#include "gc/media/decode.hpp"
#endif

namespace gc::studio {

#ifdef GC_STUDIO_PREVIEW

namespace {

// Los fotogramas ya compuestos que se guardan (ir y volver con el cabezal no los recompone): en
// vertical 1080 × 1920, unos 30.
constexpr std::size_t kCacheBytes = std::size_t{256} << 20U;

QImage to_qimage(const render::Image& image) {
    QImage out(image.width, image.height, QImage::Format_RGBA8888); // sin relleno: width · 4 por fila
    if (!out.isNull() && static_cast<std::size_t>(out.sizeInBytes()) == image.pixels.size()) {
        std::memcpy(out.bits(), image.pixels.data(), image.pixels.size());
    }
    return out;
}

} // namespace

struct PreviewSource::Worker {
    explicit Worker(PreviewSource* o) : owner(o) {
        thread = std::thread([this] { run(); });
    }
    Worker(const Worker&) = delete;
    Worker& operator=(const Worker&) = delete;
    Worker(Worker&&) = delete;
    Worker& operator=(Worker&&) = delete;
    ~Worker() {
        {
            const std::scoped_lock lock(mutex);
            stop = true;
        }
        wake.notify_all();
        thread.join();
    }

    PreviewSource* owner;
    std::mutex mutex;
    std::condition_variable wake;
    // Lo pendiente, bajo `mutex`: lo deja el hilo de la interfaz y lo recoge el del render.
    bool stop = false;
    bool closing = false;
    bool reload = false;
    std::filesystem::path root;
    std::string project_id;
    bool timeline_dirty = false;
    core::TimelineSnapshot timeline;
    std::optional<double> at;
    std::optional<double> scale;
    std::optional<PreviewProxies> proxies;
    std::thread thread; // el último: arranca con todo lo demás ya hecho

    [[nodiscard]] bool pending() const {
        return stop || closing || reload || timeline_dirty || at.has_value() || scale.has_value() ||
               proxies.has_value();
    }

    // En el hilo de la interfaz, cuando llegue (si el PreviewSource sigue vivo).
    void post(QImage image, qint64 tick, double s, QString error, QStringList warnings) const {
        PreviewSource* o = owner;
        QMetaObject::invokeMethod(
            o,
            [o, image = std::move(image), tick, s, error = std::move(error), warnings = std::move(warnings)] {
                o->publish(image, tick, s, error, warnings);
            },
            Qt::QueuedConnection);
    }

    void run() {
        QStringList warnings; // antes que `renderer`, que guarda una función que escribe aquí
        std::optional<exporter::LoadedProject> project;
        QString project_error;
        core::TimelineSnapshot current;
        std::optional<exporter::FrameRenderer> renderer;
        bool open_failed = false; // no reintentar hasta que cambie algo
        std::optional<double> want; // el último instante pedido
        std::int64_t shown = -1;    // el tick publicado con este `renderer`
        double current_scale = 1.0;
        exporter::ProxyMap current_proxies;
        for (;;) {
            bool close_now = false;
            bool reload_now = false;
            bool timeline_now = false;
            std::filesystem::path new_root;
            std::string new_id;
            core::TimelineSnapshot new_timeline;
            std::optional<double> new_at;
            std::optional<double> new_scale;
            std::optional<PreviewProxies> new_proxies;
            {
                std::unique_lock lock(mutex);
                wake.wait(lock, [&] { return pending(); });
                if (stop) {
                    return;
                }
                close_now = std::exchange(closing, false);
                reload_now = std::exchange(reload, false);
                timeline_now = std::exchange(timeline_dirty, false);
                new_root = std::move(root);
                new_id = std::move(project_id);
                new_timeline = std::move(timeline);
                new_at = std::exchange(at, std::nullopt);
                new_scale = std::exchange(scale, std::nullopt);
                new_proxies = std::exchange(proxies, std::nullopt);
            }
            const bool rescaled = new_scale && *new_scale != current_scale;
            if (rescaled) {
                current_scale = *new_scale;
            }
            if (new_proxies) {
                current_proxies.clear();
                for (const auto& [source, proxy] : *new_proxies) {
                    current_proxies.emplace(source, exporter::ProxyFile{proxy.file, proxy.source_width, proxy.source_height});
                }
            }
            const bool changed = close_now || reload_now || timeline_now || rescaled || new_proxies.has_value();
            if (changed) {
                renderer.reset();
                warnings.clear();
                open_failed = false;
                shown = -1;
            }
            if (close_now) {
                project.reset();
                current.reset();
                want.reset();
                post({}, -1, current_scale, {}, {});
            }
            if (reload_now) {
                auto loaded = exporter::load_project(new_root, new_id);
                project_error = loaded ? QString() : QString::fromStdString(loaded.error().message);
                project = loaded ? std::optional(std::move(*loaded)) : std::nullopt;
            }
            if (timeline_now) {
                current = std::move(new_timeline);
            }
            if (new_at) {
                want = new_at;
            }
            if (!want || !current || open_failed) {
                continue;
            }
            if (!project) {
                if (changed) {
                    post({}, -1, current_scale, project_error, {});
                }
                continue;
            }
            if (!renderer) {
                exporter::LoadedProject p = *project;
                p.timeline = *current;
                exporter::FrameOptions options;
                options.omit_unsupported = true; // el editor enseña lo que sabe dibujar
                options.cache_bytes = kCacheBytes;
                options.scale = current_scale;
                options.proxies = current_proxies;
                options.on_warning = [&warnings](const std::string& message) {
                    warnings << QString::fromStdString(message);
                };
                auto opened = exporter::FrameRenderer::open(p, options);
                if (!opened || opened->frames() <= 0) {
                    open_failed = true;
                    post({}, -1, current_scale,
                         opened ? QString("La timeline no tiene fotogramas.") : QString::fromStdString(opened.error().message),
                         warnings);
                    continue;
                }
                renderer.emplace(std::move(*opened));
            }
            const std::int64_t k =
                std::clamp<std::int64_t>(media::export_frame_tick(*want, renderer->fps()), 0, renderer->frames() - 1);
            if (k == shown) {
                continue;
            }
            auto frame = renderer->tick(k);
            if (!frame) {
                shown = -1;
                post({}, -1, current_scale, QString::fromStdString(frame.error().message), warnings);
                continue;
            }
            shown = k;
            post(to_qimage(frame->image), k, current_scale, {}, warnings);
        }
    }
};

PreviewSource::PreviewSource(QObject* parent) : QObject(parent), worker_(std::make_unique<Worker>(this)) {}

bool PreviewSource::available() const { return true; }

void PreviewSource::open(std::filesystem::path root, std::string project_id) {
    {
        const std::scoped_lock lock(worker_->mutex);
        worker_->reload = true;
        worker_->root = std::move(root);
        worker_->project_id = std::move(project_id);
    }
    worker_->wake.notify_one();
}

void PreviewSource::close() {
    sent_.reset();
    {
        const std::scoped_lock lock(worker_->mutex);
        // Lo que estuviera pendiente de antes ya no vale.
        worker_->closing = true;
        worker_->reload = false;
        worker_->timeline_dirty = false;
        worker_->timeline.reset();
        worker_->at.reset();
    }
    worker_->wake.notify_one();
}

void PreviewSource::setTimeline(core::TimelineSnapshot timeline) {
    if (timeline == sent_) {
        return; // la misma: no hace falta volver a preparar el render
    }
    sent_ = timeline;
    {
        const std::scoped_lock lock(worker_->mutex);
        worker_->timeline_dirty = true;
        worker_->timeline = std::move(timeline);
    }
    worker_->wake.notify_one();
    update_scale(); // otro tamaño del cuadro, quizá otra escala
}

void PreviewSource::setDisplaySize(int width, int height) {
    display_w_ = width;
    display_h_ = height;
    update_scale();
}

void PreviewSource::setProxies(PreviewProxies proxies) {
    {
        const std::scoped_lock lock(worker_->mutex);
        worker_->proxies = std::move(proxies);
    }
    worker_->wake.notify_one();
}

void PreviewSource::update_scale() {
    if (!sent_) {
        return;
    }
    // Las dimensiones de la salida, pares como el export.
    const auto w = static_cast<double>(sent_->width - sent_->width % 2);
    const auto h = static_cast<double>(sent_->height - sent_->height % 2);
    const double s = scale_for(w, h, display_w_, display_h_);
    if (s == sent_scale_) {
        return;
    }
    sent_scale_ = s;
    {
        const std::scoped_lock lock(worker_->mutex);
        worker_->scale = s;
    }
    worker_->wake.notify_one();
}

void PreviewSource::request(double seconds) {
    {
        const std::scoped_lock lock(worker_->mutex);
        worker_->at = seconds;
    }
    worker_->wake.notify_one();
}

#else // sin gc_export: no hay con qué componer

struct PreviewSource::Worker {};

PreviewSource::PreviewSource(QObject* parent) : QObject(parent) {}

bool PreviewSource::available() const { return false; }

void PreviewSource::open(std::filesystem::path /*root*/, std::string /*project_id*/) {}
void PreviewSource::close() {}
void PreviewSource::setTimeline(core::TimelineSnapshot /*timeline*/) {}
void PreviewSource::request(double /*seconds*/) {}
void PreviewSource::setDisplaySize(int width, int height) {
    display_w_ = width;
    display_h_ = height;
}
void PreviewSource::update_scale() {}
void PreviewSource::setProxies(PreviewProxies /*proxies*/) {}

#endif

PreviewSource::~PreviewSource() = default;

double PreviewSource::scale_for(double frame_w, double frame_h, int display_w, int display_h) {
    if (!(frame_w > 0 && frame_h > 0) || display_w <= 0 || display_h <= 0) {
        return 1.0;
    }
    // Lo que hace falta para llenar el recuadro (con un pelo de holgura por el redondeo).
    const double need = std::max(display_w / frame_w, display_h / frame_h) * 0.999;
    for (const double s : {0.25, 0.5}) {
        if (s >= need) {
            return s;
        }
    }
    return 1.0;
}

void PreviewSource::publish(const QImage& image, qint64 tick, double scale, const QString& error,
                            const QStringList& warnings) {
    image_ = image;
    tick_ = image.isNull() ? -1 : tick;
    scale_ = scale;
    error_ = error;
    warnings_ = warnings;
    ++revision_;
    emit frameChanged();
}

} // namespace gc::studio
