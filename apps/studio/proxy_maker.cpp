#include "proxy_maker.hpp"

#include <algorithm>
#include <utility>

#ifdef GC_STUDIO_PREVIEW
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <set>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "gc/core/io.hpp"
#include "gc/export/project.hpp"
#include "gc/export/proxy.hpp"
#endif

namespace gc::studio {

#ifdef GC_STUDIO_PREVIEW

struct ProxyMaker::Worker {
    explicit Worker(ProxyMaker* o) : owner(o) {
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

    ProxyMaker* owner;
    // Lo pendiente, bajo `mutex`.
    std::mutex mutex;
    std::condition_variable wake;
    bool stop = false;
    bool closing = false;
    bool reload = false;
    std::filesystem::path root;
    std::string project_id;
    std::filesystem::path cache_dir;
    bool timeline_dirty = false;
    core::TimelineSnapshot timeline;
    std::thread thread; // el último: arranca con todo lo demás ya hecho

    // Cerrar, abrir otro proyecto o salir cortan el proxy que se está haciendo.
    [[nodiscard]] bool interrupted() {
        const std::scoped_lock lock(mutex);
        return stop || closing || reload;
    }

    void post(PreviewProxies proxies, QString status) const {
        ProxyMaker* o = owner;
        // En un shared_ptr: mover un std::map con la STL de MSVC reserva memoria (y puede lanzar).
        auto shared = std::make_shared<const PreviewProxies>(std::move(proxies));
        QMetaObject::invokeMethod(
            o, [o, shared = std::move(shared), status = std::move(status)] { o->publish(*shared, status); },
            Qt::QueuedConnection);
    }

    void run() {
#ifdef _WIN32
        // Por debajo de la interfaz y de la vista previa: es trabajo de fondo.
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#endif
        std::optional<exporter::LoadedProject> project;
        core::TimelineSnapshot current;
        std::filesystem::path cache;
        for (;;) {
            bool close_now = false;
            bool reload_now = false;
            std::filesystem::path new_root;
            std::string new_id;
            core::TimelineSnapshot new_timeline;
            bool timeline_now = false;
            {
                std::unique_lock lock(mutex);
                wake.wait(lock, [&] { return stop || closing || reload || timeline_dirty; });
                if (stop) {
                    return;
                }
                close_now = std::exchange(closing, false);
                reload_now = std::exchange(reload, false);
                new_root = std::move(root);
                new_id = std::move(project_id);
                timeline_now = std::exchange(timeline_dirty, false);
                new_timeline = std::move(timeline);
                cache = cache_dir;
            }
            if (close_now) {
                project.reset();
                current.reset();
                post({}, {});
            }
            if (reload_now) {
                auto loaded = exporter::load_project(new_root, new_id);
                project = loaded ? std::optional(std::move(*loaded)) : std::nullopt;
            }
            if (timeline_now) {
                current = std::move(new_timeline);
            }
            if (project && current && !cache.empty()) {
                make_all(*project, *current, cache, reload_now);
            }
        }
    }

    // Los vídeos de la timeline (sin repetir, en su orden) con su material en el disco.
    static std::vector<std::filesystem::path> videos(const exporter::LoadedProject& project, const core::Timeline& tl) {
        std::vector<std::filesystem::path> out;
        for (const auto& c : tl.clips) {
            if (c->kind != "video") {
                continue;
            }
            const auto path = exporter::clip_path(project, *c);
            std::error_code ec;
            if (path && std::filesystem::is_regular_file(*path, ec) && std::ranges::find(out, *path) == out.end()) {
                out.push_back(*path);
            }
        }
        return out;
    }

    // La caché, por debajo de su tope (prune_proxies), sin tocar los proxies de este proyecto.
    static void prune(const std::filesystem::path& cache, const PreviewProxies& made) {
        std::set<std::filesystem::path> keep;
        for (const auto& [source, proxy] : made) {
            keep.insert(proxy.file);
        }
        (void)exporter::prune_proxies(cache, exporter::kProxyCacheMaxBytes, keep);
    }

    // Los proxies de la timeline: los que ya hay y, uno a uno, los que faltan. Al abrir el proyecto
    // (`opened`), y siempre que se haga alguno nuevo, se limpia la caché.
    void make_all(const exporter::LoadedProject& project, const core::Timeline& tl, const std::filesystem::path& cache,
                  bool opened) {
        const int fps = tl.fps > 0 ? static_cast<int>(tl.fps) : 30;
        PreviewProxies made;
        std::vector<std::filesystem::path> todo;
        for (const auto& source : videos(project, tl)) {
            const exporter::ProxyLookup found = exporter::find_proxy(cache, source, fps);
            if (found.state == exporter::ProxyState::Ready) {
                made[source] = {found.file.path, found.file.source_width, found.file.source_height};
            } else if (found.state == exporter::ProxyState::Missing) {
                todo.push_back(source);
            }
        }
        const auto status = [&](std::size_t i, double part) {
            return QString("Preparando proxies: %1 de %2 (%3 %)")
                .arg(i + 1)
                .arg(todo.size())
                .arg(static_cast<int>(std::lround(part * 100)));
        };
        post(made, todo.empty() ? QString() : status(0, 0.0));
        if (opened) { // tras buscarlos: los de este proyecto son los usados más hace menos
            prune(cache, made);
        }
        QString failed;
        for (std::size_t i = 0; i < todo.size(); ++i) {
            auto last = std::chrono::steady_clock::now();
            auto r = exporter::make_proxy(cache, todo[i], fps, [&](double part) {
                if (interrupted()) {
                    return false;
                }
                if (const auto now = std::chrono::steady_clock::now(); now - last > std::chrono::milliseconds(250)) {
                    last = now;
                    post(made, status(i, part));
                }
                return true;
            });
            if (interrupted()) {
                return; // el bucle de run() atiende lo que ha llegado
            }
            if (!r) {
                failed = QString("No se pudo hacer el proxy de %1: %2")
                             .arg(QString::fromStdString(core::path_to_utf8(todo[i].filename())),
                                  QString::fromStdString(r.error().message));
            } else if (r->state == exporter::ProxyState::Ready) {
                made[todo[i]] = {r->file.path, r->file.source_width, r->file.source_height};
            }
            post(made, i + 1 < todo.size() ? status(i + 1, 0.0) : failed);
        }
        if (!todo.empty()) {
            prune(cache, made);
        }
    }
};

ProxyMaker::ProxyMaker(QObject* parent) : QObject(parent), worker_(std::make_unique<Worker>(this)) {}

void ProxyMaker::setCacheDir(std::filesystem::path dir) {
    cache_dir_ = dir;
    {
        const std::scoped_lock lock(worker_->mutex);
        worker_->cache_dir = std::move(dir);
    }
}

void ProxyMaker::open(std::filesystem::path root, std::string project_id) {
    sent_.reset();
    {
        const std::scoped_lock lock(worker_->mutex);
        worker_->reload = true;
        worker_->root = std::move(root);
        worker_->project_id = std::move(project_id);
    }
    worker_->wake.notify_one();
}

void ProxyMaker::close() {
    sent_.reset();
    {
        const std::scoped_lock lock(worker_->mutex);
        worker_->closing = true;
        worker_->reload = false;
        worker_->timeline_dirty = false;
        worker_->timeline.reset();
    }
    worker_->wake.notify_one();
}

void ProxyMaker::setTimeline(core::TimelineSnapshot timeline) {
    if (timeline == sent_) {
        return;
    }
    sent_ = timeline;
    {
        const std::scoped_lock lock(worker_->mutex);
        worker_->timeline_dirty = true;
        worker_->timeline = std::move(timeline);
    }
    worker_->wake.notify_one();
}

#else // sin gc_export: sin proxies

struct ProxyMaker::Worker {};

ProxyMaker::ProxyMaker(QObject* parent) : QObject(parent) {}
void ProxyMaker::setCacheDir(std::filesystem::path dir) { cache_dir_ = std::move(dir); }
void ProxyMaker::open(std::filesystem::path /*root*/, std::string /*project_id*/) {}
void ProxyMaker::close() {}
void ProxyMaker::setTimeline(core::TimelineSnapshot /*timeline*/) {}

#endif

ProxyMaker::~ProxyMaker() = default;

void ProxyMaker::publish(const PreviewProxies& proxies, const QString& status) {
    if (proxies.size() != proxies_.size() || !std::ranges::equal(proxies, proxies_, [](const auto& a, const auto& b) {
            return a.first == b.first && a.second.file == b.second.file;
        })) {
        proxies_ = proxies;
        emit proxiesChanged();
    }
    if (status != status_) {
        status_ = status;
        emit statusChanged();
    }
}

} // namespace gc::studio
