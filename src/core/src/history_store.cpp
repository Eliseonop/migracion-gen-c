#include "gc/core/history_store.hpp"

#include <utility>

#include "gc/core/io.hpp"
#include "gc/core/serialize.hpp"

namespace gc::core {

namespace {

// timeline_from_json de una entrada; nullptr si no se puede leer.
TimelineSnapshot parse_entry(const Json& raw) {
    if (!raw.is_object()) {
        return nullptr;
    }
    auto tl = timeline_from_json(raw);
    return tl ? std::make_shared<const Timeline>(std::move(*tl)) : nullptr;
}

const Json& member(const Json& obj, const char* key) {
    static const Json kNull;
    if (!obj.is_object()) {
        return kNull;
    }
    const auto it = obj.find(key);
    return it == obj.end() ? kNull : *it;
}

} // namespace

Json history_to_json(const History& history) {
    Json undo = Json::array();
    for (const auto& tl : history.undo_stack()) {
        undo.push_back(to_json(*tl));
    }
    Json redo = Json::array();
    for (const auto& tl : history.redo_stack()) {
        redo.push_back(to_json(*tl));
    }
    Json checkpoints = Json::object();
    for (const auto& [name, tl] : history.checkpoint_states()) {
        checkpoints[name] = to_json(*tl);
    }
    Json out = Json::object();
    out["undo"] = std::move(undo);
    out["redo"] = std::move(redo);
    out["checkpoints"] = std::move(checkpoints);
    return out;
}

History history_from_json(const Json& raw, std::size_t limit) {
    std::deque<TimelineSnapshot> undo;
    std::vector<TimelineSnapshot> redo;
    History::Checkpoints checkpoints;
    if (const Json& u = member(raw, "undo"); u.is_array()) {
        for (const Json& e : u) {
            if (auto tl = parse_entry(e)) {
                undo.push_back(std::move(tl));
            }
        }
    }
    if (const Json& r = member(raw, "redo"); r.is_array()) {
        for (const Json& e : r) {
            if (auto tl = parse_entry(e)) {
                redo.push_back(std::move(tl));
            }
        }
    }
    if (const Json& c = member(raw, "checkpoints"); c.is_object()) {
        for (const auto& [name, e] : c.items()) {
            if (auto tl = parse_entry(e)) {
                checkpoints.emplace_back(name, std::move(tl));
            }
        }
    }
    return History(limit, std::move(undo), std::move(redo), std::move(checkpoints));
}

std::filesystem::path history_path(const std::filesystem::path& data_dir, std::string_view project_id) {
    return data_dir / "history" / path_from_utf8(std::string(project_id) + ".json");
}

History load_history(const std::filesystem::path& file, std::size_t limit) {
    std::error_code ec;
    if (!std::filesystem::exists(file, ec)) {
        return History(limit);
    }
    auto raw = read_json_file_py(file);
    return raw ? history_from_json(*raw, limit) : History(limit);
}

Result<void> save_history(const std::filesystem::path& file, const History& history) {
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    return write_json_file(file, history_to_json(history), -1);
}

// --- Autoguardado ------------------------------------------------------------------------

Autosave::Autosave(EditSession& session, std::filesystem::path history_file, SaveTimeline save_timeline)
    : session_(session), history_file_(std::move(history_file)), save_timeline_(std::move(save_timeline)) {
    if (save_timeline_) {
        timeline_listener_ = session_.subscribe(
            [this](const TimelineSnapshot& tl, const Change&) {
                ++writes_;
                note(timeline_error_, save_timeline_(*tl));
            });
    }
    history_listener_ =
        session_.subscribe_history([this](const History& h) {
            ++writes_;
            note(history_error_, save_history(history_file_, h));
        });
}

Autosave::~Autosave() {
    if (timeline_listener_ != 0) {
        session_.unsubscribe(timeline_listener_);
    }
    session_.unsubscribe_history(history_listener_);
}

void Autosave::note(std::optional<Error>& slot, const Result<void>& r) {
    if (r) {
        slot.reset();
    } else {
        slot = r.error();
    }
}

} // namespace gc::core
