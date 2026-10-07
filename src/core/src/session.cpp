#include "gc/core/session.hpp"

#include <algorithm>

namespace gc::core {

// --- History ------------------------------------------------------------------------------

void History::record(TimelineSnapshot current) {
    undo_.push_back(std::move(current));
    while (undo_.size() > limit_) {
        undo_.pop_front();
    }
    redo_.clear();
}

TimelineSnapshot History::undo(TimelineSnapshot current) {
    if (undo_.empty()) {
        return nullptr;
    }
    TimelineSnapshot restored = std::move(undo_.back());
    undo_.pop_back();
    redo_.push_back(std::move(current));
    return restored;
}

TimelineSnapshot History::redo(TimelineSnapshot current) {
    if (redo_.empty()) {
        return nullptr;
    }
    TimelineSnapshot restored = std::move(redo_.back());
    redo_.pop_back();
    undo_.push_back(std::move(current));
    return restored;
}

void History::checkpoint(std::string name, TimelineSnapshot current) {
    const auto it = std::ranges::find(checkpoints_, name, &std::pair<std::string, TimelineSnapshot>::first);
    if (it != checkpoints_.end()) {
        it->second = std::move(current); // mismo nombre: se actualiza en su sitio
        return;
    }
    checkpoints_.emplace_back(std::move(name), std::move(current));
}

TimelineSnapshot History::checkpoint_state(std::string_view name) const {
    const auto it = std::ranges::find(checkpoints_, name, &std::pair<std::string, TimelineSnapshot>::first);
    return it == checkpoints_.end() ? nullptr : it->second;
}

std::vector<std::string> History::checkpoints() const {
    std::vector<std::string> out;
    for (const auto& [name, state] : checkpoints_) {
        out.push_back(name);
    }
    return out;
}

// --- EditSession -------------------------------------------------------------------------

EditSession::EditSession(Timeline initial, std::unique_ptr<IdSource> ids, std::size_t history_limit)
    : current_(std::make_shared<const Timeline>(std::move(initial))), ids_(std::move(ids)), history_(history_limit) {}

EditSession::EditSession(Timeline initial, History history, std::unique_ptr<IdSource> ids)
    : current_(std::make_shared<const Timeline>(std::move(initial))), ids_(std::move(ids)),
      history_(std::move(history)) {}

Result<Change> EditSession::apply(const Command& command) {
    auto result = apply_command(*current_, command, *ids_);
    if (!result) {
        return std::unexpected(result.error());
    }
    const bool recorded = group_depth_ == 0;
    if (recorded) {
        history_.record(current_);
    }
    Change change{0, std::string(command_name(command)), std::move(result->changed), std::move(result->warnings)};
    if (recorded) {
        history_changed(); // como timeline_store: el historial se guarda antes que la timeline
    }
    publish(std::make_shared<const Timeline>(std::move(result->timeline)), change);
    change.revision = revision_;
    return change;
}

Result<Change> EditSession::apply(std::string_view op, const Json& params) {
    auto command = parse_command(op, params);
    if (!command) {
        return std::unexpected(command.error());
    }
    return apply(*command);
}

bool EditSession::undo() {
    TimelineSnapshot restored = history_.undo(current_);
    if (!restored) {
        return false;
    }
    history_changed();
    publish(std::move(restored), Change{0, "undo", {"undo"}, {}});
    return true;
}

bool EditSession::redo() {
    TimelineSnapshot restored = history_.redo(current_);
    if (!restored) {
        return false;
    }
    history_changed();
    publish(std::move(restored), Change{0, "redo", {"redo"}, {}});
    return true;
}

void EditSession::checkpoint(std::string name) {
    history_.checkpoint(std::move(name), current_);
    history_changed();
}

Result<void> EditSession::restore_checkpoint(std::string_view name) {
    TimelineSnapshot state = history_.checkpoint_state(name);
    if (!state) {
        return fail(ErrorCode::NotFound, "Checkpoint inexistente: " + std::string(name));
    }
    history_.record(current_); // restaurar también se puede deshacer
    history_changed();
    publish(std::move(state), Change{0, "restore", {"restore"}, {}});
    return {};
}

std::size_t EditSession::subscribe(Listener listener) {
    const std::size_t id = next_listener_++;
    listeners_.emplace_back(id, std::move(listener));
    return id;
}

void EditSession::unsubscribe(std::size_t id) {
    std::erase_if(listeners_, [&](const auto& entry) { return entry.first == id; });
}

std::size_t EditSession::subscribe_history(HistoryListener listener) {
    const std::size_t id = next_listener_++;
    history_listeners_.emplace_back(id, std::move(listener));
    return id;
}

void EditSession::unsubscribe_history(std::size_t id) {
    std::erase_if(history_listeners_, [&](const auto& entry) { return entry.first == id; });
}

void EditSession::history_changed() {
    const auto listeners = history_listeners_;
    for (const auto& [id, listener] : listeners) {
        listener(history_);
    }
}

void EditSession::publish(TimelineSnapshot next, Change change) {
    current_ = std::move(next);
    change.revision = ++revision_;
    // Copia: un observador puede (des)suscribirse mientras se notifica.
    const auto listeners = listeners_;
    for (const auto& [id, listener] : listeners) {
        listener(current_, change);
    }
}

// --- Group --------------------------------------------------------------------------------

EditSession::Group::Group(EditSession& session) : session_(session), start_(session.current_) {
    ++session_.group_depth_;
}

void EditSession::Group::commit() {
    if (done_) {
        return;
    }
    done_ = true;
    --session_.group_depth_;
    if (session_.group_depth_ == 0 && session_.current_ != start_) {
        session_.history_.record(start_); // todo el grupo es un solo paso
        session_.history_changed();
    }
}

EditSession::Group::~Group() {
    if (done_) {
        return;
    }
    // Sin commit: deshacer todo lo hecho dentro del grupo.
    --session_.group_depth_;
    if (session_.current_ != start_) {
        session_.publish(start_, Change{0, "cancel", {"cancel"}, {}});
    }
}

} // namespace gc::core
