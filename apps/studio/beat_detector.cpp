#include "beat_detector.hpp"

#include <cstdint>
#include <map>
#include <mutex>
#include <system_error>
#include <thread>
#include <tuple>
#include <utility>

#ifdef GC_STUDIO_PREVIEW
#include "gc/core/io.hpp"
#include "gc/export/project.hpp"
#include "gc/media/audio.hpp"
#endif

namespace gc::studio {

namespace {

#ifdef GC_STUDIO_PREVIEW
// La caché de beats.detect_beats: por la ruta, el tamaño y la fecha del archivo, y se vacía al
// pasar de 32 (volver a detectar el mismo audio no lo vuelve a decodificar).
using CacheKey = std::tuple<std::string, std::uintmax_t, std::int64_t>;
std::mutex g_cache_mutex;                      // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
std::map<CacheKey, core::Beats> g_cache;       // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
constexpr std::size_t kCacheMax = 32;

Result<core::Beats> cached_beats(const std::filesystem::path& path) {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    const auto time = std::filesystem::last_write_time(path, ec);
    const CacheKey key{core::path_to_utf8(std::filesystem::absolute(path, ec)), size,
                       static_cast<std::int64_t>(time.time_since_epoch().count())};
    {
        const std::scoped_lock lock(g_cache_mutex);
        if (const auto it = g_cache.find(key); it != g_cache.end()) {
            return it->second;
        }
    }
    auto beats = media::detect_beats(path);
    if (beats) {
        const std::scoped_lock lock(g_cache_mutex);
        if (g_cache.size() > kCacheMax) {
            g_cache.clear();
        }
        g_cache[key] = *beats;
    }
    return beats;
}
#endif

} // namespace

struct BeatDetector::Worker {
    std::thread thread;
    ~Worker() {
        if (thread.joinable()) {
            thread.join();
        }
    }
    Worker() = default;
    Worker(const Worker&) = delete;
    Worker& operator=(const Worker&) = delete;
    Worker(Worker&&) = delete;
    Worker& operator=(Worker&&) = delete;
};

BeatDetector::BeatDetector(QObject* parent) : QObject(parent), worker_(std::make_unique<Worker>()) {}

BeatDetector::~BeatDetector() = default; // espera al hilo: lo que publique ya no llega (el objeto se va)

bool BeatDetector::available() {
#ifdef GC_STUDIO_PREVIEW
    return true;
#else
    return false;
#endif
}

bool BeatDetector::start(std::filesystem::path root, std::string project_id, core::TimelineClip clip) {
    if (running_ || !available()) {
        return false;
    }
    if (worker_->thread.joinable()) {
        worker_->thread.join(); // el anterior ya acabó (running_ es false)
    }
    running_ = true;
#ifdef GC_STUDIO_PREVIEW
    worker_->thread = std::thread([this, root = std::move(root), pid = std::move(project_id), clip = std::move(clip)] {
        const QString id = QString::fromStdString(clip.id);
        QVariantList times;
        double bpm = 0;
        QString error;
        // El orden de main.py: el tipo del clip y después su archivo.
        if (clip.kind != "audio" && clip.kind != "video") {
            error = "Los beats solo se detectan en clips de audio o vídeo.";
        } else if (auto project = exporter::load_project(root, pid); !project) {
            error = QString::fromStdString(project.error().message);
        } else {
            const auto path = exporter::clip_path(*project, clip);
            std::error_code ec;
            if (!path || !std::filesystem::exists(*path, ec)) {
                error = "No se encuentra el archivo del clip.";
            } else if (auto beats = cached_beats(*path); !beats) {
                error = QString::fromStdString(beats.error().message);
            } else {
                for (const double t : beats->times) {
                    times.push_back(t);
                }
                bpm = beats->bpm;
            }
        }
        QMetaObject::invokeMethod(
            this, [this, id, times = std::move(times), bpm, error] { done(id, times, bpm, error); }, Qt::QueuedConnection);
    });
#else
    (void)root;
    (void)project_id;
    (void)clip;
#endif
    return true;
}

void BeatDetector::done(const QString& clipId, const QVariantList& times, double bpm, const QString& error) {
    running_ = false;
    emit finished(clipId, times, bpm, error);
}

} // namespace gc::studio
