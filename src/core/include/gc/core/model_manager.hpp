#pragma once

// Modelos de IA residentes en RAM, como model_manager.py de video-yt: cada motor (Eliminar fondo,
// SAM, Whisper, TTS) registra su modelo al cargarlo con la función que lo suelta, y así:
//   - touch(nombre) en cada inferencia marca el último uso;
//   - in_use(nombre) protege un modelo mientras dura un proceso largo (un job);
//   - un hilo descarga los que llevan más de idle_minutes sin usarse (ajuste models.idle_minutes,
//     por defecto 5; 0 = nunca);
//   - solo un modelo PESADO a la vez: al registrar uno, los otros pesados que no estén en uso se
//     descargan.
// El orden de los modelos es el de un dict de Python: el de registro, y volver a registrar uno no
// lo mueve. Las funciones de descarga se llaman fuera del cerrojo y si lanzan, se avisa y se sigue.

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "gc/core/json.hpp"

namespace gc::core {

inline constexpr double kModelIdleMinutesDefault = 5.0;

// idle_minutes() de model_manager.py sobre los ajustes ya cargados (lo que da settings.load()):
// `(ajustes.get("models") or {}).get("idle_minutes", 5)` pasado por float() de Python y nunca
// negativo (`max(0.0, …)`: un NaN da 0); si algo falla (no es un dict, ni un número), 5.
[[nodiscard]] double model_idle_minutes(const Json& settings);

class ModelManager {
public:
    struct Options {
        // Segundos de un reloj monótono (time.monotonic); vacío = std::chrono::steady_clock.
        std::function<double()> clock;
        // Los minutos de inactividad antes de descargar (model_idle_minutes de los ajustes); vacío =
        // kModelIdleMinutesDefault.
        std::function<double()> idle_minutes;
        // El hilo que descarga los inactivos cada `reaper_interval_s` (se arranca con el primer
        // registro, como _ensure_reaper). false para los tests.
        bool reaper = true;
        double reaper_interval_s = 60.0;
        // Lo que model_manager.py manda a su log (descargas y fallos al descargar).
        std::function<void(const std::string&)> on_log;
    };

    // Lo que da loaded(): por nombre, con los segundos redondeados a una cifra como round(…, 1).
    struct Loaded {
        std::string name;
        bool heavy = false;
        bool busy = false;
        double idle_s = 0;
        double loaded_s = 0;
    };

    // Mientras viva, el modelo no se descarga por inactividad (el `with in_use(nombre)` de Python).
    class InUse {
    public:
        InUse(InUse&& other) noexcept;
        InUse& operator=(InUse&&) = delete;
        InUse(const InUse&) = delete;
        InUse& operator=(const InUse&) = delete;
        ~InUse();

    private:
        friend class ModelManager;
        InUse(ModelManager* manager, std::string name);
        ModelManager* manager_;
        std::string name_;
    };

    ModelManager();
    explicit ModelManager(Options options);
    ModelManager(const ModelManager&) = delete;
    ModelManager& operator=(const ModelManager&) = delete;
    ModelManager(ModelManager&&) = delete;
    ModelManager& operator=(ModelManager&&) = delete;
    ~ModelManager(); // para el hilo; no descarga nada

    // Anota un modelo recién cargado (register de Python). `unload` debe soltar sus referencias.
    void register_model(const std::string& name, std::function<void()> unload, bool heavy = false);
    void touch(std::string_view name);
    // El motor soltó el modelo por su cuenta: quitarlo del registro (sin llamar a `unload`).
    void forget(std::string_view name);
    [[nodiscard]] InUse in_use(std::string name);

    bool unload(std::string_view name);
    // Los que no están en uso, en el orden del registro.
    std::vector<std::string> unload_all();
    // Los que llevan al menos `max_idle_s` segundos sin usarse (sin valor: idle_minutes() · 60);
    // con 0 o menos, ninguno.
    std::vector<std::string> unload_idle(std::optional<double> max_idle_s = std::nullopt);

    [[nodiscard]] std::vector<Loaded> loaded() const;
    [[nodiscard]] double idle_minutes() const;
    // memory_report(): {"rss_bytes", "idle_minutes", "models": loaded()}.
    [[nodiscard]] Json memory_report() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Memoria residente del proceso en bytes (process_rss_bytes): el WorkingSetSize en Windows y la
// segunda cifra de /proc/self/statm por el tamaño de página en Linux; 0 si no se puede saber.
[[nodiscard]] std::uint64_t process_rss_bytes();

} // namespace gc::core
