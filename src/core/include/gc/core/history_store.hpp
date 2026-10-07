#pragma once

// El historial en disco y el autoguardado (espejo de timeline_history.py y de
// timeline_store._load_history/_save_history de video-yt).
//
// Formato de data/history/<pid>.json, el mismo que escribe video-yt:
//   {"undo": [timeline…], "redo": [timeline…], "checkpoints": {nombre: timeline}}
// con cada timeline como su model_dump() y la cima de cada pila al final. Los dos
// programas pueden leer el historial que deja el otro.

#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <string_view>

#include "gc/core/error.hpp"
#include "gc/core/json.hpp"
#include "gc/core/session.hpp"

namespace gc::core {

[[nodiscard]] Json history_to_json(const History& history);

// Tolerante, como el _norm de Python: lo que falte es una pila vacía, y las entradas que
// no se pueden leer como timeline (o, en Python, como dict) se descartan. Las timelines
// de versiones antiguas se migran al leerlas.
[[nodiscard]] History history_from_json(const Json& raw, std::size_t limit = History::kDefaultLimit);

// <data>/history/<pid>.json
[[nodiscard]] std::filesystem::path history_path(const std::filesystem::path& data_dir, std::string_view project_id);

// Sin archivo, o con uno ilegible, un historial vacío: el historial nunca impide editar.
[[nodiscard]] History load_history(const std::filesystem::path& file, std::size_t limit = History::kDefaultLimit);
// Escritura atómica (temporal + reemplazo), en JSON compacto.
[[nodiscard]] Result<void> save_history(const std::filesystem::path& file, const History& history);

// Autoguardado de una sesión: tras cada cambio guarda la timeline (con `save_timeline`:
// la timeline vive dentro del proyecto) y, cada vez que cambia el historial, el
// historial. Escribe en el momento, en el hilo de la sesión; un fallo no detiene la
// edición: queda en last_error() hasta el siguiente guardado bueno.
class Autosave {
public:
    using SaveTimeline = std::function<Result<void>(const Timeline&)>;

    Autosave(EditSession& session, std::filesystem::path history_file, SaveTimeline save_timeline = nullptr);
    ~Autosave();
    Autosave(const Autosave&) = delete;
    Autosave& operator=(const Autosave&) = delete;

    // El fallo pendiente (primero el de la timeline); nullopt = todo guardado.
    [[nodiscard]] std::optional<Error> last_error() const { return timeline_error_ ? timeline_error_ : history_error_; }
    [[nodiscard]] std::size_t writes() const noexcept { return writes_; }

private:
    static void note(std::optional<Error>& slot, const Result<void>& r);

    EditSession& session_;
    std::filesystem::path history_file_;
    SaveTimeline save_timeline_;
    std::size_t timeline_listener_ = 0;
    std::size_t history_listener_ = 0;
    std::optional<Error> timeline_error_;
    std::optional<Error> history_error_;
    std::size_t writes_ = 0;
};

} // namespace gc::core
