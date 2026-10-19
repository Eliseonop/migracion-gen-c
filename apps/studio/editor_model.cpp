#include "editor_model.hpp"

#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <utility>

#include "gc/core/clip_layout_ui.hpp"
#include "gc/core/clip_pose_edit.hpp"
#include "gc/core/keyframes.hpp"
#include "gc/core/material_drop.hpp"
#include "gc/core/no_overlap.hpp"
#include "gc/core/pose.hpp"
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

// Puntos de keyframe del clip (ClipBlock de EdTimeline): fracción 0–1 de su ancho. Los de pose van
// en tiempo local; sin ellos, los del encuadre, que van en tiempo de la fuente.
QVariantList keyframe_marks(const core::TimelineClip& c) {
    QVariantList out;
    const double dur = core::ui_clip_duration(c);
    if (core::keyframes_enabled(c) && c.keyframes->contains("items") && c.keyframes->at("items").is_array()) {
        for (const Json& k : c.keyframes->at("items")) {
            if (k.is_object() && k.contains("t") && k.at("t").is_number()) {
                out << k.at("t").get<double>() / (dur > 0 ? dur : 1);
            }
        }
    } else if (c.reframe && (c.kind == "video" || c.kind == "image")) {
        const double src = std::max(0.0, c.out_point - c.in_point);
        for (const core::Keyframe& k : c.reframe->keyframes) {
            out << (k.t - c.in_point) / (src > 0 ? src : 1);
        }
    }
    return out;
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
    materials_.clear();
    payloads_.clear();
    const auto add = [&](const char* asset_kind, const QString& key, const Json& item, const char* kind) {
        const Json payload = core::drag_payload(asset_kind, item);
        payloads_[key] = payload;
        materials_ << QVariantMap{
            {"key", key},
            {"assetKind", asset_kind},
            {"kind", kind},
            {"name", qstr(payload.value("name", Json("")).is_string() ? payload.value("name", "") : "")},
            {"duration", core::js_number(payload.value("duration", Json(0)))}};
    };
    for (const auto& c : (*project)->clips) {
        add("clips", QString("clips:%1").arg(c.index), core::to_json(c), "video");
    }
    for (const auto& i : (*project)->images) {
        add("images", "images:" + qstr(i.id), core::to_json(i), "image");
    }
    for (const auto& a : (*project)->audios) {
        add("audios", "audios:" + qstr(a.id), core::to_json(a), "audio");
    }
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
        std::unordered_map<std::string, bool> locked;
        double y = kRulerHeight;
        for (const core::TimelineTrack* t : core::display_tracks(tl.tracks)) {
            const int h = core::track_row_height(t->kind, row_height_);
            rows_of[t->id] = {y, h};
            locked[t->id] = t->locked;
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
                            {"disabled", c->disabled},
                            {"locked", locked[c->track_id]},
                            {"kfs", keyframe_marks(*c)}});
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
    const double local = playhead_ - c->start;
    const core::Pose pose = core::clip_pose(*c, std::max(0.0, local));
    const auto& tl = *session_->timeline();
    const core::TransformView tv = core::transform_view(*c, local, tl.width, tl.height);
    return {
        {"transform", QVariantMap{{"show", tv.show},
                                  {"xy", tv.xy},
                                  {"scale", tv.scale || tv.zoom_scale},
                                  {"zoomScale", tv.zoom_scale},
                                  {"rotation", tv.rotation},
                                  {"capcut", tv.capcut},
                                  {"x", tv.x},
                                  {"y", tv.y},
                                  {"scalePct", tv.scale_pct},
                                  {"degrees", tv.rotation_deg}}},
        {"keepPitch", c->keep_pitch},
        {"reverse", c->reverse},
        {"poseOpacity", pose.opacity},
        {"volumeAt", core::clip_volume_at(*c, std::max(0.0, local))},
        {"blend", qstr(c->blend_mode.value_or("normal"))},
        {"appear", qstr(c->appear)},
        {"exit", qstr(c->exit)},
        {"note", qstr(c->note.value_or(""))},
        {"animated", core::keyframes_enabled(*c) && core::pose_keyframed(*c)},
        {"kf", qstr(core::kf_state(*c, std::min(core::ui_clip_duration(*c), std::max(0.0, local)), fps()))},
        {"id", qstr(c->id)},
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
        if (!selection_.ids.empty()) {
            emit selectionChanged(); // la pose del inspector es la del cabezal
        }
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

void EditorModel::selectInRect(double x0, double y0, double x1, double y1, bool additive) {
    if (!session_) {
        return;
    }
    const double left = std::min(x0, x1);
    const double right = std::max(x0, x1);
    const double top = std::min(y0, y1);
    const double bottom = std::max(y0, y1);
    std::vector<std::string> ids = additive ? selection_.ids : std::vector<std::string>{};
    for (const QVariantMap& row : clip_model_.rows()) {
        const double cx0 = row["start"].toDouble() * pps_;
        const double cx1 = cx0 + std::max(6.0, row["duration"].toDouble() * pps_);
        const double cy0 = row["clipY"].toDouble();
        const double cy1 = cy0 + row["clipHeight"].toDouble();
        const std::string id = row["clipId"].toString().toStdString();
        if (right >= cx0 && left <= cx1 && bottom >= cy0 && top <= cy1 &&
            std::ranges::find(ids, id) == ids.end()) {
            ids.push_back(id);
        }
    }
    selection_.anchor = ids.empty() ? std::nullopt : std::optional(ids.back());
    selection_.ids = std::move(ids);
    emit selectionChanged();
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

// materialTarget de EdTimeline.jsx: el material empieza en el puntero, con el imán, y cae donde
// diga dropTarget.
std::optional<std::pair<Json, core::Drop>> EditorModel::planMaterialDrop(const QString& key, double x,
                                                                         double y, bool alt) const {
    const auto it = payloads_.find(key);
    if (!session_ || it == payloads_.end()) {
        return std::nullopt;
    }
    const Json& payload = it->second;
    const core::Timeline& tl = *session_->timeline();
    const double d = core::js_number(payload.value("duration", Json(0)));
    const double span = std::max(0.1, std::isnan(d) || d == 0 ? 1 : d);
    const double pointer = x / pps_;
    const double thr = core::align_threshold_sec(pps_);
    core::TimelineClip ghost;
    ghost.id = "__drop__";
    ghost.start = pointer;
    ghost.out_point = span;
    const auto snapped = core::snap_clip_move(ghost, 0, snapOthers({}, false), thr);
    std::vector<core::TrackRow> geo;
    for (const QVariant& v : tracks_) {
        const QVariantMap t = v.toMap();
        geo.push_back({t["id"].toString().toStdString(), t["y"].toDouble(),
                       t["y"].toDouble() + t["height"].toDouble()});
    }
    const std::string kind = core::lane_kind_for_asset(payload.value("asset_kind", std::string()));
    const auto land = core::drop_target(tl.tracks, tl.clips, geo,
                                        {.kind = kind,
                                         .y = y,
                                         .time = snapped.start,
                                         .duration = span,
                                         .pointer_time = pointer,
                                         .replace = alt,
                                         .pps = pps_,
                                         .snap_sec = thr});
    if (!land) {
        return std::nullopt;
    }
    return std::pair{payload, *land};
}

QVariantMap EditorModel::previewMaterialDrop(const QString& key, double x, double y, bool alt) const {
    const auto plan = planMaterialDrop(key, x, y, alt);
    if (!plan) {
        return {};
    }
    const core::Drop& land = plan->second;
    const double d = core::js_number(plan->first.value("duration", Json(0)));
    return {{"start", land.start},
            {"trackId", land.track_id ? qstr(*land.track_id) : QString()},
            {"newTrack", land.slot.has_value()},
            {"lineY", land.line_y},
            {"replace", land.replace_id.has_value()},
            {"duration", std::max(0.1, std::isnan(d) || d == 0 ? 1 : d)}};
}

bool EditorModel::altHeld() { return QGuiApplication::keyboardModifiers().testFlag(Qt::AltModifier); }

void EditorModel::dropMaterial(const QString& key, double x, double y, bool alt) {
    const auto plan = planMaterialDrop(key, x, y, alt);
    if (!plan) {
        return;
    }
    const auto& [payload, land] = *plan;
    core::Timeline next = *session_->timeline();
    std::string track_id = land.track_id.value_or("");
    std::optional<double> fit;
    if (land.slot) { // resolveDropTarget: la pista nueva se crea ahora
        core::TimelineTrack t =
            core::build_track(core::lane_kind_for_asset(payload.value("asset_kind", std::string())),
                              next.tracks, ids_.hex32().substr(0, 8));
        track_id = t.id;
        next.tracks.insert(next.tracks.begin() +
                               static_cast<std::ptrdiff_t>(core::slot_index(next.tracks, *land.slot)),
                           std::move(t));
    } else if (land.replace_id) {
        if (const core::TimelineClip* old = clip(qstr(*land.replace_id))) {
            fit = core::ui_clip_duration(*old);
        }
        std::erase_if(next.clips, [&](const auto& c) { return c->id == *land.replace_id; });
    }
    auto made = core::clip_from_json(core::place_asset_clip(payload, track_id, land.start, fit, ids_));
    if (!ok(made)) {
        return;
    }
    const std::string id = made->id;
    next.clips.emplace_back(std::move(*made));
    selection_ = {{id}, id};
    commit(std::move(next), "drop-material", {id});
    emit selectionChanged();
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

QVariantList EditorModel::markers() const {
    QVariantList out;
    if (!session_) {
        return out;
    }
    const auto text = [](const Json& m, const char* key, const char* fallback) {
        return qstr(m.contains(key) && m.at(key).is_string() ? m.at(key).get<std::string>() : fallback);
    };
    for (const Json& m : session_->timeline()->markers) {
        if (m.is_object() && m.contains("t") && m.at("t").is_number()) {
            out << QVariantMap{{"id", text(m, "id", "")},
                               {"t", m.at("t").get<double>()},
                               {"label", text(m, "label", "")},
                               {"color", text(m, "color", "#38bdf8")}};
        }
    }
    return out;
}

void EditorModel::toggleDisabled() {
    if (!session_ || selection_.ids.empty()) {
        return;
    }
    const core::TimelineClip* primary = clip(qstr(selection_.anchor.value_or(selection_.ids.front())));
    const bool off = !(primary && primary->disabled);
    core::Timeline next = *session_->timeline();
    for (auto& c : next.clips) {
        if (std::ranges::find(selection_.ids, c->id) != selection_.ids.end()) {
            c.mut().disabled = off;
        }
    }
    commit(std::move(next), "disable", selection_.ids);
}

void EditorModel::toggleMarker() {
    if (!session_) {
        return;
    }
    core::Timeline next = *session_->timeline();
    next.markers = core::toggle_marker_at(next.markers, playhead_, fps(), ids_);
    session_->replace(std::move(next), "markers", {});
}

void EditorModel::jumpSnap(int dir) {
    if (session_) {
        if (const auto t = core::next_snap_time(session_->timeline()->markers, session_->timeline()->clips,
                                                playhead_, dir)) {
            setPlayhead(*t);
        }
    }
}

void EditorModel::nudgePlayhead(double dt) {
    setPlayhead(std::clamp(playhead_ + dt, 0.0, duration()));
}

void EditorModel::toggleTrack(const QString& id, const QString& prop) {
    if (!track(id)) {
        return;
    }
    core::Timeline next = *session_->timeline();
    for (auto& t : next.tracks) {
        if (qstr(t.id) != id) {
            continue;
        }
        bool& flag = prop == "hidden"   ? t.hidden
                     : prop == "muted"  ? t.muted
                     : prop == "locked" ? t.locked
                                        : t.magnetic;
        flag = !flag;
    }
    commit(std::move(next), "track-" + prop.toStdString(), {id.toStdString()});
}

void EditorModel::renameTrack(const QString& id, const QString& name) {
    const core::TimelineTrack* t = track(id);
    const QString n = name.trimmed().left(3);
    if (t && !n.isEmpty() && n != qstr(t->name)) {
        apply(core::ops::RenameTrack{t->id, n.toStdString()});
    }
}

// compactTrack de VideoEditor.jsx: los clips de la pista uno tras otro desde 0, en su orden.
void EditorModel::compactTrack(const QString& id) {
    const core::TimelineTrack* t = track(id);
    if (!t || t->locked) {
        return;
    }
    core::Timeline next = *session_->timeline();
    std::vector<Cow<core::TimelineClip>*> row;
    for (auto& c : next.clips) {
        if (c->track_id == t->id) {
            row.push_back(&c);
        }
    }
    std::ranges::stable_sort(row, {}, [](const Cow<core::TimelineClip>* c) { return (*c)->start; });
    double cursor = 0;
    for (auto* c : row) {
        const double start = core::js_fixed3(cursor);
        cursor += core::ui_clip_duration(**c);
        if ((*c)->start != start) {
            c->mut().start = start;
        }
    }
    commit(std::move(next), "compact", {t->id});
}

void EditorModel::reorderTrack(const QString& id, const QString& targetId, bool below) {
    if (track(id) && track(targetId) && id != targetId) {
        apply(core::ops::ReorderTrack{id.toStdString(), targetId.toStdString(), below ? "below" : "above"});
    }
}

void EditorModel::setPose(const QVariantMap& patch) {
    if (!session_ || selection_.ids.empty()) {
        return;
    }
    const Json json = Json::parse(QJsonDocument(QJsonObject::fromVariantMap(patch)).toJson().toStdString());
    core::Timeline next = *session_->timeline();
    for (auto& c : next.clips) {
        if (std::ranges::find(selection_.ids, c->id) != selection_.ids.end()) {
            core::commit_pose(c.mut(), json, playhead_ - c->start, fps(), ids_);
        }
    }
    commit(std::move(next), "pose", selection_.ids);
}

template <class Patch>
void EditorModel::pose_each(const char* cause, Patch patch) {
    if (!session_ || selection_.ids.empty()) {
        return;
    }
    core::Timeline next = *session_->timeline();
    for (auto& c : next.clips) {
        if (std::ranges::find(selection_.ids, c->id) != selection_.ids.end() && c->kind != "audio") {
            core::commit_pose(c.mut(), patch(*c), playhead_ - c->start, fps(), ids_);
        }
    }
    commit(std::move(next), cause, selection_.ids);
}

void EditorModel::setPosition(double x, double y) {
    const auto& tl = *session_->timeline();
    pose_each("transform", [&](const core::TimelineClip& c) {
        return core::transform_pos_patch(c, x, y, tl.width, tl.height);
    });
}

void EditorModel::setScalePct(double pct) {
    pose_each("transform", [&](const core::TimelineClip& c) { return core::transform_scale_patch(c, pct); });
}

void EditorModel::setRotation(double deg) {
    pose_each("transform", [&](const core::TimelineClip&) { return Json{{"rotation", deg}}; });
}

void EditorModel::resetTransform() {
    pose_each("transform", [](const core::TimelineClip& c) { return core::transform_reset_patch(c); });
}

template <class Edit>
void EditorModel::fx_each(const char* cause, Edit edit) {
    if (!session_ || selection_.ids.empty()) {
        return;
    }
    core::Timeline next = *session_->timeline();
    for (auto& c : next.clips) {
        if (std::ranges::find(selection_.ids, c->id) != selection_.ids.end() && c->kind != "text") {
            edit(c.mut());
        }
    }
    commit(std::move(next), cause, selection_.ids);
}

void EditorModel::setSpeed(double speed) {
    fx_each("speed", [&](core::TimelineClip& c) { c.speed = std::clamp(speed, 0.1, 10.0); });
}

void EditorModel::toggleKeepPitch() {
    const core::TimelineClip* c = selection_.ids.empty() ? nullptr : clip(qstr(selection_.ids.front()));
    if (c) {
        fx_each("speed", [keep = !c->keep_pitch](core::TimelineClip& item) { item.keep_pitch = keep; });
    }
}

void EditorModel::toggleReverse() {
    const core::TimelineClip* c = selection_.ids.empty() ? nullptr : clip(qstr(selection_.ids.front()));
    if (c) {
        fx_each("speed", [reverse = !c->reverse](core::TimelineClip& item) { item.reverse = reverse; });
    }
}

void EditorModel::setBlend(const QString& mode) {
    if (!session_ || selection_.ids.empty()) {
        return;
    }
    core::Timeline next = *session_->timeline();
    for (auto& c : next.clips) {
        if (c->kind != "audio" && std::ranges::find(selection_.ids, c->id) != selection_.ids.end()) {
            c.mut().blend_mode =
                mode.isEmpty() || mode == "normal" ? std::nullopt : std::optional(mode.toStdString());
        }
    }
    commit(std::move(next), "blend", selection_.ids);
}

void EditorModel::setTransition(const QString& appear, const QString& exit) {
    if (!session_ || selection_.ids.empty()) {
        return;
    }
    const auto ids = selection_.ids;
    core::EditSession::Group group(*session_);
    for (const auto& id : ids) {
        if (!ok(session_->apply(
                core::ops::SetClipTransition{id, appear.toStdString(), exit.toStdString()}))) {
            return;
        }
    }
    group.commit();
}

// setClipNote de VideoEditor.jsx: la nota (sin espacios alrededor) y su origen; vacía la quita.
void EditorModel::setNote(const QString& note) {
    const core::TimelineClip* c = selection_.ids.empty() ? nullptr : clip(qstr(selection_.ids.front()));
    if (!c) {
        return;
    }
    const std::string text = note.trimmed().toStdString();
    if (text == c->note.value_or("")) {
        return;
    }
    core::Timeline next = *session_->timeline();
    for (auto& item : next.clips) {
        if (item->id == c->id) {
            item.mut().note = text.empty() ? std::nullopt : std::optional(text);
            item.mut().note_source = text.empty() ? std::nullopt : std::optional<std::string>("user");
        }
    }
    commit(std::move(next), "note", {c->id});
}

bool EditorModel::copySelection() {
    if (!session_) {
        return false;
    }
    std::vector<const core::TimelineClip*> chosen;
    for (const auto& c : session_->timeline()->clips) {
        if (std::ranges::find(selection_.ids, c->id) != selection_.ids.end()) {
            chosen.push_back(&*c);
        }
    }
    if (chosen.empty()) {
        return false;
    }
    double base = chosen.front()->start;
    for (const auto* c : chosen) {
        base = std::min(base, c->start);
    }
    clipboard_.clear();
    for (const auto* c : chosen) {
        clipboard_.emplace_back(*c, c->start - base);
    }
    return true;
}

void EditorModel::cutSelection() {
    if (copySelection()) {
        deleteSelection();
    }
}

bool EditorModel::paste() {
    if (!session_ || clipboard_.empty()) {
        return false;
    }
    core::Timeline next = *session_->timeline();
    std::vector<std::string> ids;
    for (const auto& [source, offset] : clipboard_) {
        core::TimelineClip copy = source;
        if (!std::ranges::any_of(next.tracks, [&](const auto& t) { return t.id == copy.track_id; })) {
            const std::string lane = core::lane_kind(copy.kind);
            const auto fallback =
                std::ranges::find_if(next.tracks, [&](const auto& t) { return t.kind == lane && !t.locked; });
            if (fallback == next.tracks.end()) {
                continue;
            }
            copy.track_id = fallback->id;
        }
        copy.dup_of = source.dup_of.value_or(source.id); // lineageRoot
        copy.id = core::make_uid(ids_, "c");
        copy.start = core::js_fixed3(std::max(0.0, playhead_) + offset);
        ids.push_back(copy.id);
        next.clips.emplace_back(std::move(copy));
    }
    if (ids.empty()) {
        return false;
    }
    selection_ = {ids, ids.back()};
    commit(std::move(next), "paste", ids);
    emit selectionChanged();
    return true;
}

void EditorModel::toggleKeyframe() {
    const core::TimelineClip* c =
        selection_.ids.empty() ? nullptr : clip(qstr(selection_.anchor.value_or(selection_.ids.front())));
    if (!c) {
        return;
    }
    core::Timeline next = *session_->timeline();
    const std::string id = c->id;
    for (auto& item : next.clips) {
        if (item->id == id) {
            core::toggle_keyframe_at(item.mut(), playhead_ - item->start, fps(), ids_);
        }
    }
    commit(std::move(next), "keyframe", {id});
}

void EditorModel::fadeVolume(const QString& side) {
    if (!session_ || selection_.ids.empty()) {
        return;
    }
    core::Timeline next = *session_->timeline();
    for (auto& c : next.clips) {
        if ((c->kind == "audio" || c->kind == "video") &&
            std::ranges::find(selection_.ids, c->id) != selection_.ids.end()) {
            core::apply_volume_fade_ui(c.mut(), core::ui_clip_duration(*c), side.toStdString(), ids_);
        }
    }
    commit(std::move(next), "fade-" + side.toStdString(), selection_.ids);
}

void EditorModel::toggleMuted() {
    const core::TimelineClip* primary = selection_.ids.empty() ? nullptr : clip(qstr(selection_.ids.front()));
    if (!primary) {
        return;
    }
    const bool muted = !primary->muted;
    core::Timeline next = *session_->timeline();
    for (auto& c : next.clips) {
        if (std::ranges::find(selection_.ids, c->id) != selection_.ids.end()) {
            c.mut().muted = muted;
        }
    }
    commit(std::move(next), "mute", selection_.ids);
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
