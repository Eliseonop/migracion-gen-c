#include "thumbnails.hpp"

#include <algorithm>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <system_error>

#include "gc/core/io.hpp"

#ifdef GC_STUDIO_PREVIEW
#include <QImage>
#include <QString>
#include <condition_variable>
#include <mutex>
#include <set>
#include <thread>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include "gc/media/decode.hpp"
#endif

namespace gc::studio {

namespace {

namespace fs = std::filesystem;

// Sube si cambia cómo se hace una miniatura: las de antes dejan de valer.
constexpr int kThumbVersion = 1;

// Lo que identifica la versión del archivo: su tamaño y su fecha.
struct SourceId {
    std::uintmax_t size = 0;
    std::int64_t mtime = 0;
};

std::optional<SourceId> source_id(const fs::path& source) {
    std::error_code ec;
    if (!fs::is_regular_file(source, ec)) {
        return std::nullopt;
    }
    const auto size = fs::file_size(source, ec);
    if (ec) {
        return std::nullopt;
    }
    const auto time = fs::last_write_time(source, ec);
    if (ec) {
        return std::nullopt;
    }
    return SourceId{size, static_cast<std::int64_t>(time.time_since_epoch().count())};
}

// La miniatura que le toca a `source` en la caché, esté hecha o no: FNV-1a de la ruta absoluta, el
// tamaño, la fecha, las medidas y la versión (como la clave de los proxies, gc/export/proxy.hpp). Nada
// sin caché o si el vídeo no se puede leer.
std::optional<fs::path> thumbnail_path(const fs::path& cache_dir, const fs::path& source) {
    if (cache_dir.empty()) {
        return std::nullopt;
    }
    const auto id = source_id(source);
    if (!id) {
        return std::nullopt;
    }
    std::uint64_t h = 0xCBF29CE484222325ULL;
    const auto mix = [&h](std::string_view s) {
        for (const char c : s) {
            h = (h ^ static_cast<unsigned char>(c)) * 0x100000001B3ULL;
        }
        h = (h ^ 0xFFU) * 0x100000001B3ULL; // separador
    };
    std::error_code ec;
    const fs::path absolute = fs::absolute(source, ec);
    mix(core::path_to_utf8((ec ? source : absolute).lexically_normal()));
    mix(std::to_string(id->size));
    mix(std::to_string(id->mtime));
    mix(std::format("{}x{}", kThumbWidth, kThumbHeight));
    mix(std::to_string(kThumbVersion));
    return cache_dir / std::format("{:016x}.jpg", h);
}

#ifdef GC_STUDIO_PREVIEW

constexpr int kJpegQuality = 85;

QString qpath(const fs::path& path) {
    return QString::fromStdString(core::path_to_utf8(path));
}

// El primer fotograma de `source` en `file`: reducido hasta cubrir kThumbWidth × kThumbHeight y
// recortado al centro, como `object-fit: cover` (la tarjeta lo pinta tal cual).
Result<void> write_thumbnail(const fs::path& source, const fs::path& file) {
    const std::string name = core::path_to_utf8(source.filename());
    auto frame = media::frame_at(source, 0.0);
    if (!frame) {
        return fail(frame.error().code, std::format("no se puede leer {}: {}", name, frame.error().message));
    }
    if (!*frame || (*frame)->width <= 0 || (*frame)->height <= 0) {
        return fail(ErrorCode::NotFound, std::format("{} no tiene fotogramas", name));
    }
    const media::RgbaFrame& f = **frame;
    const QImage full(f.pixels.data(), f.width, f.height, static_cast<qsizetype>(f.width) * 4,
                      QImage::Format_RGBA8888);
    const QImage cover =
        full.scaled(kThumbWidth, kThumbHeight, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    const QImage thumb = cover
                             .copy((cover.width() - kThumbWidth) / 2, (cover.height() - kThumbHeight) / 2,
                                   kThumbWidth, kThumbHeight)
                             .convertToFormat(QImage::Format_RGB888);
    std::error_code ec;
    fs::create_directories(file.parent_path(), ec);
    fs::path part = file;
    part += ".part";
    bool ok = !ec && thumb.save(qpath(part), "JPG", kJpegQuality);
    if (ok) {
        fs::rename(part, file, ec);
        ok = !ec;
    }
    if (!ok) {
        fs::remove(part, ec);
        return fail(ErrorCode::Io, std::format("no se puede escribir la miniatura {}", core::path_to_utf8(file)));
    }
    return {};
}

#endif

} // namespace

std::optional<std::filesystem::path> find_thumbnail(const std::filesystem::path& cache_dir,
                                                    const std::filesystem::path& source) {
    auto file = thumbnail_path(cache_dir, source);
    std::error_code ec;
    if (!file || !fs::is_regular_file(*file, ec)) {
        return std::nullopt;
    }
    return file;
}

#ifdef GC_STUDIO_PREVIEW

Result<std::filesystem::path> make_thumbnail(const std::filesystem::path& cache_dir,
                                             const std::filesystem::path& source) {
    if (cache_dir.empty()) {
        return fail(ErrorCode::InvalidArgument, "sin carpeta para las miniaturas");
    }
    const auto file = thumbnail_path(cache_dir, source);
    if (!file) {
        return fail(ErrorCode::NotFound, std::format("no se puede leer {}", core::path_to_utf8(source)));
    }
    std::error_code ec;
    if (fs::is_regular_file(*file, ec)) {
        return *file;
    }
    if (auto written = write_thumbnail(source, *file); !written) {
        return std::unexpected(written.error());
    }
    return *file;
}

struct Thumbnails::Worker {
    explicit Worker(Thumbnails* o) : owner(o) {
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

    Thumbnails* owner;
    // Lo pendiente, bajo `mutex`: lo deja el hilo de la interfaz y lo recoge este.
    std::mutex mutex;
    std::condition_variable wake;
    bool stop = false;
    bool pending = false;
    fs::path cache_dir;
    std::vector<fs::path> sources;
    std::thread thread; // el último: arranca con todo lo demás ya hecho

    // Una petición nueva o salir cortan la tanda, entre una miniatura y la siguiente.
    [[nodiscard]] bool interrupted() {
        const std::scoped_lock lock(mutex);
        return stop || pending;
    }

    void post(Done done) const {
        if (done.empty()) {
            return;
        }
        Thumbnails* o = owner;
        QMetaObject::invokeMethod(o, [o, done = std::move(done)] { o->publish(done); }, Qt::QueuedConnection);
    }

    void run() {
#ifdef _WIN32
        // Por debajo de la interfaz: es trabajo de fondo.
        SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
#endif
        // Las que no se pudieron hacer, por su nombre en la caché (que cambia con el archivo): no se
        // reintentan hasta que el vídeo cambie.
        std::set<fs::path> failed;
        for (;;) {
            std::vector<fs::path> todo;
            fs::path cache;
            {
                std::unique_lock lock(mutex);
                wake.wait(lock, [&] { return stop || pending; });
                if (stop) {
                    return;
                }
                pending = false;
                todo = std::exchange(sources, {});
                cache = cache_dir;
            }
            if (!cache.empty()) {
                make_all(todo, cache, failed);
            }
        }
    }

    void make_all(const std::vector<fs::path>& todo, const fs::path& cache, std::set<fs::path>& failed) {
        Done found;   // lo que ya se sabe: las de la caché y los vídeos que no pueden tenerla
        Done missing; // el vídeo y la miniatura que le toca
        for (const fs::path& source : todo) {
            const auto file = thumbnail_path(cache, source);
            std::error_code ec;
            if (!file || failed.contains(*file)) {
                found.emplace_back(source, fs::path());
            } else if (fs::is_regular_file(*file, ec)) {
                found.emplace_back(source, *file);
            } else {
                missing.emplace_back(source, *file);
            }
        }
        post(std::move(found));
        for (const auto& [source, file] : missing) {
            if (interrupted()) {
                return; // el bucle de run() atiende lo que ha llegado
            }
            // Si no sale, tampoco vale la que tenía antes (de otra versión del archivo).
            const bool made = write_thumbnail(source, file).has_value();
            if (!made) {
                failed.insert(file);
            }
            post({{source, made ? file : fs::path()}});
        }
    }
};

Thumbnails::Thumbnails(QObject* parent) : QObject(parent), worker_(std::make_unique<Worker>(this)) {}

void Thumbnails::setCacheDir(std::filesystem::path dir) {
    if (dir == cache_dir_) {
        return;
    }
    cache_dir_ = dir;
    {
        const std::scoped_lock lock(worker_->mutex);
        worker_->cache_dir = std::move(dir);
    }
    if (!ready_.empty()) { // las de otra caché
        ready_.clear();
        emit changed();
    }
}

void Thumbnails::request(std::vector<std::filesystem::path> sources) {
    std::vector<std::filesystem::path> unique; // sin repetir, en su orden
    unique.reserve(sources.size());
    for (std::filesystem::path& source : sources) {
        if (std::ranges::find(unique, source) == unique.end()) {
            unique.push_back(std::move(source));
        }
    }
    {
        const std::scoped_lock lock(worker_->mutex);
        worker_->pending = true;
        worker_->sources = std::move(unique);
    }
    worker_->wake.notify_one();
}

#else // sin gc_export: sin miniaturas

Result<std::filesystem::path> make_thumbnail(const std::filesystem::path& /*cache_dir*/,
                                             const std::filesystem::path& /*source*/) {
    return fail(ErrorCode::Unsupported, "sin gc_export no hay miniaturas");
}

struct Thumbnails::Worker {};

Thumbnails::Thumbnails(QObject* parent) : QObject(parent) {}
void Thumbnails::setCacheDir(std::filesystem::path dir) { cache_dir_ = std::move(dir); }
void Thumbnails::request(std::vector<std::filesystem::path> /*sources*/) {}

#endif

Thumbnails::~Thumbnails() = default;

std::filesystem::path Thumbnails::find(const std::filesystem::path& source) const {
    const auto it = ready_.find(source);
    return it == ready_.end() ? std::filesystem::path() : it->second;
}

void Thumbnails::publish(const Done& done) {
    bool any = false;
    for (const auto& [source, file] : done) {
        if (file.empty()) {
            any = ready_.erase(source) > 0 || any;
        } else if (std::filesystem::path& known = ready_[source]; known != file) {
            known = file;
            any = true;
        }
    }
    if (any) {
        emit changed();
    }
}

} // namespace gc::studio
