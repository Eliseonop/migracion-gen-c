#include "editor_model.hpp"

#include <algorithm>
#include <cmath>

#include "gc/core/serialize.hpp"
#include "gc/core/timeline_rules.hpp"
#include "gc/core/timeline_view.hpp"

namespace gc::studio {

namespace {

constexpr int kRulerHeight = 22;
constexpr int kClipInset = 3;

QString qstr(const std::string& s) {
    return QString::fromStdString(s);
}

// defaultTracks() de editorModel.js: lo que abre un proyecto sin timeline.
core::Timeline default_timeline() {
    core::Timeline tl;
    for (const auto& [id, kind] :
         {std::pair{"V1", "video"}, {"V2", "video"}, {"A1", "audio"}, {"A2", "audio"}}) {
        core::TimelineTrack t;
        t.id = id;
        t.kind = kind;
        t.name = id;
        tl.tracks.push_back(std::move(t));
    }
    return tl;
}

QString clip_label(const core::TimelineClip& c) {
    const QString name = qstr(c.name.value_or(""));
    if (c.kind == "text") {
        return c.text && !c.text->empty() ? qstr(*c.text) : name;
    }
    if (c.kind == "adjustment" && name.isEmpty()) {
        return "Capa de ajuste";
    }
    return name.isEmpty() ? qstr(c.filename) : name;
}

QString speed_badge(const core::TimelineClip& c) {
    const double sp = core::ui_clip_speed(c);
    if (c.kind == "text" || sp == 1) {
        return {};
    }
    return std::fmod(sp, 1) == 0 ? QString("%1x").arg(sp) : QString::number(sp, 'f', 1) + "x";
}

} // namespace

EditorModel::EditorModel(core::ProjectsStore& store, QObject* parent) : QObject(parent), store_(store) {}

EditorModel::~EditorModel() = default;

template <class T>
bool EditorModel::ok(const Result<T>& r) {
    setError(r ? QString() : qstr(r.error().message));
    return r.has_value();
}

void EditorModel::setError(const QString& message) {
    if (message != error_) {
        error_ = message;
        emit errorChanged();
    }
}

bool EditorModel::openProject(const QString& pid) {
    const auto project = store_.get_project(pid.toStdString());
    if (!ok(project)) {
        return false;
    }
    if (!*project) {
        setError("Proyecto no encontrado.");
        return false;
    }
    project_id_ = (*project)->id;
    project_name_ = qstr((*project)->name);
    session_ = std::make_unique<core::EditSession>((*project)->timeline.value_or(default_timeline()));
    session_->subscribe([this](const core::TimelineSnapshot& tl, const core::Change&) {
        (void)ok(store_.save_timeline(project_id_, core::to_json(*tl)));
        rebuild();
    });
    selection_ = {};
    playhead_ = 0;
    rebuild();
    emit projectChanged();
    emit selectionChanged();
    emit playheadChanged();
    return true;
}

void EditorModel::closeProject() {
    session_.reset();
    project_id_.clear();
    project_name_.clear();
    tracks_.clear();
    clips_.clear();
    selection_ = {};
    emit projectChanged();
    emit timelineChanged();
    emit selectionChanged();
}

void EditorModel::rebuild() {
    tracks_.clear();
    clips_.clear();
    if (!session_) {
        emit timelineChanged();
        return;
    }
    const core::Timeline& tl = *session_->timeline();
    std::erase_if(selection_.ids, [&](const std::string& id) { return !clip(qstr(id)); });
    double y = kRulerHeight;
    for (const core::TimelineTrack* t : core::display_tracks(tl.tracks)) {
        const int h = core::track_row_height(t->kind, row_height_);
        tracks_.push_back(QVariantMap{{"id", qstr(t->id)},
                                      {"name", qstr(t->name)},
                                      {"kind", qstr(t->kind)},
                                      {"y", y},
                                      {"height", h},
                                      {"hidden", t->hidden},
                                      {"muted", t->muted},
                                      {"locked", t->locked},
                                      {"magnetic", t->magnetic}});
        for (const auto& c : tl.clips) {
            if (c->track_id != t->id) {
                continue;
            }
            clips_.push_back(
                QVariantMap{{"id", qstr(c->id)},
                            {"trackId", qstr(c->track_id)},
                            {"kind", qstr(c->kind)},
                            {"label", clip_label(*c)},
                            {"speed", speed_badge(*c)},
                            {"start", c->start},
                            {"duration", core::ui_clip_duration(*c)},
                            {"y", y + kClipInset},
                            {"height", h - 2 * kClipInset},
                            {"selected", std::ranges::find(selection_.ids, c->id) != selection_.ids.end()},
                            {"muted", c->muted},
                            {"disabled", c->disabled}});
        }
        y += h;
    }
    emit timelineChanged();
}

double EditorModel::duration() const {
    double end = 0;
    if (session_) {
        for (const auto& c : session_->timeline()->clips) {
            end = std::max(end, core::ui_clip_end(*c));
        }
    }
    return end;
}

int EditorModel::fps() const {
    return session_ ? core::normalize_fps(static_cast<double>(session_->timeline()->fps)) : 30;
}

double EditorModel::aspect() const {
    if (!session_ || session_->timeline()->height <= 0) {
        return 9.0 / 16.0;
    }
    return static_cast<double>(session_->timeline()->width) /
           static_cast<double>(session_->timeline()->height);
}

QStringList EditorModel::selection() const {
    QStringList out;
    for (const auto& id : selection_.ids) {
        out << qstr(id);
    }
    return out;
}

QVariantMap EditorModel::selectedClip() const {
    const core::TimelineClip* c = selection_.ids.empty() ? nullptr : clip(qstr(selection_.ids.front()));
    if (!c) {
        return {};
    }
    return {{"id", qstr(c->id)},
            {"kind", qstr(c->kind)},
            {"label", clip_label(*c)},
            {"trackId", qstr(c->track_id)},
            {"start", c->start},
            {"duration", core::ui_clip_duration(*c)},
            {"inPoint", c->in_point},
            {"outPoint", c->out_point},
            {"speed", core::ui_clip_speed(*c)},
            {"opacity", c->opacity.value_or(1.0)},
            {"muted", c->muted},
            {"disabled", c->disabled}};
}

void EditorModel::setPlayhead(double t) {
    const double clamped = std::max(0.0, t);
    if (clamped != playhead_) {
        playhead_ = clamped;
        emit playheadChanged();
    }
}

void EditorModel::setPps(double pps) {
    const double clamped = core::clamp_pps(pps, duration(), 900, fps());
    if (clamped != pps_) {
        pps_ = clamped;
        emit zoomChanged();
    }
}

void EditorModel::setRowHeight(int height) {
    const int clamped = core::clamp_row_height(height);
    if (clamped != row_height_) {
        row_height_ = clamped;
        rebuild();
    }
}

const core::TimelineClip* EditorModel::clip(const QString& id) const {
    if (!session_) {
        return nullptr;
    }
    const auto& clips = session_->timeline()->clips;
    const auto it = std::ranges::find_if(clips, [&](const auto& c) { return qstr(c->id) == id; });
    return it == clips.end() ? nullptr : &**it;
}

const core::TimelineTrack* EditorModel::track(const QString& id) const {
    if (!session_) {
        return nullptr;
    }
    const auto& tracks = session_->timeline()->tracks;
    const auto it = std::ranges::find_if(tracks, [&](const auto& t) { return qstr(t.id) == id; });
    return it == tracks.end() ? nullptr : &*it;
}

bool EditorModel::canDrop(const core::TimelineClip& c, const core::TimelineTrack& t) const {
    const auto fits = core::clip_fits_track(c.kind, t.kind);
    return !t.locked && fits && *fits;
}

void EditorModel::select(const QString& id, bool additive, bool range) {
    if (!session_) {
        return;
    }
    selection_ = core::next_clip_selection(session_->timeline()->clips, selection_, id.toStdString(),
                                           {.additive = additive, .range = range, .keep_group = false});
    rebuild();
    emit selectionChanged();
}

void EditorModel::clearSelection() {
    selection_ = {};
    rebuild();
    emit selectionChanged();
}

QVariantMap EditorModel::previewMove(const QString& id, double start, const QString& trackId) const {
    const core::TimelineClip* c = clip(id);
    const core::TimelineTrack* t = track(trackId);
    if (!c) {
        return {};
    }
    if (!t || !canDrop(*c, *t)) {
        t = track(qstr(c->track_id));
    }
    const std::string self[] = {c->id};
    const double at = core::clamp_start_no_overlap(session_->timeline()->clips, t->id, start,
                                                   core::ui_clip_duration(*c), self);
    return {{"start", at}, {"trackId", qstr(t->id)}};
}

void EditorModel::moveClip(const QString& id, double start, const QString& trackId) {
    const QVariantMap to = previewMove(id, start, trackId);
    const core::TimelineClip* c = clip(id);
    if (!c || (to["start"].toDouble() == c->start && to["trackId"].toString() == qstr(c->track_id))) {
        return;
    }
    apply(core::ops::MoveClip{c->id, to["start"].toDouble(), to["trackId"].toString().toStdString()});
}

QVariantMap EditorModel::previewTrim(const QString& id, bool left, double delta) const {
    const core::TimelineClip* c = clip(id);
    if (!c) {
        return {};
    }
    const auto edge = left ? core::TrimEdge::Left : core::TrimEdge::Right;
    core::TimelineClip next = *c;
    core::trim_clip_patch(*c, edge, core::clamp_trim_delta(session_->timeline()->clips, *c, edge, delta))
        .apply(next);
    return {{"start", next.start}, {"duration", core::ui_clip_duration(next)}};
}

void EditorModel::trimClip(const QString& id, bool left, double delta) {
    const core::TimelineClip* c = clip(id);
    if (!c || delta == 0) {
        return;
    }
    const auto edge = left ? core::TrimEdge::Left : core::TrimEdge::Right;
    core::Timeline next = *session_->timeline();
    for (auto& item : next.clips) {
        if (item->id == c->id) {
            core::trim_clip_patch(*c, edge, core::clamp_trim_delta(next.clips, *c, edge, delta))
                .apply(item.mut());
        }
    }
    session_->replace(std::move(next), left ? "trim-left" : "trim-right", {c->id});
}

void EditorModel::apply(const core::Command& command) {
    if (session_) {
        (void)ok(session_->apply(command));
    }
}

// splitClip de VideoEditor.jsx: solo los clips seleccionados que pisa el cabezal; la selección
// pasa a la mitad izquierda.
void EditorModel::splitAtPlayhead() {
    if (!session_) {
        return;
    }
    core::ClipSelection next = selection_; // rebuild() poda selection_ en cada paso
    core::EditSession::Group group(*session_);
    for (std::string& id : next.ids) {
        const core::TimelineClip* c = clip(qstr(id));
        if (!c || playhead_ <= c->start || playhead_ >= core::ui_clip_end(*c)) {
            continue;
        }
        const auto change = session_->apply(core::ops::SplitClip{id, playhead_});
        if (!ok(change)) {
            return;
        }
        if (next.anchor == id) {
            next.anchor = change->changed.front();
        }
        id = change->changed.front();
    }
    group.commit();
    selection_ = std::move(next);
    rebuild();
    emit selectionChanged();
}

void EditorModel::deleteSelection() {
    if (!session_ || selection_.ids.empty()) {
        return;
    }
    const auto ids = selection_.ids;
    core::EditSession::Group group(*session_);
    for (const auto& id : ids) {
        if (!ok(session_->apply(core::ops::RemoveClip{id}))) {
            return;
        }
    }
    group.commit();
    selection_ = {};
    emit selectionChanged();
}

void EditorModel::duplicateSelection() {
    if (!session_) {
        return;
    }
    const auto ids = selection_.ids;
    core::EditSession::Group group(*session_);
    for (const auto& id : ids) {
        if (!ok(session_->apply(core::ops::DuplicateClip{id, std::nullopt}))) {
            return;
        }
    }
    group.commit();
}

void EditorModel::undo() {
    if (session_) {
        session_->undo();
    }
}

void EditorModel::redo() {
    if (session_) {
        session_->redo();
    }
}

void EditorModel::zoomBy(double factor, double viewWidth) {
    const double next = core::clamp_pps(pps_ * factor, duration(), viewWidth, fps());
    if (next != pps_) {
        pps_ = next;
        emit zoomChanged();
    }
}

void EditorModel::zoomToFit(double viewWidth) {
    const double span = std::max(duration(), 1.0);
    pps_ = core::clamp_pps(viewWidth * 0.95 / span, duration(), viewWidth, fps());
    emit zoomChanged();
}

QVariantList EditorModel::ticks(double scrollX, double viewWidth) const {
    QVariantList out;
    const double max_t = std::max(duration() + 30, viewWidth / pps_ + scrollX / pps_);
    for (const core::Tick& k : core::build_ticks(max_t, pps_, fps(), scrollX, viewWidth)) {
        out.push_back(
            QVariantMap{{"t", k.t},
                        {"x", k.t * pps_},
                        {"major", k.major},
                        {"label", k.major ? qstr(core::fmt_ruler(k.t, fps(), k.step)) : QString()}});
    }
    return out;
}

QString EditorModel::formatTime(double t) const {
    return qstr(core::fmt_ruler(t, fps(), 1.0 / fps()));
}

} // namespace gc::studio
