#include "export_job.hpp"

#include <QFileInfo>
#include <cmath>
#include <utility>

#ifdef GC_STUDIO_PREVIEW
#include <atomic>
#include <chrono>
#include <cstdint>
#include <format>
#include <mutex>
#include <system_error>
#include <thread>

#include "gc/core/io.hpp"
#include "gc/core/projects_store.hpp"
#include "gc/export/project.hpp"
#include "gc/export/render.hpp"
#endif

namespace gc::studio {

#ifdef GC_STUDIO_PREVIEW

struct ExportJob::Worker {
    Worker() = default;
    Worker(const Worker&) = delete;
    Worker& operator=(const Worker&) = delete;
    Worker(Worker&&) = delete;
    Worker& operator=(Worker&&) = delete;
    ~Worker() {
        cancel = true;
        if (thread.joinable()) {
            thread.join();
        }
    }

    std::atomic<bool> cancel{false};
    std::thread thread;
};

ExportJob::ExportJob(QObject* parent) : QObject(parent) {}

bool ExportJob::available() const { return true; }

bool ExportJob::start(std::filesystem::path root, std::string project_id, core::TimelineSnapshot timeline) {
    if (running_) {
        return false;
    }
    worker_.reset(); // el hilo del anterior ya acabó: se recoge
    warnings_.clear();
    file_.clear();
    folder_.clear();
    progress_ = 0;
    if (!timeline || timeline->clips.empty()) {
        error_ = "No hay nada en la timeline para exportar.";
        message_ = error_;
        emit changed();
        return false;
    }
    running_ = true;
    error_.clear();
    message_ = "Exportando… 0 %";
    emit changed();
    worker_ = std::make_unique<Worker>();
    Worker* w = worker_.get();
    w->thread = std::thread([this, w, root = std::move(root), id = std::move(project_id), tl = std::move(timeline)] {
        const auto finish = [this](QString file, QString error, bool cancelled, QStringList warnings) {
            QMetaObject::invokeMethod(
                this,
                [this, file = std::move(file), error = std::move(error), cancelled, warnings = std::move(warnings)] {
                    done(file, error, cancelled, warnings);
                },
                Qt::QueuedConnection);
        };
        auto loaded = exporter::load_project(root, id);
        if (!loaded) {
            finish({}, QString::fromStdString(loaded.error().message), false, {});
            return;
        }
        exporter::LoadedProject project = std::move(*loaded);
        project.timeline = *tl; // la de la sesión (ya guardada: el editor guarda cada cambio)
        // jobs._run_export: <carpeta del proyecto>/exports/final_<UTC>.mp4.
        const std::filesystem::path folder = core::project_base(project.dirs.clips_dir, project.project) / "exports";
        std::error_code ec;
        std::filesystem::create_directories(folder, ec);
        if (ec) {
            finish({}, QString::fromStdString(std::format("no se pudo crear {}", core::path_to_utf8(folder))), false, {});
            return;
        }
        const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
        const std::filesystem::path out = folder / core::path_from_utf8(std::format("final_{:%Y%m%d-%H%M%S}.mp4", now));

        std::mutex warn_mutex;
        QStringList warnings;
        exporter::RenderOptions options;
        options.omit_unsupported = true;
        options.on_warning = [&](const std::string& message) {
            const std::scoped_lock lock(warn_mutex);
            warnings << QString::fromStdString(message);
        };
        // Del hilo que codifica: unas 5 veces por segundo y el último.
        auto last = std::chrono::steady_clock::now() - std::chrono::seconds(1);
        options.on_progress = [&](std::int64_t frames_done, std::int64_t total) {
            const auto t = std::chrono::steady_clock::now();
            if (frames_done < total && t - last < std::chrono::milliseconds(200)) {
                return;
            }
            last = t;
            const double part = total > 0 ? static_cast<double>(frames_done) / static_cast<double>(total) : 1.0;
            QMetaObject::invokeMethod(this, [this, part] { progressed(part); }, Qt::QueuedConnection);
        };
        options.cancelled = [w] { return w->cancel.load(); };
        auto stats = exporter::render_project(project, out, options);
        const bool cancelled = stats && stats->cancelled;
        if (!stats || cancelled) {
            std::filesystem::remove(out, ec); // nada a medias
        }
        QStringList seen;
        {
            const std::scoped_lock lock(warn_mutex);
            seen = warnings;
        }
        finish(stats && !cancelled ? QString::fromStdString(core::path_to_utf8(out)) : QString(),
               stats ? QString() : QString::fromStdString(stats.error().message), cancelled, seen);
    });
    return true;
}

void ExportJob::cancel() {
    if (worker_) {
        worker_->cancel = true;
    }
}

#else // sin gc_export: no hay con qué renderizar

struct ExportJob::Worker {};

ExportJob::ExportJob(QObject* parent) : QObject(parent) {}

bool ExportJob::available() const { return false; }

bool ExportJob::start(std::filesystem::path /*root*/, std::string /*project_id*/, core::TimelineSnapshot /*timeline*/) {
    error_ = "Exportar necesita gc_export (compila con MSVC + vcpkg).";
    message_ = error_;
    emit changed();
    return false;
}

void ExportJob::cancel() {}

#endif

ExportJob::~ExportJob() = default;

void ExportJob::dismiss() {
    if (running_) {
        return;
    }
    progress_ = 0;
    message_.clear();
    error_.clear();
    file_.clear();
    folder_.clear();
    warnings_.clear();
    emit changed();
}

void ExportJob::progressed(double progress) {
    if (!running_) {
        return;
    }
    progress_ = progress;
    message_ = QString("Exportando… %1 %").arg(static_cast<int>(std::lround(progress * 100)));
    emit changed();
}

void ExportJob::done(const QString& file, const QString& error, bool cancelled, const QStringList& warnings) {
    running_ = false;
    file_ = file;
    folder_ = file.isEmpty() ? QUrl() : QUrl::fromLocalFile(QFileInfo(file).absolutePath());
    error_ = error;
    warnings_ = warnings;
    if (!file.isEmpty()) {
        progress_ = 1.0;
    }
    // Los mensajes del job de video-yt.
    message_ = cancelled           ? QString("Export cancelado.")
               : !error.isEmpty()  ? "Error exportando el vídeo final: " + error
                                   : QString("Vídeo final exportado.");
    emit changed();
    emit finished();
}

} // namespace gc::studio
