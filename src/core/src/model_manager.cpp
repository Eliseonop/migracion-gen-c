#include "gc/core/model_manager.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <format>
#include <fstream>
#include <mutex>
#include <thread>
#include <utility>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// windows.h antes que psapi.h
#include <psapi.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

#include "py.hpp"

namespace gc::core {

namespace {

// `bool(x)` de Python para un valor JSON.
bool py_truthy(const Json& j) {
    if (j.is_null()) {
        return false;
    }
    if (j.is_boolean()) {
        return j.get<bool>();
    }
    if (j.is_number()) {
        return j.get<double>() != 0.0;
    }
    if (j.is_string() || j.is_array() || j.is_object()) {
        return !j.empty();
    }
    return true;
}

} // namespace

double model_idle_minutes(const Json& settings) {
    // `settings.load().get("models") or {}` y `.get("idle_minutes", 5)`: si no son dicts, el
    // AttributeError cae en el except y da el valor por defecto; float() que falla, también.
    if (!settings.is_object()) {
        return kModelIdleMinutesDefault;
    }
    Json models = settings.contains("models") ? settings.at("models") : Json(nullptr);
    if (!py_truthy(models)) {
        models = Json::object();
    }
    if (!models.is_object()) {
        return kModelIdleMinutesDefault;
    }
    const auto f = detail::py_float(models.contains("idle_minutes") ? models.at("idle_minutes") : Json(5));
    if (!f) {
        return kModelIdleMinutesDefault;
    }
    return *f > 0.0 ? *f : 0.0; // max(0.0, f): con NaN, el primero
}

struct ModelManager::Impl {
    struct Entry {
        std::string name;
        std::function<void()> unload;
        bool heavy = false;
        double last = 0;
        int busy = 0;
        double since = 0;
    };

    explicit Impl(Options o) : options(std::move(o)) {}
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
    Impl(Impl&&) = delete;
    Impl& operator=(Impl&&) = delete;
    ~Impl() {
        {
            const std::scoped_lock lock(reaper_mutex);
            stop = true;
        }
        wake.notify_all();
        if (reaper.joinable()) {
            reaper.join();
        }
    }

    Options options;
    mutable std::mutex mutex;
    std::vector<Entry> models; // en el orden de registro, como el dict de Python

    std::mutex reaper_mutex;
    std::condition_variable wake;
    std::thread reaper;
    bool reaper_started = false;
    bool stop = false;

    [[nodiscard]] double now() const {
        if (options.clock) {
            return options.clock();
        }
        return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    Entry* find(std::string_view name) {
        for (Entry& e : models) {
            if (e.name == name) {
                return &e;
            }
        }
        return nullptr;
    }

    void log(const std::string& message) const {
        if (options.on_log) {
            options.on_log(message);
        }
    }

    [[nodiscard]] double idle_minutes() const {
        return options.idle_minutes ? options.idle_minutes() : kModelIdleMinutesDefault;
    }

    // _unload: lo quita del registro y, fuera del cerrojo, llama a su descarga.
    bool unload_one(const std::string& name) {
        std::function<void()> fn;
        {
            const std::scoped_lock lock(mutex);
            const auto it = std::ranges::find(models, name, &Entry::name);
            if (it == models.end()) {
                return false;
            }
            fn = std::move(it->unload);
            models.erase(it);
        }
        try {
            if (fn) {
                fn();
            }
        } catch (const std::exception& e) { // una descarga fallida no debe tumbar nada
            log(std::format("Modelos: fallo al descargar {}: {}", name, e.what()));
        } catch (...) {
            log(std::format("Modelos: fallo al descargar {}", name));
        }
        log(std::format("Modelos: {} descargado de la memoria.", name));
        return true;
    }

    std::vector<std::string> unload_idle(std::optional<double> max_idle_s) {
        const double max = max_idle_s ? *max_idle_s : idle_minutes() * 60.0;
        if (max <= 0) {
            return {};
        }
        const double t = now();
        std::vector<std::string> names;
        {
            const std::scoped_lock lock(mutex);
            for (const Entry& e : models) {
                if (e.busy == 0 && t - e.last >= max) {
                    names.push_back(e.name);
                }
            }
        }
        std::vector<std::string> out;
        for (const std::string& n : names) {
            if (unload_one(n)) {
                out.push_back(n);
            }
        }
        return out;
    }

    void ensure_reaper() {
        if (!options.reaper) {
            return;
        }
        const std::scoped_lock lock(reaper_mutex);
        if (reaper_started) {
            return;
        }
        reaper_started = true;
        reaper = std::thread([this] {
            std::unique_lock sleeping(reaper_mutex);
            const auto interval = std::chrono::duration<double>(options.reaper_interval_s);
            while (!wake.wait_for(sleeping, interval, [&] { return stop; })) {
                sleeping.unlock();
                try {
                    static_cast<void>(unload_idle(std::nullopt));
                } catch (const std::exception& e) { // el hilo no debe morir
                    log(std::format("Modelos: fallo en la descarga por inactividad: {}", e.what()));
                }
                sleeping.lock();
            }
        });
    }
};

ModelManager::ModelManager() : ModelManager(Options{}) {}
ModelManager::ModelManager(Options options) : impl_(std::make_unique<Impl>(std::move(options))) {}
ModelManager::~ModelManager() = default;

void ModelManager::register_model(const std::string& name, std::function<void()> unload, bool heavy) {
    std::vector<std::string> others;
    {
        const std::scoped_lock lock(impl_->mutex);
        const double last = impl_->now();
        if (Impl::Entry* prev = impl_->find(name)) { // la clave ya estaba: no cambia de sitio
            prev->unload = std::move(unload);
            prev->heavy = heavy;
            prev->last = last;
        } else {
            const double since = impl_->now();
            impl_->models.push_back({name, std::move(unload), heavy, last, 0, since});
        }
        if (heavy) {
            for (const Impl::Entry& e : impl_->models) {
                if (e.name != name && e.heavy && e.busy == 0) {
                    others.push_back(e.name);
                }
            }
        }
    }
    for (const std::string& n : others) {
        impl_->log(std::format("Modelos: descargo {} para cargar {} (uno pesado a la vez).", n, name));
        impl_->unload_one(n);
    }
    impl_->ensure_reaper();
}

void ModelManager::touch(std::string_view name) {
    const std::scoped_lock lock(impl_->mutex);
    if (Impl::Entry* e = impl_->find(name)) {
        e->last = impl_->now();
    }
}

void ModelManager::forget(std::string_view name) {
    const std::scoped_lock lock(impl_->mutex);
    std::erase_if(impl_->models, [&](const Impl::Entry& e) { return e.name == name; });
}

ModelManager::InUse ModelManager::in_use(std::string name) {
    {
        const std::scoped_lock lock(impl_->mutex);
        if (Impl::Entry* e = impl_->find(name)) {
            ++e->busy;
        }
    }
    return InUse(this, std::move(name));
}

ModelManager::InUse::InUse(ModelManager* manager, std::string name) : manager_(manager), name_(std::move(name)) {}

ModelManager::InUse::InUse(InUse&& other) noexcept :
    manager_(std::exchange(other.manager_, nullptr)), name_(std::move(other.name_)) {}

ModelManager::InUse::~InUse() {
    if (manager_ == nullptr) {
        return;
    }
    // Al salir del `with`: el que haya ahora con ese nombre (puede ser otro registro).
    Impl& m = *manager_->impl_;
    const std::scoped_lock lock(m.mutex);
    if (Impl::Entry* e = m.find(name_)) {
        e->busy = std::max(0, e->busy - 1);
        e->last = m.now();
    }
}

bool ModelManager::unload(std::string_view name) { return impl_->unload_one(std::string(name)); }

std::vector<std::string> ModelManager::unload_all() {
    std::vector<std::string> names;
    {
        const std::scoped_lock lock(impl_->mutex);
        for (const Impl::Entry& e : impl_->models) {
            if (e.busy == 0) {
                names.push_back(e.name);
            }
        }
    }
    std::vector<std::string> out;
    for (const std::string& n : names) {
        if (impl_->unload_one(n)) {
            out.push_back(n);
        }
    }
    return out;
}

std::vector<std::string> ModelManager::unload_idle(std::optional<double> max_idle_s) {
    return impl_->unload_idle(max_idle_s);
}

std::vector<ModelManager::Loaded> ModelManager::loaded() const {
    const double t = impl_->now();
    std::vector<Loaded> out;
    {
        const std::scoped_lock lock(impl_->mutex);
        for (const Impl::Entry& e : impl_->models) {
            out.push_back({e.name, e.heavy, e.busy > 0, detail::py_round(t - e.last, 1), detail::py_round(t - e.since, 1)});
        }
    }
    std::ranges::sort(out, {}, &Loaded::name); // sorted(_models.items()): por nombre (puntos de código)
    return out;
}

double ModelManager::idle_minutes() const { return impl_->idle_minutes(); }

Json ModelManager::memory_report() const {
    Json models = Json::array();
    for (const Loaded& l : loaded()) {
        Json m = Json::object();
        m["name"] = l.name;
        m["heavy"] = l.heavy;
        m["busy"] = l.busy;
        m["idle_s"] = l.idle_s;
        m["loaded_s"] = l.loaded_s;
        models.push_back(std::move(m));
    }
    Json out = Json::object();
    out["rss_bytes"] = process_rss_bytes();
    out["idle_minutes"] = idle_minutes();
    out["models"] = std::move(models);
    return out;
}

std::uint64_t process_rss_bytes() {
#ifdef _WIN32
    // GetProcessMemoryInfo de psapi, como el ctypes de Python; K32… está en kernel32 desde Windows 7
    // (así no hace falta enlazar psapi.lib).
    using Fn = BOOL(WINAPI*)(HANDLE, PPROCESS_MEMORY_COUNTERS, DWORD);
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    if (k32 == nullptr) {
        return 0;
    }
    // GetProcAddress da un puntero de función genérico: por void* para que GCC no avise
    // (-Wcast-function-type) al pasarlo al tipo de verdad.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast,bugprone-casting-through-void)
    const auto fn = reinterpret_cast<Fn>(reinterpret_cast<void*>(GetProcAddress(k32, "K32GetProcessMemoryInfo")));
    PROCESS_MEMORY_COUNTERS pmc{};
    pmc.cb = sizeof(pmc);
    if (fn != nullptr && fn(GetCurrentProcess(), &pmc, pmc.cb) != 0) {
        return pmc.WorkingSetSize;
    }
    return 0;
#elif defined(__linux__)
    std::ifstream statm("/proc/self/statm");
    std::uint64_t size = 0;
    std::uint64_t resident = 0;
    if (statm >> size >> resident) {
        const long page = sysconf(_SC_PAGESIZE);
        return page > 0 ? resident * static_cast<std::uint64_t>(page) : 0;
    }
    return 0;
#else
    return 0; // macOS: Python usa ru_maxrss (el pico, no la residente); sin equivalente aquí
#endif
}

} // namespace gc::core
