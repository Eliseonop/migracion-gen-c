// El Clip Editor del editor (la pestaña «clip» de VideoEditor.jsx de video-yt): abrir un vídeo del material
// entero, marcar un tramo con Z/X (o arrastrando las marcas en la regla) y guardarlo en el material por
// referencia con «Crear clip» o «Agregar a material» (SegmentConfirmModal.jsx). La lógica sale de
// gc/core/clip_extract.hpp y el guardado, de create_segments (gc/core/segments.hpp): sin render.
//
// La timeline del Clip Editor es otra EditSession, sin autoguardado: no se guarda en el proyecto. Al cambiar
// de pestaña, la que no se ve espera en other_session_ con su cabezal y su zoom.
//
// Aquí también entra la grabación de un directo (Vídeo › Directo, live_recorder.hpp): al material, y cada marca,
// un clip por referencia con create_segments, como «Crear clip».

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "editor_model.hpp"
#include "gc/core/clip_extract.hpp"
#include "gc/core/clip_pose_edit.hpp"
#include "gc/core/io.hpp"
#include "gc/core/live_capture.hpp"
#include "gc/core/material_drop.hpp"
#include "gc/core/media_import.hpp"
#include "gc/core/projects_store.hpp"
#include "gc/core/segments.hpp"
#include "gc/core/serialize.hpp"
#include "gc/core/timeline_ui.hpp"
#include "gc/core/yt_ui.hpp"

#ifdef GC_STUDIO_PREVIEW
#include "gc/media/probe.hpp"
#endif

namespace gc::studio {

namespace {

QString qstr(const std::string& s) { return QString::fromStdString(s); }
std::string utf8(const QString& s) { return s.toStdString(); }

// fmt de lib/utils.js: «m:ss» o «h:mm:ss», con los segundos hacia abajo.
QString fmt(double s) {
    const auto n = static_cast<long long>(std::floor(std::isfinite(s) ? std::max(0.0, s) : 0.0));
    const QString ss = QString::number(n % 60).rightJustified(2, '0');
    if (n >= 3600) {
        return QString("%1:%2:%3").arg(n / 3600).arg(QString::number((n % 3600) / 60).rightJustified(2, '0'), ss);
    }
    return QString("%1:%2").arg((n % 3600) / 60).arg(ss);
}

// La duración del archivo (probe_duration); sin gc_media, o si no se puede leer, nada.
std::optional<double> file_seconds(const std::filesystem::path& path) {
#ifdef GC_STUDIO_PREVIEW
    const double d = media::probe_duration(path);
    return d > 0 ? std::optional<double>(d) : std::nullopt;
#else
    (void)path;
    return std::nullopt;
#endif
}

// Un texto del material (lo que no es texto, vacío: `c.x || ''`).
std::string text_of(const Json& item, const char* key) {
    const auto it = item.find(key);
    return it != item.end() && it->is_string() ? it->get<std::string>() : std::string();
}

core::MarkEnd mark_end(const QString& which) { return which == "in" ? core::MarkEnd::In : core::MarkEnd::Out; }

double or_zero(double v) { return std::isnan(v) ? 0.0 : v; } // `v || 0` de un número

} // namespace

QVariantMap EditorModel::clipMark() const {
    const auto value = [](const std::optional<double>& v) { return v ? QVariant(*v) : QVariant(); };
    return {{"in", value(clip_mark_.in)},
            {"out", value(clip_mark_.out)},
            {"hasIn", clip_mark_.in.has_value()},
            {"hasOut", clip_mark_.out.has_value()},
            {"range", core::has_mark_range(clip_mark_)}};
}

bool EditorModel::canCreateSegment() const {
    if (!clip_mode_ || !session_) {
        return false;
    }
    const auto& clips = session_->timeline()->clips;
    return std::ranges::any_of(clips, [](const auto& c) { return c->kind == "video"; });
}

core::Timeline EditorModel::clip_workspace() const {
    core::Timeline tl;
    const core::EditSession* project = clip_mode_ ? other_session_.get() : session_.get();
    if (project != nullptr) { // el formato de salida es el del proyecto, como outW/outH en React
        tl.fps = project->timeline()->fps;
        tl.width = project->timeline()->width;
        tl.height = project->timeline()->height;
    }
    core::TimelineTrack v1;
    v1.id = "V1";
    v1.kind = "video";
    v1.name = "V1";
    tl.tracks.push_back(std::move(v1));
    return tl;
}

std::unique_ptr<core::EditSession> EditorModel::make_clip_session(core::Timeline tl) {
    auto session = std::make_unique<core::EditSession>(std::move(tl));
    session->subscribe([this](const core::TimelineSnapshot&, const core::Change&) {
        rebuild();
        emit selectionChanged();
    });
    return session;
}

void EditorModel::swap_canvas(bool to_clip) {
    player_.pause(); // stopPlayback
    std::swap(session_, other_session_);
    std::swap(playhead_, other_playhead_);
    std::swap(pps_, other_pps_);
    if (pps_ <= 0) { // la primera vez, el zoom de la otra pestaña
        pps_ = other_pps_;
    }
    clip_mode_ = to_clip;
    selection_ = {};
    mask_drag_.reset();
    mask_live_.reset();
    rebuild();
    player_.seeked(playhead_);
    setPps(pps_); // acotado a la duración de esta timeline
    emit canvasTabChanged();
    emit selectionChanged();
    emit playheadChanged();
    emit zoomChanged();
}

void EditorModel::setCanvasTab(const QString& tab) {
    const bool to_clip = tab == "clip";
    if (!session_ || to_clip == clip_mode_) {
        return;
    }
    if (to_clip && !other_session_) { // goClipTab sin timeline del Clip Editor: applyEmptyClipTl
        other_session_ = make_clip_session(clip_workspace());
        other_playhead_ = 0;
        other_pps_ = 0;
    }
    swap_canvas(to_clip);
}

void EditorModel::reset_clip_editor() {
    other_session_.reset();
    clip_mode_ = false;
    other_playhead_ = 0;
    other_pps_ = 0;
    clip_meta_.clear();
    clip_mark_ = {};
    seg_ask_.clear();
    emit canvasTabChanged();
    emit clipMetaChanged();
    emit clipMarkChanged();
    emit segAskChanged();
}

bool EditorModel::openClipEditor(const QString& key) {
    if (!session_ || !key.startsWith("clips:")) {
        return false;
    }
    const auto project = store_.get_project(project_id_);
    if (!ok(project) || !*project) {
        return false;
    }
    const std::string ident = utf8(key.mid(6));
    const core::ClipInfo* info = core::find_material_clip(**project, ident);
    if (info == nullptr) {
        set_notice("error", "Este clip no tiene fuente para editar.");
        return false;
    }
    const Json item = core::to_json(*info);
    const auto seg = core::segment_range(item);
    // openProjectClip: un vídeo local se abre tal cual (sin proxy), así la timeline está en segundos de su
    // archivo. Un recorte de YouTube que no es un segmento ni el vídeo completo, video-yt lo vuelve a
    // preparar desde YouTube por el backend (prepareReframe): eso aún no está aquí.
    std::string src = text_of(item, "source_url");
    if (src.empty()) {
        src = text_of(item, "youtube_url");
    }
    const bool youtube_cut = !seg && core::yt_is_url(core::trim_js_spaces(src)) && text_of(item, "provider") != "youtube";
    if (youtube_cut) {
        set_notice("error", "Este clip se recorta desde YouTube y eso aún no está en GenClipPro: abre en el Clip "
                            "Editor el vídeo completo o un segmento.");
        return false;
    }
    const auto file = core::resolve_project_media(store_.clips_dir(), **project, "video", info->filename);
    if (!file) {
        set_notice("error", "No se encuentra el archivo de este vídeo.");
        return false;
    }
    std::string title = text_of(item, "label");
    if (title.empty()) {
        title = !info->filename.empty() ? info->filename : "Clip #" + std::to_string(info->index);
    }
    // El vídeo entero; un segmento, hasta un segundo después de su fin. Divergencia: sin pasar del final del
    // archivo (el <video> de React se para ahí; el render repetiría el último fotograma).
    double span = std::max(0.3, seg ? seg->second + 1 : std::max(core::material_duration(item), 0.5));
    if (const auto d = file_seconds(*file)) {
        span = std::max(0.3, std::min(span, *d));
    }
    Json base = item; // openDirectSource: el archivo original desde 0, sin el encuadre del material
    for (const char* k : {"in_point", "out_point", "reframe", "face_track"}) {
        base.erase(k);
    }
    base["label"] = title;
    auto made = core::clip_from_json(core::make_clip_json("clips", base, "V1", 0, Json(core::js_fixed3(span)), ids_));
    if (!ok(made)) {
        return false;
    }
    core::Timeline tl = clip_workspace();
    const std::string clip_id = made->id;
    tl.clips.emplace_back(std::move(*made));

    if (clip_mode_) { // otro vídeo: la timeline anterior del Clip Editor se descarta (clipTlRef = null)
        swap_canvas(false);
    }
    other_session_ = make_clip_session(std::move(tl));
    other_playhead_ = 0;
    other_pps_ = 0;
    swap_canvas(true);

    clip_meta_ = {{"title", qstr(title)},
                  {"description", qstr(text_of(item, "description"))},
                  {"sourceIdent", qstr(ident)},
                  {"segment", seg.has_value()}};
    emit clipMetaChanged();
    clip_mark_ = seg ? core::ClipMark{seg->first, seg->second} : core::ClipMark{};
    emit clipMarkChanged();
    if (!seg_ask_.isEmpty()) {
        seg_ask_.clear();
        emit segAskChanged();
    }
    select(qstr(clip_id));
    if (clip_mark_.in) {
        setPlayhead(*clip_mark_.in);
    }
    return true;
}

void EditorModel::setClipTitle(const QString& title) {
    if (clip_meta_.value("title").toString() != title) {
        clip_meta_["title"] = title;
        emit clipMetaChanged();
    }
}

void EditorModel::setClipDescription(const QString& description) {
    if (clip_meta_.value("description").toString() != description) {
        clip_meta_["description"] = description;
        emit clipMetaChanged();
    }
}

void EditorModel::markClip(const QString& which) {
    if (!clip_mode_) {
        return;
    }
    clip_mark_ = core::set_mark(clip_mark_, mark_end(which), playhead_);
    emit clipMarkChanged();
}

void EditorModel::dragClipMark(const QString& which, double t) {
    if (!clip_mode_) {
        return;
    }
    const double at = core::snap_to_frame(t, fps());
    clip_mark_ = core::drag_mark(clip_mark_, mark_end(which), at, duration());
    emit clipMarkChanged();
    setPlayhead(at); // el cabezal la sigue: el fotograma exacto donde empieza o acaba
}

bool EditorModel::clearClipMark() {
    if (!clip_mode_ || (!clip_mark_.in && !clip_mark_.out)) {
        return false;
    }
    clip_mark_ = {};
    emit clipMarkChanged();
    return true;
}

void EditorModel::createSegmentFromMark() {
    if (!clip_mode_ || !session_ || !seg_ask_.isEmpty()) {
        return;
    }
    const auto range = core::mark_to_source_range(session_->timeline()->clips, clip_mark_);
    if (!range) {
        set_notice("error", qstr(range.error()));
        return;
    }
    player_.pause();
    const std::string source_description(core::trim_js_spaces(utf8(clip_meta_.value("description").toString())));
    seg_ask_ = {{"start", core::js_fixed3(range->start)}, // + segStart, que es 0 con un vídeo local
                {"end", core::js_fixed3(range->end)},
                {"clipId", qstr(range->clip->id)},
                {"title", qstr(core::segment_title(utf8(clip_meta_.value("title").toString())))},
                {"descMode", source_description.empty() ? "manual" : "source"},
                {"description", ""},
                {"sourceDescription", qstr(source_description)},
                {"simple", false},
                {"phase", "edit"},
                {"error", ""}};
    emit segAskChanged();
}

void EditorModel::addClipToMaterial(const QString& clipId) {
    if (!clip_mode_ || !seg_ask_.isEmpty()) {
        return;
    }
    const core::TimelineClip* video = clip(clipId);
    if (video == nullptr || video->kind != "video") {
        return;
    }
    // Su in/out actuales, en segundos del archivo (segStart es 0 con un vídeo local).
    const double start = core::js_fixed3(or_zero(video->in_point));
    const double end = core::js_fixed3(video->out_point);
    if (end - start < core::kMinSegment) {
        set_notice("error", "El clip es demasiado corto para guardarlo como material.");
        return;
    }
    player_.pause();
    const QString source_description =
        qstr(std::string(core::trim_js_spaces(utf8(clip_meta_.value("description").toString()))));
    seg_ask_ = {{"start", start},
                {"end", end},
                {"clipId", clipId},
                {"title", qstr(core::segment_title(utf8(clip_meta_.value("title").toString())))},
                {"descMode", "manual"},
                {"description", source_description},
                {"sourceDescription", source_description},
                {"simple", true},
                {"phase", "edit"},
                {"error", ""}};
    emit segAskChanged();
}

void EditorModel::patchSegAsk(const QVariantMap& patch) {
    if (seg_ask_.isEmpty()) {
        return;
    }
    for (const char* key : {"title", "descMode", "description"}) {
        if (patch.contains(key)) {
            seg_ask_[key] = patch.value(key);
        }
    }
    emit segAskChanged();
}

void EditorModel::cancelSegment() {
    if (!seg_ask_.isEmpty()) {
        seg_ask_.clear();
        emit segAskChanged();
    }
}

void EditorModel::confirmSegment() {
    if (seg_ask_.isEmpty() || !session_ || core::trim_js_spaces(utf8(seg_ask_.value("title").toString())).empty()) {
        return; // canConfirm: con título
    }
    const double start = seg_ask_.value("start").toDouble();
    const double end = seg_ask_.value("end").toDouble();
    std::string label(core::trim_js_spaces(utf8(seg_ask_.value("title").toString())));
    if (label.empty()) {
        label = core::segment_label(utf8(clip_meta_.value("title").toString()), start, end);
    }
    const std::string description = core::segment_description(
        core::desc_mode_from(utf8(seg_ask_.value("descMode").toString())), utf8(seg_ask_.value("description").toString()),
        utf8(seg_ask_.value("sourceDescription").toString()));
    const Json ranges = Json::array({{{"start", start},
                                      {"end", end},
                                      {"label", label},
                                      {"description", description.empty() ? Json() : Json(description)}}});
    const auto made = core::create_segments(store_, project_id_, utf8(clip_meta_.value("sourceIdent").toString()), ranges,
                                            file_seconds, ids_);
    if (!made) {
        seg_ask_["phase"] = "error";
        seg_ask_["error"] = qstr(made.error().message);
        emit segAskChanged();
        return;
    }
    reload_project_materials();
    seg_ask_.clear();
    emit segAskChanged();
    set_notice("success", QString("«%1» añadido a Mis materiales · %2").arg(qstr(label), fmt(end - start)));
    clip_mark_ = {clip_mark_.out, std::nullopt}; // la salida de este es la entrada del próximo
    emit clipMarkChanged();
}

void EditorModel::import_live(const QString& file, const QVariantMap& info, const QVariantList& marks) {
    const std::string pid = utf8(info.value("pid").toString());
    const auto path = core::path_from_utf8(utf8(file));
    const QString title = info.value("title").toString().trimmed();
    const QString uploader = info.value("uploader").toString().trimmed();
    const QString platform = info.value("platform").toString();
    core::ImportMeta meta;
    meta.label = utf8(!title.isEmpty() ? title : QString("Directo de %1").arg(!uploader.isEmpty() ? uploader : platform));
    meta.origin = "live";
    meta.provider = utf8(platform.toLower());
    meta.source_url = utf8(info.value("url").toString());
    if (const QString id = info.value("id").toString(); !id.isEmpty()) {
        meta.external_id = utf8(id);
    }
    if (!uploader.isEmpty()) {
        meta.author = utf8(uploader);
    }
    const auto clip = core::adopt_video_file(store_, pid, path, core::path_to_utf8(path.filename()), file_seconds, ids_, meta);
    if (!clip) {
        live_.finish({}, QString("No se pudo meter la grabación en el material: %1").arg(qstr(clip.error().message)));
        return;
    }
    // Cada marca, un clip por referencia: un rato antes y otro después (los que se pisan, juntos).
    std::vector<double> times;
    for (const QVariant& m : marks) {
        times.push_back(m.toMap().value("t").toDouble());
    }
    const std::string label = clip->label.value_or(core::path_to_utf8(path.stem()));
    Json ranges = Json::array();
    for (const auto& [start, end] : core::live_mark_ranges(times, clip->end, live_.before(), live_.after())) {
        ranges.push_back({{"start", start}, {"end", end}, {"label", core::segment_label(label, start, end)}});
    }
    std::size_t made = 0;
    QString problem;
    if (!ranges.empty()) {
        if (const auto segs = core::create_segments(store_, pid, std::to_string(clip->index), ranges, file_seconds, ids_)) {
            made = segs->size();
        } else {
            problem = qstr(segs.error().message);
        }
    }
    if (pid == project_id_) {
        reload_project_materials();
    }
    QString message = QString("«%1» (%2) está en el material").arg(qstr(label), fmt(clip->end));
    if (made == 1) {
        message += " con 1 clip";
    } else if (made > 1) {
        message += QString(" con %1 clips").arg(made);
    }
    message += info.value("ended").toBool() ? QString(": el directo acabó.") : QString(".");
    if (!problem.isEmpty()) {
        message += " Los clips de las marcas no se pudieron crear: " + problem;
    }
    live_.finish(message);
    set_notice(problem.isEmpty() ? "success" : "info", message);
}

} // namespace gc::studio
