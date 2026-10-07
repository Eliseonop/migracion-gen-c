#pragma once

// EditSession: el documento vivo de una timeline.
//
//   - Guarda la timeline actual como snapshot INMUTABLE (shared_ptr<const Timeline>):
//     se puede pasar tal cual al hilo de render o de export, que nunca verá un
//     estado a medias.
//   - Todo cambio entra por apply() (comandos de edit.hpp) y deja un paso en el
//     historial. Deshacer/rehacer intercambia snapshots: es instantáneo y barato
//     porque los snapshots comparten los clips que no cambian.
//   - Group agrupa varias operaciones en UN paso de deshacer (arrastrar un clip
//     son decenas de move_clip; el usuario deshace una vez).
//   - Los observadores (UI, render, autosave) reciben el snapshot nuevo y qué cambió.
//
// Una EditSession se usa desde UN hilo (el de la UI); lo que se comparte entre
// hilos son los snapshots.

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "gc/core/edit.hpp"
#include "gc/core/error.hpp"
#include "gc/core/ids.hpp"
#include "gc/core/model.hpp"

namespace gc::core {

using TimelineSnapshot = std::shared_ptr<const Timeline>;

// Pilas de deshacer/rehacer y checkpoints con nombre (timeline_history.py).
class History {
public:
    static constexpr std::size_t kDefaultLimit = 50;
    using Checkpoints = std::vector<std::pair<std::string, TimelineSnapshot>>; // en orden de creación

    explicit History(std::size_t limit = kDefaultLimit) : limit_(limit) {}
    // Un historial ya hecho (p. ej. leído de disco). Como en Python, el límite solo se
    // aplica al apilar un estado nuevo: lo que venga de más se conserva hasta entonces.
    History(std::size_t limit, std::deque<TimelineSnapshot> undo, std::vector<TimelineSnapshot> redo,
            Checkpoints checkpoints)
        : limit_(limit), undo_(std::move(undo)), redo_(std::move(redo)), checkpoints_(std::move(checkpoints)) {}

    // Antes de un cambio: apila el estado actual y descarta el rehacer pendiente.
    void record(TimelineSnapshot current);
    // Devuelven el estado restaurado, o nullptr si no hay nada.
    [[nodiscard]] TimelineSnapshot undo(TimelineSnapshot current);
    [[nodiscard]] TimelineSnapshot redo(TimelineSnapshot current);

    [[nodiscard]] bool can_undo() const noexcept { return !undo_.empty(); }
    [[nodiscard]] bool can_redo() const noexcept { return !redo_.empty(); }
    [[nodiscard]] std::size_t undo_depth() const noexcept { return undo_.size(); }
    [[nodiscard]] std::size_t redo_depth() const noexcept { return redo_.size(); }

    // Puntos seguros con nombre: mejor que deshacer paso a paso tras una secuencia larga.
    void checkpoint(std::string name, TimelineSnapshot current);
    [[nodiscard]] TimelineSnapshot checkpoint_state(std::string_view name) const;
    [[nodiscard]] std::vector<std::string> checkpoints() const;

    // Las pilas tal cual (la cima es el final), para guardarlas (history_store.hpp).
    [[nodiscard]] std::size_t limit() const noexcept { return limit_; }
    [[nodiscard]] const std::deque<TimelineSnapshot>& undo_stack() const noexcept { return undo_; }
    [[nodiscard]] const std::vector<TimelineSnapshot>& redo_stack() const noexcept { return redo_; }
    [[nodiscard]] const Checkpoints& checkpoint_states() const noexcept { return checkpoints_; }

private:
    std::size_t limit_;
    std::deque<TimelineSnapshot> undo_;
    std::vector<TimelineSnapshot> redo_;
    Checkpoints checkpoints_;
};

// Qué ha pasado en un cambio de la sesión.
struct Change {
    std::uint64_t revision = 0;
    std::string cause;                // nombre de la op, "undo", "redo", "restore"…
    std::vector<std::string> changed; // ids afectados
    std::vector<std::string> warnings;
};

class EditSession {
public:
    using Listener = std::function<void(const TimelineSnapshot&, const Change&)>;
    // Se llama cada vez que cambia el historial (también al guardar un checkpoint o al
    // cerrar un grupo, que no cambian la timeline).
    using HistoryListener = std::function<void(const History&)>;

    explicit EditSession(Timeline initial, std::unique_ptr<IdSource> ids = std::make_unique<RandomIdSource>(),
                         std::size_t history_limit = History::kDefaultLimit);
    // Retoma una sesión: la timeline y su historial guardado.
    EditSession(Timeline initial, History history, std::unique_ptr<IdSource> ids = std::make_unique<RandomIdSource>());

    [[nodiscard]] const TimelineSnapshot& timeline() const noexcept { return current_; }
    // Sube con cada cambio: sirve de clave para cachés (render, miniaturas, UI).
    [[nodiscard]] std::uint64_t revision() const noexcept { return revision_; }

    [[nodiscard]] Result<Change> apply(const Command& command);
    [[nodiscard]] Result<Change> apply(std::string_view op, const Json& params);

    bool undo();
    bool redo();
    [[nodiscard]] bool can_undo() const noexcept { return history_.can_undo(); }
    [[nodiscard]] bool can_redo() const noexcept { return history_.can_redo(); }

    void checkpoint(std::string name);
    [[nodiscard]] Result<void> restore_checkpoint(std::string_view name);
    [[nodiscard]] std::vector<std::string> checkpoints() const { return history_.checkpoints(); }
    [[nodiscard]] const History& history() const noexcept { return history_; }

    // Varias operaciones = un solo paso de deshacer. Si el grupo se destruye sin
    // commit(), la timeline vuelve a como estaba al empezarlo.
    class Group {
    public:
        explicit Group(EditSession& session);
        ~Group();
        Group(const Group&) = delete;
        Group& operator=(const Group&) = delete;
        void commit();

    private:
        EditSession& session_;
        TimelineSnapshot start_;
        bool done_ = false;
    };

    std::size_t subscribe(Listener listener);
    void unsubscribe(std::size_t id);
    std::size_t subscribe_history(HistoryListener listener);
    void unsubscribe_history(std::size_t id);

private:
    void publish(TimelineSnapshot next, Change change);
    void history_changed();

    TimelineSnapshot current_;
    std::unique_ptr<IdSource> ids_;
    History history_;
    std::uint64_t revision_ = 0;
    int group_depth_ = 0;
    std::vector<std::pair<std::size_t, Listener>> listeners_;
    std::vector<std::pair<std::size_t, HistoryListener>> history_listeners_;
    std::size_t next_listener_ = 1;
};

} // namespace gc::core
