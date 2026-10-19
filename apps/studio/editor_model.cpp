#include "editor_model.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <utility>

#include "gc/core/no_overlap.hpp"
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
    selection_ = {};
    rebuild();
    emit projectChanged();
    emit timelineChanged();
    emit selectionChanged();
}

void EditorModel::rebuild() {
    tracks_.clear();
    QVector<QVariantMap> rows;
    if (session_) {
        const core::Timeline& tl = *session_->timeline();
        std::erase_if(selection_.ids, [&](const std::string& id) { return !clip(qstr(id)); });
        std::unordered_map<std::string, std::pair<double, int>> rows_of; // pista → (y, alto)
        double y = kRulerHeight;
        for (const core::TimelineTrack* t : core::display_tracks(tl.tracks)) {
            const int h = core::track_row_height(t->kind, row_height_);
            rows_of[t->id] = {y, h};
            tracks_.push_back(QVariantMap{{"id", qstr(t->id)},
                                          {"name", qstr(t->name)},
                                          {"kind", qstr(t->kind)},
                                          {"y", y},
                                          {"height", h},
                                          {"hidden", t->hidden},
                                          {"muted", t->muted},
                                          {"locked", t->locked},
                                          {"magnetic", t->magnetic}});
            y += h;
        }
        for (const auto& c : tl.clips) { // en el orden del array: el de después queda delante
            const auto row = rows_of.find(c->track_id);
            if (row == rows_of.end()) {
                continue;
            }
            rows.push_back({{"clipId", qstr(c->id)},
                            {"trackId", qstr(c->track_id)},
                            {"kind", qstr(c->kind)},
                            {"label", clip_label(*c)},
                            {"speed", speed_badge(*c)},
                            {"start", c->start},
                            {"duration", core::ui_clip_duration(*c)},
                            {"clipY", row->second.first + kClipInset},
                            {"clipHeight", row->second.second - 2 * kClipInset},
                            {"muted", c->muted},
                            {"disabled", c->disabled}});
        }
    }
    clip_model_.sync(std::move(rows));
    emit timelineChanged();
}

QVariantList EditorModel::clips() const {
    QVariantList out;
    for (const QVariantMap& row : clip_model_.rows()) {
        out << row;
    }
    return out;
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
    emit selectionChanged(); // sin rebuild(): rehacer la lista recrearía el clip que se está pulsando
}

void EditorModel::clearSelection() {
    selection_ = {};
    emit selectionChanged();
}

struct EditorModel::DragPlan {
    bool group = false;
    double delta = 0;
    core::Drop land;
    std::vector<double> times;
};

std::vector<core::AlignClip> EditorModel::snapOthers(std::span<const std::string> moving,
                                                     bool beats_too) const {
    const core::Timeline& tl = *session_->timeline();
    auto out = core::align_others(tl.clips, moving);
    for (auto& t :
         core::snap_targets(tl.markers, tl.clips, beats_too ? moving : std::span<const std::string>{})) {
        out.push_back(std::move(t));
    }
    return out;
}

// EdTimeline.startDrag (modo move): un grupo se mueve junto y se para contra el primer vecino; un
// clip suelto va con el imán y cae donde diga dropTarget (otra pista o una nueva).
std::optional<EditorModel::DragPlan> EditorModel::planDrag(const QString& id, double delta,
                                                           double centerY) const {
    const core::TimelineClip* c = clip(id);
    if (!c) {
        return std::nullopt;
    }
    const core::Timeline& tl = *session_->timeline();
    const double thr = core::align_threshold_sec(pps_);
    const bool group =
        selection_.ids.size() > 1 && std::ranges::find(selection_.ids, c->id) != selection_.ids.end();
    if (group) {
        std::vector<const core::TimelineClip*> origs;
        for (const auto& sid : selection_.ids) {
            if (const core::TimelineClip* o = clip(qstr(sid))) {
                origs.push_back(o);
            }
        }
        const auto snapped = core::snap_clip_group(origs, delta, snapOthers(selection_.ids, true), thr);
        const double dd = core::clamp_group_delta(tl.clips, origs, snapped.delta, tl.tracks);
        return DragPlan{true, dd, {}, dd == snapped.delta ? snapped.times : std::vector<double>{}};
    }
    const std::string self[] = {c->id};
    const auto snapped = core::snap_clip_move(*c, delta, snapOthers(self, true), thr);
    std::vector<core::TrackRow> geo;
    for (const QVariant& v : tracks_) {
        const QVariantMap t = v.toMap();
        geo.push_back({t["id"].toString().toStdString(), t["y"].toDouble(),
                       t["y"].toDouble() + t["height"].toDouble()});
    }
    const bool alone =
        std::ranges::count(tl.clips, c->track_id, [](const auto& x) { return x->track_id; }) == 1;
    const std::string kind = core::lane_kind(c->kind);
    const auto land = core::drop_target(tl.tracks, tl.clips, geo,
                                        {.kind = kind,
                                         .y = centerY,
                                         .time = snapped.start,
                                         .duration = core::ui_clip_duration(*c),
                                         .exclude = self,
                                         .origin_id = c->track_id,
                                         .origin_alone = alone,
                                         .pps = pps_,
                                         .snap_sec = thr});
    if (!land) {
        return std::nullopt;
    }
    return DragPlan{false, 0, *land, land->start == snapped.start ? snapped.times : std::vector<double>{}};
}

QVariantMap EditorModel::previewDrag(const QString& id, double delta, double centerY) const {
    const auto plan = planDrag(id, delta, centerY);
    if (!plan) {
        return {};
    }
    QVariantList times;
    for (const double t : plan->times) {
        times << t;
    }
    if (plan->group) {
        return {{"group", true}, {"delta", plan->delta}, {"times", times}};
    }
    return {{"group", false},
            {"start", plan->land.start},
            {"trackId", plan->land.track_id ? qstr(*plan->land.track_id) : QString()},
            {"newTrack", plan->land.slot.has_value()},
            {"lineY", plan->land.line_y},
            {"times", times}};
}

void EditorModel::dropDrag(const QString& id, double delta, double centerY) {
    const auto plan = planDrag(id, delta, centerY);
    const core::TimelineClip* c = clip(id);
    if (!plan || !c) {
        return;
    }
    core::Timeline next = *session_->timeline();
    if (plan->group) {
        if (plan->delta == 0) {
            return;
        }
        for (auto& item : next.clips) {
            if (std::ranges::find(selection_.ids, item->id) != selection_.ids.end()) {
                item.mut().start = core::js_fixed3(item->start + plan->delta);
            }
        }
        commit(std::move(next), "move-group", selection_.ids);
        return;
    }
    std::string track_id = plan->land.track_id.value_or(c->track_id);
    if (plan->land.slot) { // moveClipToNewTrack: la pista nueva se crea al soltar
        core::TimelineTrack t =
            core::build_track(core::lane_kind(c->kind), next.tracks, ids_.hex32().substr(0, 8));
        if (const core::TimelineTrack* src = track(qstr(c->track_id));
            t.kind == "text" && src && src->style) {
            t.style = src->style;
        }
        track_id = t.id;
        const auto at = static_cast<std::ptrdiff_t>(core::slot_index(next.tracks, *plan->land.slot));
        next.tracks.insert(next.tracks.begin() + at, std::move(t));
    } else if (track_id == c->track_id && plan->land.start == c->start) {
        return;
    }
    for (auto& item : next.clips) {
        if (item->id == c->id) {
            item.mut().track_id = track_id;
            item.mut().start =
                plan->land.slot ? core::js_fixed3(std::max(0.0, plan->land.start)) : plan->land.start;
        }
    }
    commit(std::move(next), "move", {c->id}, {c->track_id});
}

void EditorModel::moveClip(const QString& id, double start, const QString& trackId) {
    const core::TimelineClip* c = clip(id);
    const core::TimelineTrack* t = track(trackId);
    if (c && t && canDrop(*c, *t)) {
        apply(core::ops::MoveClip{c->id, start, t->id});
    }
}

// previewTrim de EdTimeline: el borde no pasa del vecino (salvo en una pista magnética, que se
// vuelve a empaquetar) y el imán tampoco puede saltárselo.
std::optional<core::ClipPatch> EditorModel::planTrim(const QString& id, bool left, double delta,
                                                     std::vector<double>* times) const {
    const core::TimelineClip* c = clip(id);
    if (!c) {
        return std::nullopt;
    }
    const auto& clips = session_->timeline()->clips;
    const core::TimelineTrack* t = track(qstr(c->track_id));
    const auto edge = left ? core::TrimEdge::Left : core::TrimEdge::Right;
    const auto bound = [&](double d) {
        return t && t->magnetic ? d : core::clamp_trim_delta(clips, *c, edge, d);
    };
    const double d = bound(delta);
    const std::string self[] = {c->id};
    auto snap = core::snap_clip_trim(*c, edge, d, snapOthers(self, false), core::align_threshold_sec(pps_));
    if (bound(snap.delta) == snap.delta) {
        if (times) {
            *times = std::move(snap.times);
        }
        return snap.patch;
    }
    return core::trim_clip_patch(*c, edge, d);
}

QVariantMap EditorModel::previewTrim(const QString& id, bool left, double delta) const {
    std::vector<double> guide;
    const auto patch = planTrim(id, left, delta, &guide);
    if (!patch) {
        return {};
    }
    core::TimelineClip next = *clip(id);
    patch->apply(next);
    QVariantList times;
    for (const double t : guide) {
        times << t;
    }
    return {{"start", next.start}, {"duration", core::ui_clip_duration(next)}, {"times", times}};
}

void EditorModel::trimClip(const QString& id, bool left, double delta) {
    const auto patch = planTrim(id, left, delta);
    if (!patch || delta == 0) {
        return;
    }
    core::Timeline next = *session_->timeline();
    for (auto& item : next.clips) {
        if (qstr(item->id) == id) {
            patch->apply(item.mut());
        }
    }
    commit(std::move(next), left ? "trim-left" : "trim-right", {id.toStdString()});
}

void EditorModel::commit(core::Timeline next, std::string cause, std::vector<std::string> changed,
                         std::vector<std::string> emptied) {
    if (auto settled = core::settle_timeline(*session_->timeline(), next, ids_)) {
        next = std::move(settled->timeline);
    }
    for (const auto& gone : core::emptied_tracks(next.tracks, next.clips, emptied)) {
        std::erase_if(next.tracks, [&](const core::TimelineTrack& t) { return t.id == gone; });
        for (auto& t : next.tracks) {
            if (t.linked_track_id == gone) {
                t.linked_track_id.reset();
            }
        }
    }
    session_->replace(std::move(next), std::move(cause), std::move(changed));
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

// deleteClip de VideoEditor.jsx: fuera los clips, y sus pistas si se quedan vacías.
void EditorModel::deleteSelection() {
    if (!session_ || selection_.ids.empty()) {
        return;
    }
    core::Timeline next = *session_->timeline();
    std::vector<std::string> tracks;
    for (const auto& c : next.clips) {
        if (std::ranges::find(selection_.ids, c->id) != selection_.ids.end()) {
            tracks.push_back(c->track_id);
        }
    }
    std::erase_if(next.clips, [&](const auto& c) {
        return std::ranges::find(selection_.ids, c->id) != selection_.ids.end();
    });
    const auto ids = std::exchange(selection_.ids, {});
    selection_.anchor.reset();
    commit(std::move(next), "delete", ids, std::move(tracks));
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
