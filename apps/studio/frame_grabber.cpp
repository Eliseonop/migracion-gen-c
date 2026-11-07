#include "frame_grabber.hpp"

#include <QMetaObject>
#include <utility>

#ifdef GC_STUDIO_PREVIEW
#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>

#include "gc/core/export_input.hpp"
#include "gc/media/decode.hpp"
#include "gc/media/probe.hpp"
#endif

namespace gc::studio {

#ifdef GC_STUDIO_PREVIEW

struct FrameGrabber::Worker {
    explicit Worker(FrameGrabber* o) : owner(o) {
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

    struct Ask {
        std::filesystem::path path;
        double t = 0;
        double fps = 30;
        std::uint64_t id = 0;
    };

    FrameGrabber* owner;
    std::mutex mutex;
    std::condition_variable wake;
    bool stop = false;
    std::optional<Ask> pending; // bajo `mutex`
    std::thread thread;         // el último: arranca con todo lo demás ya hecho

    void run() {
        std::filesystem::path known; // la duración del último archivo, para no sondearlo cada vez
        double duration = 0;
        for (;;) {
            Ask ask;
            {
                std::unique_lock lock(mutex);
                wake.wait(lock, [&] { return stop || pending.has_value(); });
                if (stop) {
                    return;
                }
                ask = std::move(*pending);
                pending.reset();
            }
            if (ask.path != known) {
                known = ask.path;
                duration = media::probe_duration(ask.path);
            }
            // safeMediaTime: el último fotograma, no el final (donde ya no hay ninguno).
            double t = std::max(0.0, ask.t);
            if (duration > 0) {
                const double step = std::max(core::kLastFramePull, 1.0 / (ask.fps > 0 ? ask.fps : 30.0));
                t = std::min(t, std::max(0.0, duration - step));
            }
            QImage image;
            if (auto frame = media::frame_at(ask.path, t); frame && frame->has_value()) {
                const media::RgbaFrame& f = **frame;
                image = QImage(f.pixels.data(), f.width, f.height, static_cast<qsizetype>(f.width) * 4,
                               QImage::Format_RGBA8888)
                            .copy();
            }
            FrameGrabber* o = owner;
            QMetaObject::invokeMethod( // QImage comparte los datos: copiarlo no copia los píxeles
                o, [o, id = ask.id, image = std::move(image)] { o->deliver(id, image); }, Qt::QueuedConnection);
        }
    }
};

FrameGrabber::FrameGrabber(QObject* parent) : QObject(parent), worker_(std::make_unique<Worker>(this)) {}

FrameGrabber::~FrameGrabber() = default;

void FrameGrabber::request(std::filesystem::path path, double t, double fps) {
    {
        const std::scoped_lock lock(worker_->mutex);
        worker_->pending = Worker::Ask{std::move(path), t, fps, ++asked_};
    }
    worker_->wake.notify_one();
}

void FrameGrabber::clear() {
    {
        const std::scoped_lock lock(worker_->mutex);
        worker_->pending.reset();
    }
    ++asked_; // lo que esté decodificando llega con un pedido viejo
    if (!frame_.isNull()) {
        frame_ = {};
        emit frameChanged();
    }
}

#else

struct FrameGrabber::Worker {};

FrameGrabber::FrameGrabber(QObject* parent) : QObject(parent) {}
FrameGrabber::~FrameGrabber() = default;
void FrameGrabber::request(std::filesystem::path /*path*/, double /*t*/, double /*fps*/) {}
void FrameGrabber::clear() {}

#endif

void FrameGrabber::deliver(std::uint64_t asked, QImage image) {
    if (asked != asked_ || image.isNull()) {
        return; // de un pedido anterior, o no se pudo leer: se queda el que había
    }
    frame_ = std::move(image);
    emit frameChanged();
}

} // namespace gc::studio
