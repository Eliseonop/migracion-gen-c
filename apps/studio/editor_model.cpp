#include "editor_model.hpp"

#include <QColor>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QPointF>
#include <QRegularExpression>
#include <QUrl>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeDatabase>
#include <algorithm>
#include <cmath>
#include <map>
#include <unordered_map>
#include <utility>

#include "gc/core/captions_ui.hpp"
#include "gc/core/clip_attrs.hpp"
#include "gc/core/clip_filters.hpp"
#include "gc/core/clip_layout_ui.hpp"
#include "gc/core/clip_media.hpp"
#include "gc/core/face_follow.hpp"
#include "gc/core/fx_catalog.hpp"
#include "gc/core/clip_pose_edit.hpp"
#include "gc/core/collections.hpp"
#include "gc/core/frame.hpp"
#include "gc/core/history_store.hpp"
#include "gc/core/io.hpp"
#include "gc/core/keyframes.hpp"
#include "gc/core/library.hpp"
#include "gc/core/mask_edit.hpp"
#include "gc/core/mask_ui.hpp"
#include "gc/core/masks.hpp"
#include "gc/core/material_drop.hpp"
#include "gc/core/media_import.hpp"
#include "gc/core/no_overlap.hpp"
#include "gc/core/pose.hpp"
#include "gc/core/serialize.hpp"
#include "gc/core/sfx_disk.hpp"
#include "gc/core/sfx_name.hpp"
#include "gc/core/shape_geometry.hpp"
#include "gc/core/shapes.hpp"
#include "gc/core/text_clip_ui.hpp"
#include "gc/core/text_style.hpp"
#include "gc/core/timeline_rules.hpp"
#include "gc/core/timeline_view.hpp"
#include "gc/core/vfx_stack.hpp"
#include "qt_json.hpp"

#ifdef GC_STUDIO_PREVIEW
#include "gc/export/image_import.hpp"
#include "gc/media/decode.hpp"
#include "gc/media/probe.hpp"
#endif

namespace gc::studio {

namespace {

// SHAPE_CATALOG de lib/shapes.js: categorías con sus tipos y etiquetas.
const std::vector<std::pair<const char*, std::vector<std::pair<const char*, const char*>>>> kShapeCatalog{
    {"Básicas", {{"square", "Cuadrado"}, {"circle", "Círculo"}, {"rect", "Rectángulo"}, {"line", "Línea"}}},
    {"Flechas", {{"arrow", "Flecha"}, {"arrow_curve", "Flecha curva"}, {"arrow_double", "Flecha doble"}}},
    {"Geométricas", {{"triangle", "Triángulo"}, {"star", "Estrella"}, {"polygon", "Polígono"}}},
    {"UI", {{"speech", "Burbuja"}, {"check", "Check"}, {"x", "X"}, {"marker", "Marcador"}, {"heart", "Corazón"}}}};

const std::map<std::string, std::vector<std::string>>& text_kf_sections() {
    static const std::map<std::string, std::vector<std::string>> kSections{
        {"color", {"color"}},
        {"stroke", {"border_color", "border_width"}},
        {"bg", {"bg", "bg_opacity", "bg_radius", "bg_pad_x", "bg_pad_y", "bg_dx", "bg_dy"}},
        {"glow", {"glow_color", "glow_intensity", "glow_range", "glow_dx", "glow_dy"}},
        {"curve", {"curve"}},
        {"stretch", {"stretch_x", "stretch_y"}},
        {"shadow", {"shadow_color", "shadow_opacity", "shadow_blur", "shadow_distance", "shadow_angle"}}};
    return kSections;
}


double js_fixed1(double v) { return std::isfinite(v) ? core::js_fixed(v, 1) : 0.0; }

// ASSETS_DIR de config.py: VIDEO_YT_ASSETS o «assets» junto a la carpeta raíz.
std::filesystem::path assets_dir(const std::filesystem::path& data_dir) {
    const QByteArray env = qgetenv("VIDEO_YT_ASSETS");
    return env.isEmpty() ? data_dir.parent_path().parent_path() / "assets" : core::path_from_utf8(env.toStdString());
}

Json vfx_list(const core::TimelineClip* c) { return c && c->vfx && c->vfx->is_array() ? *c->vfx : Json::array(); }

// SWATCH y applyMatrix de clipFilters.js.
QStringList filter_swatch(std::string_view id, double amount) {
    constexpr std::array<std::array<double, 3>, 4> kSwatch{{{0.87, 0.64, 0.52}, {0.36, 0.62, 0.9}, {0.3, 0.55, 0.25}, {0.5, 0.5, 0.5}}};
    const core::AffineColorMatrix m = core::filter_matrix(id, amount);
    QStringList out;
    for (const auto& rgb : kSwatch) {
        std::array<int, 3> c{};
        for (std::size_t i = 0; i < 3; ++i) {
            const auto& row = m.at(i);
            const double v = row[0] * rgb[0] + row[1] * rgb[1] + row[2] * rgb[2] + row[3];
            c.at(i) = static_cast<int>(std::lround(std::clamp(v, 0.0, 1.0) * 255));
        }
        out << QColor(c[0], c[1], c[2]).name();
    }
    return out;
}

} // namespace

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
// clipBeats de lib/beats.js: los beats del clip (cuántos tiempos finitos, el bpm y uno de cada
// 1, 2 o 4), o nada si no hay tiempos.
struct BeatsView {
    int count = 0;
    double bpm = 0;
    int every = 1;
};
std::optional<BeatsView> clip_beats_view(const core::TimelineClip& c) {
    if (!c.beats || !c.beats->is_object()) {
        return std::nullopt;
    }
    const Json& b = *c.beats;
    const auto times = b.find("times");
    if (times == b.end() || !times->is_array() || times->empty()) {
        return std::nullopt;
    }
    BeatsView v;
    for (const Json& t : *times) {
        v.count += std::isfinite(core::js_number(t)) ? 1 : 0;
    }
    const double bpm = core::js_number(b.value("bpm", Json()));
    v.bpm = std::isnan(bpm) ? 0.0 : bpm; // Number(b.bpm) || 0
    const double every = core::js_number(b.value("every", Json()));
    v.every = every == 2 || every == 4 ? static_cast<int>(every) : 1;
    return v;
}

QVariant beats_variant(const core::TimelineClip& c) {
    const auto v = clip_beats_view(c);
    if (!v) {
        return {};
    }
    return QVariantMap{{"count", v->count}, {"bpm", v->bpm}, {"every", v->every}};
}

// Beats del clip como fracción de su ancho (las rayitas de EdTimeline).
QVariantList beat_marks(const core::TimelineClip& c) {
    QVariantList out;
    const double dur = core::ui_clip_duration(c);
    for (const double t : core::clip_beat_times(c)) {
        if (dur > 0) {
            out << (t - c.start) / dur;
        }
    }
    return out;
}

// Rombos de keyframe del clip: {id, f} con f la fracción de su ancho (los del encuadre, sin keyframes).
QVariantList keyframe_marks(const core::TimelineClip& c) {
    QVariantList out;
    const double dur = core::ui_clip_duration(c);
    const auto id_of = [](const Json& extra) { return qstr(extra.is_object() ? extra.value("id", "") : ""); };
    if (core::keyframes_enabled(c) && c.keyframes->contains("items") && c.keyframes->at("items").is_array()) {
        for (const Json& k : c.keyframes->at("items")) {
            if (k.is_object() && k.contains("t") && k.at("t").is_number()) {
                out << QVariantMap{{"id", id_of(k)}, {"f", k.at("t").get<double>() / (dur > 0 ? dur : 1)}};
            }
        }
    } else if (c.reframe && (c.kind == "video" || c.kind == "image")) {
        const double src = std::max(0.0, c.out_point - c.in_point);
        for (const core::Keyframe& k : c.reframe->keyframes) {
            out << QVariantMap{{"id", id_of(k.extra)}, {"f", (k.t - c.in_point) / (src > 0 ? src : 1)}};
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

EditorModel::EditorModel(core::ProjectsStore& store, core::SettingsFile* settings, QObject* parent)
    : QObject(parent), store_(store), settings_(settings), player_(*this) {
    // Sonando, el inspector no se recalcula a cada fotograma (setPlayhead): al parar, una vez.
    connect(&player_, &Player::playingChanged, this, [this] {
        if (!player_.playing() && !selection_.ids.empty()) {
            emit selectionChanged();
        }
    });
    // Cada proxy que acaba, a la vista previa.
    connect(&proxy_maker_, &ProxyMaker::proxiesChanged, this, [this] { preview_.setProxies(proxy_maker_.proxies()); });
    connect(&beat_detector_, &BeatDetector::finished, this, &EditorModel::beats_detected);
    connect(&crop_frames_, &FrameGrabber::frameChanged, this, &EditorModel::cropFrameChanged);
    face_jobs_.setPollInterval(500); // followMaskFace sondea cada medio segundo
    // La máscara que se edita cambia con el clip elegido, la timeline, el cabezal y el modo.
    for (const auto signal : {&EditorModel::selectionChanged, &EditorModel::timelineChanged,
                              &EditorModel::playheadChanged, &EditorModel::maskModeChanged}) {
        connect(this, signal, this, &EditorModel::editMaskChanged);
    }
}

void EditorModel::set_notice(const QString& type, const QString& message) {
    notice_ = QVariantMap{{"type", type}, {"message", message}};
    emit noticeChanged();
}

void EditorModel::clearNotice() {
    if (!notice_.isEmpty()) {
        notice_.clear();
        emit noticeChanged();
    }
}

void EditorModel::detectBeats(const QString& clipId) {
    const core::TimelineClip* c = clip(clipId);
    if (!c || (c->kind != "audio" && c->kind != "video") || !beat_busy_.isEmpty()) {
        return;
    }
    if (!beat_detector_.start(store_.file().parent_path().parent_path(), project_id_, *c)) {
        set_notice("error", "No se pudieron detectar los beats.");
        return;
    }
    beat_busy_ = clipId;
    beat_project_ = project_id_;
    emit beatBusyChanged();
}

void EditorModel::beats_detected(const QString& clipId, const QVariantList& times, double bpm, const QString& error) {
    beat_busy_.clear();
    emit beatBusyChanged();
    if (!error.isEmpty()) {
        set_notice("error", error);
        return;
    }
    // En el clip tal como está ahora (si el proyecto sigue abierto): {times, bpm || 0, every || 1}.
    const core::TimelineClip* c = session_ && beat_project_ == project_id_ ? clip(clipId) : nullptr;
    if (c) {
        const auto before = clip_beats_view(*c);
        Json list = Json::array();
        for (const QVariant& t : times) {
            list.push_back(t.toDouble());
        }
        core::Timeline next = *session_->timeline();
        for (auto& x : next.clips) {
            if (x->id == clipId.toStdString()) {
                x.mut().beats = Json{{"times", std::move(list)}, {"bpm", bpm}, {"every", before ? before->every : 1}};
                break;
            }
        }
        commit(std::move(next), "beats", {clipId.toStdString()});
    }
    set_notice("success", QString("%1 beats · %2 BPM").arg(times.size()).arg(static_cast<int>(std::floor(bpm + 0.5))));
}

void EditorModel::setBeatsEvery(const QString& clipId, int every) {
    const core::TimelineClip* c = clip(clipId);
    if (!c || !c->beats || !c->beats->is_object()) {
        return;
    }
    core::Timeline next = *session_->timeline();
    for (auto& x : next.clips) {
        if (x->id == clipId.toStdString()) {
            (*x.mut().beats)["every"] = every;
        }
    }
    commit(std::move(next), "beats", {clipId.toStdString()});
}

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

void EditorModel::add_material(const char* source, const char* asset_kind, const QString& key, const Json& item,
                               const char* kind, QVariantList& into) {
    const Json payload = core::drag_payload(asset_kind, item);
    payloads_[key] = payload;
    into << QVariantMap{{"key", key},
                        {"source", source},
                        {"assetKind", asset_kind},
                        {"kind", kind},
                        {"name", qstr(payload.value("name", Json("")).is_string() ? payload.value("name", "") : "")},
                        {"duration", core::js_number(payload.value("duration", Json(0)))}};
}

void EditorModel::project_materials(const core::Project& project, QVariantList& into) {
    for (const auto& c : project.clips) {
        add_material("project", "clips", QString("clips:%1").arg(c.index), core::to_json(c), "video", into);
    }
    for (const auto& i : project.images) {
        add_material("project", "images", "images:" + qstr(i.id), core::to_json(i), "image", into);
    }
    for (const auto& a : project.audios) {
        add_material("project", "audios", "audios:" + qstr(a.id), core::to_json(a), "audio", into);
    }
}

namespace {

// isVideoFile, isImageFile (imagePaste.js) e isAudioFile de EdMaterial.jsx: el tipo MIME que da el
// sistema por la extensión (el `file.type` del navegador) o la extensión. "" si no es ninguno.
QString media_file_kind(const QString& name) {
    static const QRegularExpression video(R"(\.(mp4|mov|mkv|webm|avi|m4v|mpe?g|wmv|flv)$)",
                                          QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression image(R"(\.(png|jpe?g|webp|gif|bmp|tiff?|avif|heic|heif)$)",
                                          QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression audio(R"(\.(mp3|wav|m4a|aac|ogg|flac|wma)$)",
                                          QRegularExpression::CaseInsensitiveOption);
    const QString mime = QMimeDatabase().mimeTypeForFile(name, QMimeDatabase::MatchExtension).name();
    if (mime.startsWith("video/") || video.match(name).hasMatch()) {
        return "video";
    }
    if (mime.startsWith("image/") || image.match(name).hasMatch()) {
        return "image";
    }
    if (mime.startsWith("audio/") || audio.match(name).hasMatch()) {
        return "audio";
    }
    return {};
}

std::filesystem::path local_path(const QUrl& url) {
    return core::path_from_utf8((url.isLocalFile() ? url.toLocalFile() : url.toString()).toStdString());
}

} // namespace

QString EditorModel::dropFileKind(const QList<QUrl>& files) const {
    if (files.isEmpty()) {
        return "file";
    }
    QStringList kinds;
    for (const QUrl& url : files) {
        kinds << media_file_kind(qstr(core::path_to_utf8(local_path(url).filename())));
    }
    for (const char* kind : {"video", "image", "audio"}) {
        if (kinds.contains(kind)) {
            return kind;
        }
    }
    return "other";
}

int EditorModel::importFiles(const QList<QUrl>& files, bool dropped) {
    if (!session_) {
        return 0;
    }
    const core::DurationProbe probe = [](const std::filesystem::path& path) -> std::optional<double> {
#ifdef GC_STUDIO_PREVIEW
        return media::probe_duration(path);
#else
        (void)path;
        return std::nullopt;
#endif
    };
#ifdef GC_STUDIO_PREVIEW
    const core::ImageCodec codec = exporter::image_codec();
#else
    const core::ImageCodec codec; // sin gc_media: solo los PNG, y sin su tamaño
#endif
    // importMedia: los vídeos, luego las imágenes y luego los audios; lo que no es ninguno, fuera.
    std::vector<std::pair<std::filesystem::path, QString>> todo;
    for (const char* wanted : {"video", "image", "audio"}) {
        for (const QUrl& url : files) {
            const auto path = local_path(url);
            if (media_file_kind(qstr(core::path_to_utf8(path.filename()))) == wanted) {
                todo.emplace_back(path, wanted);
            }
        }
    }
    if (todo.empty()) {
        set_notice("error", dropped ? QString("Formato no compatible. Suelta un vídeo, una imagen o un audio.")
                                    : QString("Suelta un vídeo, una imagen o un audio."));
        return 0;
    }
    int imported = 0;
    QString last;
    QStringList problems;
    for (const auto& [path, kind] : todo) {
        const std::string name = core::path_to_utf8(path.filename());
        std::optional<Error> failed;
        if (kind == "video") { // upload_video: videos.import_video valida la extensión
            if (auto r = core::import_video_file(store_, project_id_, path, name, probe, ids_); !r) {
                failed = r.error();
            }
        } else if (kind == "image") { // upload_images: los GIF animados se quedan como GIF
            if (auto r = core::import_image_file(store_, project_id_, path, name, codec, ids_, {}, true); !r) {
                failed = r.error();
            }
        } else if (auto r = core::import_audio_file(store_, project_id_, path, name, probe, ids_); !r) {
            failed = r.error();
        }
        if (failed) {
            problems << qstr(name) + ": " + qstr(failed->message);
        } else {
            ++imported;
            last = qstr(name);
        }
    }
    if (imported > 0) { // el material nuevo, delante (como el del proyecto al abrirlo)
        if (const auto project = store_.get_project(project_id_); project && *project) {
            materials_.removeIf([](const QVariant& m) { return m.toMap()["source"] == "project"; });
            QVariantList fresh;
            project_materials(**project, fresh);
            materials_ = fresh + materials_;
            emit projectChanged();
        }
    }
    // Los textos de importMedia (EdMaterial.jsx); React para en el primer fallo, aquí se dice cada uno.
    if (!problems.isEmpty()) {
        set_notice("error", problems.join("\n"));
    } else if (imported > 0) {
        set_notice("success", imported == 1 ? QString("«%1» está en el material.").arg(last)
                                            : QString("%1 archivos añadidos al material.").arg(imported));
    }
    return imported;
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
    const auto add = [&](const char* source, const char* asset_kind, const QString& key, const Json& item,
                         const char* kind) { add_material(source, asset_kind, key, item, kind, materials_); };
    project_materials(**project, materials_);
    const auto add_text = [&](const Json* theme) { // dragTextPayload
        const std::string id = theme ? theme->value("id", std::string()) : std::string();
        const std::string name = theme ? theme->value("name", std::string()) : std::string(core::kDefaultText);
        const QString key = "text:" + qstr(id.empty() ? "default" : id);
        payloads_[key] = Json{{"asset_kind", "text"}, {"kind", "text"}, {"asset_id", id.empty() ? "text" : id},
                              {"filename", ""}, {"name", name},
                              {"text", theme ? theme->value("sample", std::string(core::kDefaultText)) : std::string(core::kDefaultText)},
                              {"duration", core::kTextDefaultDuration}, {"theme_id", id.empty() ? Json() : Json(id)}};
        materials_ << QVariantMap{{"key", key},   {"source", "text"}, {"assetKind", "text"}, {"kind", "text"},
                                  {"themeId", qstr(id)}, {"name", qstr(name)}, {"duration", core::kTextDefaultDuration}};
    };
    add_text(nullptr);
    for (const Json& theme : core::subtitle_themes()) {
        add_text(&theme);
    }
    for (const auto& [category, items] : kShapeCatalog) { // EdShapes: dragShapePayload
        for (const auto& [type, label] : items) {
            const QString key = QString("shape:") + type;
            payloads_[key] = Json{{"asset_kind", "shape"},       {"asset_id", type},
                                  {"filename", ""},               {"name", label},
                                  {"duration", core::kShapeDefaultDuration}, {"kind", "shape"},
                                  {"shape", core::default_shape(type)},      {"type", type},
                                  {"label", label}};
            materials_ << QVariantMap{{"key", key},     {"source", "shapes"}, {"assetKind", "shape"}, {"kind", "shape"},
                                      {"type", type},   {"name", label},      {"category", category},
                                      {"duration", core::kShapeDefaultDuration}};
        }
    }
    // La biblioteca global (data/library) y los sonidos de la carpeta de SFX.
    const auto data_dir = store_.file().parent_path();
    if (const auto lib = core::LibraryFile(data_dir).list()) {
        for (const auto& [list, kind] : {std::pair{"clips", "video"}, {"images", "image"}, {"audios", "audio"}}) {
            for (const Json& item : lib->value(list, Json::array())) {
                add("library", list, QString("lib:%1:").arg(list) + qstr(item.value("id", "")), item, kind);
            }
        }
    }
    load_sfx("", "");
    load_collections("", "");
    // Deshacer sobrevive a cerrar el proyecto: data/history/<pid>.json, el mismo archivo que video-yt.
    autosave_.reset();
    const auto history_file = core::history_path(store_.file().parent_path(), project_id_);
    sizes_.clear();
    // Lo antiguo («fill», un overlay pegado a un hueco) entra ya como objeto libre, sin paso de deshacer:
    // en video-yt lo convierte el efecto en cuanto carga el medio, dentro del debounce del historial, y
    // el autoguardado lo guarda a los 800 ms. Aquí se guarda al abrir.
    core::Timeline start = (*project)->timeline.value_or(default_timeline());
    playhead_ = 0;
    if (!free_layouts(start).empty()) {
        (void)ok(store_.save_timeline(project_id_, core::to_json(start)));
    }
    session_ = std::make_unique<core::EditSession>(std::move(start), core::load_history(history_file));
    autosave_ = std::make_unique<core::Autosave>(*session_, history_file, [this](const core::Timeline& tl) {
        const auto saved = store_.save_timeline(project_id_, core::to_json(tl));
        return saved ? Result<void>{} : Result<void>(std::unexpected(saved.error()));
    });
    session_->subscribe([this](const core::TimelineSnapshot&, const core::Change&) {
        const auto failed = autosave_->last_error();
        setError(failed ? qstr(failed->message) : QString());
        rebuild();
        emit selectionChanged(); // el inspector pinta el clip ya cambiado
    });
    selection_ = {};
    mask_drag_.reset();
    mask_live_.reset();
    // La raíz del proyecto, como la de video-yt: <raíz>/data/projects.json y <raíz>/clips.
    const auto root = store_.file().parent_path().parent_path();
    player_.close();
    player_.open(root, project_id_);
    preview_.open(root, project_id_);
    proxy_maker_.open(root, project_id_);
    rebuild();
    emit projectChanged();
    emit selectionChanged();
    emit playheadChanged();
    return true;
}

std::optional<std::filesystem::path> EditorModel::sfx_dir() const {
    const auto data_dir = store_.file().parent_path();
    return core::sfx_base(settings_ ? settings_->load() : Json::object(), assets_dir(data_dir) / "sfx_library");
}

void EditorModel::load_sfx(const std::string& q, const std::string& category) {
    sfx_query_ = {q, category};
    materials_.removeIf([](const QVariant& m) { return m.toMap()["source"] == "sfx"; });
    const Json settings = settings_ ? settings_->load() : Json::object();
    const Json sfx = core::sfx_search(sfx_dir(), settings, q, category);
    sfx_items_.clear();
    for (const Json& item : sfx.value("items", Json::array())) {
        const QString key = "sfx:" + qstr(item.value("id", ""));
        const Json payload = core::drag_payload("sfx", item);
        payloads_[key] = payload;
        sfx_items_[key] = item;
        materials_ << QVariantMap{{"key", key},
                                  {"source", "sfx"},
                                  {"assetKind", "sfx"},
                                  {"kind", "audio"},
                                  {"name", qstr(item.value("name", ""))},
                                  {"category", qstr(item.value("category", ""))},
                                  {"duration", core::js_number(payload.value("duration", Json(0)))}};
    }
    sfx_categories_.clear();
    for (const Json& c : sfx.value("categories", Json::array())) {
        sfx_categories_ << to_variant(c);
    }
}

std::optional<std::filesystem::path> EditorModel::collections_base() const {
    const auto data_dir = store_.file().parent_path();
    return core::collections_root(settings_ ? settings_->load() : Json::object(), assets_dir(data_dir) / "material");
}

void EditorModel::load_collections(const std::string& q, const std::string& collection) {
    collections_query_ = {q, collection};
    materials_.removeIf([](const QVariant& m) { return m.toMap()["source"] == "collection"; });
    const Json settings = settings_ ? settings_->load() : Json::object();
    const auto root = collections_base();
    collections_root_ = root ? QString::fromStdString(core::path_to_utf8(*root)) : QString();
    collections_.clear();
    if (const auto list = core::list_collections(root, settings)) {
        for (const Json& c : list->value("collections", Json::array())) {
            collections_ << QVariantMap{{"id", qstr(c.value("id", ""))},
                                        {"name", qstr(c.value("name", ""))},
                                        {"count", c.value("count", 0)},
                                        {"enabled", c.value("enabled", true)},
                                        {"favorite", c.value("favorite", false)},
                                        {"path", qstr(c.value("path", ""))}};
        }
    }
    const auto found = core::collections_search(root, settings, q, "", collection);
    for (const Json& item : found ? found->value("items", Json::array()) : Json::array()) {
        const std::string kind = item.value("kind", "");
        const char* asset_kind = kind == "video" ? "clips" : kind == "audio" ? "audios" : "images";
        Json media = item;
        if (kind == "gif") {
            media["animated"] = true;
        }
        const QString key = "col:" + qstr(item.value("id", ""));
        const Json payload = core::drag_payload(asset_kind, media);
        payloads_[key] = payload;
        materials_ << QVariantMap{{"key", key},
                                  {"source", "collection"},
                                  {"assetKind", asset_kind},
                                  {"kind", kind == "video" ? "video" : kind == "audio" ? "audio" : "image"},
                                  {"name", qstr(item.value("name", ""))},
                                  {"duration", core::js_number(payload.value("duration", Json(0)))}};
    }
}

void EditorModel::searchCollections(const QString& q, const QString& collection) {
    if (session_) {
        load_collections(q.toStdString(), collection.toStdString());
        emit projectChanged();
    }
}

void EditorModel::reload_collections() {
    load_collections(collections_query_.first, collections_query_.second);
    emit projectChanged();
}

void EditorModel::setCollectionPrefs(const QString& id, const QVariantMap& patch) {
    if (!settings_) {
        return;
    }
    const auto flag = [&](const char* key) {
        return patch.contains(key) ? std::optional(patch[key].toBool()) : std::nullopt;
    };
    const std::string label = patch["label"].toString().toStdString();
    const auto saved = core::set_collection_prefs(*settings_, id.toStdString(), flag("enabled"), flag("favorite"),
                                                  patch.contains("label") ? std::optional<std::string_view>(label)
                                                                          : std::nullopt);
    if (ok(saved)) {
        reload_collections();
    }
}

bool EditorModel::setCollectionsRoot(const QString& path) {
    if (!settings_) {
        return false;
    }
    const QUrl url(path);
    const auto saved = core::set_collections_root(*settings_, (url.isLocalFile() ? url.toLocalFile() : path).toStdString());
    if (!ok(saved)) {
        return false;
    }
    if (!*saved) {
        setError("Carpeta no válida.");
        return false;
    }
    load_collections("", "");
    emit projectChanged();
    return true;
}

QString EditorModel::createCollection(const QString& name) {
    const auto id = core::create_collection(collections_base(), name.toStdString());
    if (!ok(id)) {
        return {};
    }
    reload_collections();
    return qstr(*id);
}

int EditorModel::addCollectionFiles(const QString& collection, const QList<QUrl>& files) {
    std::vector<core::IncomingFile> incoming;
    for (const QUrl& url : files) {
        const auto src = core::path_from_utf8(url.toLocalFile().toStdString());
        const auto copy = [src](const std::filesystem::path& target) -> Result<void> {
            std::error_code ec;
            std::filesystem::copy_file(src, target, ec);
            return ec ? Result<void>(fail(ErrorCode::Io, ec.message())) : Result<void>();
        };
        incoming.push_back(core::IncomingFile{Json(core::path_to_utf8(src.filename())), copy});
    }
    const auto added = core::add_collection_files(collections_base(), collection.toStdString(), incoming);
    if (!ok(added)) {
        return 0;
    }
    if (const Json errors = added->value("errors", Json::array()); !errors.empty()) {
        setError(qstr(errors[0].value("file", "") + ": " + errors[0].value("error", "")));
    }
    reload_collections();
    return static_cast<int>(added->value("saved", Json::array()).size());
}

Json EditorModel::real_sfx_categories() const {
    Json cats = Json::array();
    for (const QVariant& c : sfx_categories_) {
        cats.push_back(to_json(c.toMap()));
    }
    return core::real_sfx_categories(cats);
}

QVariantList EditorModel::sfxDrafts(const QList<QUrl>& files, const QString& defaultCategory) const {
    const Json cats = real_sfx_categories();
    const auto with_id = [&](const std::string& id) {
        return std::ranges::find_if(cats, [&](const Json& c) { return c.value("id", Json()) == id; });
    };
    auto cat = with_id(defaultCategory.toStdString());
    cat = cat != cats.end() ? cat : with_id("13_OTHER");
    cat = cat != cats.end() ? cat : cats.begin();
    const Json id = cat != cats.end() ? cat->value("id", Json("")) : Json("");
    const Json label = cat != cats.end() ? cat->value("label", id) : Json("");
    QVariantList out;
    for (const QUrl& url : files) {
        const QString file = url.isLocalFile() ? url.toLocalFile() : url.toString();
        if (core::is_sfx_audio(QFileInfo(file).fileName().toStdString())) {
            out << QVariantMap{{"file", file},
                               {"sfxId", QVariant()},
                               {"name", qstr(core::sfx_name_from_file(QFileInfo(file).fileName().toStdString()))},
                               {"categoryId", to_qvariant(id)},
                               {"categoryLabel", to_qvariant(label)},
                               {"uso", ""},
                               {"saved", false}};
        }
    }
    return out;
}

QVariantMap EditorModel::sfxDraft(const QString& key) const {
    const auto it = sfx_items_.find(key);
    return it == sfx_items_.end() ? QVariantMap() : to_variant(core::sfx_draft_from_saved(it->second));
}

QVariantMap EditorModel::createSfxCategory(const QString& label) {
    const Json hit = core::match_sfx_category(label.toStdString(), real_sfx_categories());
    if (!hit.at("categoryId").get<std::string>().empty()) {
        return {{"id", to_qvariant(hit.at("categoryId"))}};
    }
    const auto cat = core::sfx_create_category(sfx_dir(), label.toStdString());
    if (!cat) {
        return {{"error", qstr(cat.error().message)}};
    }
    load_sfx(sfx_query_.first, sfx_query_.second);
    emit projectChanged();
    return to_variant(*cat);
}

QString EditorModel::saveSfx(const QVariantMap& row) {
    const Json draft = to_json(row);
    const Json payload = core::sfx_save_payload(draft, real_sfx_categories());
    const core::SfxSoundEdit edit{payload.at("name").get<std::string>(), payload.at("categoryId").get<std::string>(),
                                  payload.at("newCategory").get<std::string>(), payload.at("uso").get<std::string>()};
    if (edit.category_id.empty() && edit.new_category.empty()) {
        return "Pon una categoría.";
    }
    Result<Json> saved = fail(ErrorCode::InvalidArgument, "Sin archivo.");
    if (const QString id = row["sfxId"].toString(); !id.isEmpty()) {
        if (!settings_) {
            return "Sin ajustes.";
        }
        saved = core::sfx_update_sound(sfx_dir(), *settings_, id.toStdString(), edit);
    } else if (QFile file(row["file"].toString()); file.open(QIODevice::ReadOnly)) {
        const QByteArray data = file.readAll();
        saved = core::sfx_add_sound(sfx_dir(), QFileInfo(file).fileName().toStdString(),
                                    std::string_view(data.constData(), static_cast<std::size_t>(data.size())), edit);
    }
    if (!saved) {
        return qstr(saved.error().message);
    }
    load_sfx(sfx_query_.first, sfx_query_.second);
    emit projectChanged();
    return {};
}

void EditorModel::searchSfx(const QString& q, const QString& category) {
    if (session_) {
        load_sfx(q.toStdString(), category.toStdString());
        emit projectChanged();
    }
}

bool EditorModel::exportVideo() {
    if (!session_) {
        return false;
    }
    player_.pause();
    // Lo que se exporta es la timeline de la sesión, ya guardada (cada cambio va a projects.json).
    return export_job_.start(store_.file().parent_path().parent_path(), project_id_, session_->timeline());
}

void EditorModel::closeProject() {
    player_.close();
    preview_.close();
    proxy_maker_.close();
    autosave_.reset();
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
                            {"assetId", qstr(c->asset_id)},
                            {"label", clip_label(*c)},
                            {"speed", speed_badge(*c)},
                            {"start", c->start},
                            {"duration", core::ui_clip_duration(*c)},
                            {"clipY", row->second.first + kClipInset},
                            {"clipHeight", row->second.second - 2 * kClipInset},
                            {"muted", c->muted},
                            {"disabled", c->disabled},
                            {"locked", locked[c->track_id]},
                            {"kfs", keyframe_marks(*c)},
                            {"beats", beat_marks(*c)}});
        }
        preview_.setTimeline(session_->timeline()); // si es otra, el render se vuelve a preparar
        preview_.request(playhead_);
        player_.setTimeline(session_->timeline()); // sonando, se vuelve a mezclar
        proxy_maker_.setTimeline(session_->timeline()); // un vídeo nuevo, su proxy
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
    const core::TransformView tv = core::transform_view(*c, local, static_cast<double>(tl.width),
                                                        static_cast<double>(tl.height), height_scale(*c));
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
                                  {"degrees", tv.rotation_deg},
                                  {"rotX", c->kind == "text" ? js_fixed1(core::ui_props_at(*c, std::max(0.0, local)).get("rot_x")) : 0.0},
                                  {"rotY", c->kind == "text" ? js_fixed1(core::ui_props_at(*c, std::max(0.0, local)).get("rot_y")) : 0.0}}},
        {"text", qstr(c->text.value_or(""))},
        {"effects", to_variant(c->effects.value_or(Json::object()))},
        {"audioFx", [&] { // valueOf de EdEffects: el valor animado si hay keyframes
             const core::KfProps live = core::clip_props_at(*c, std::max(0.0, local));
             const Json fx = c->audio_fx.value_or(Json::object());
             QVariantMap out;
             for (const char* key : {"eq", "compressor", "reverb", "echo", "denoise", "distortion", "underwater",
                                     "telephone", "radio", "megaphone", "muffled"}) {
                 const Json& v = fx.contains(key) ? fx.at(key) : Json();
                 const double raw = core::keyframes_enabled(*c) ? live.get(key)
                                    : v.is_boolean()              ? (v.get<bool>() ? 1.0 : 0.0)
                                                                  : core::js_number(v);
                 out[key] = std::isfinite(raw) ? std::clamp(raw, 0.0, 1.0) : 0.0;
             }
             return out;
         }()},
        {"filters", [&] {
             QVariantList out;
             for (const auto& f : core::clip_filters(*c)) {
                 out << QVariantMap{{"id", qstr(f.id)}, {"amount", f.amount}, {"swatch", filter_swatch(f.id, f.amount)}};
             }
             return out;
         }()},
        {"styleKf", [&] {
             QVariantMap out;
             if (c->kind == "text") {
                 for (const auto& [section, keys] : text_kf_sections()) {
                     out[qstr(section)] = qstr(core::style_kf_state(*c, std::max(0.0, local), keys, fps()));
                 }
             }
             return out;
         }()},
        {"themeId", c->kind == "text" ? qstr(core::selected_subtitle_theme_id(c->style.value_or(Json())).value_or(""))
                                       : QString()},
        {"lookId", c->kind == "text" ? qstr(core::active_look_id(c->style.value_or(Json())).value_or("")) : QString()},
        {"style", c->kind == "text" ? to_variant(core::with_text_style_kf(c->style.value_or(Json::object()),
                                                                           core::text_style_at(*c, std::max(0.0, local))))
                                    : QVariantMap()},
        {"shape", c->kind == "shape" ? to_variant(core::normalize_shape(c->shape.value_or(Json::object()))
                                                      .value_or(Json::object()))
                                     : QVariantMap()},
        {"mask", [&] { // la primera máscara en el cabezal (con sus keyframes), también las de ajuste
             const auto masks = core::clip_masks_at(*c, std::max(0.0, local), true);
             return masks.empty() ? QVariantMap() : to_variant(masks.front());
         }()},
        {"maskCount", static_cast<int>(c->masks.size())},
        {"beats", beats_variant(*c)}, // {count, bpm, every} o nada
        {"vfx", [&] { // la pila con lo que pinta el inspector: nombre y parámetros del catálogo
             QVariantList out;
             const auto catalog = core::fx_catalog();
             for (const Json& e : vfx_list(c)) {
                 const Json* fx = catalog ? catalog->effect(e.value("effect", Json())) : nullptr;
                 QVariantMap row = to_variant(e);
                 row["name"] = fx ? qstr(fx->value("name", "")) : "Desconocido (" + qstr(e.value("effect", "")) + ")";
                 QVariantList specs;
                 for (const Json& p : fx ? fx->value("params", Json::array()) : Json::array()) {
                     specs << to_variant(p);
                 }
                 row["specs"] = specs;
                 out << row;
             }
             return out;
         }()},
        {"animations", [&] {
             QVariantMap out;
             if (c->animations && c->animations->is_object()) {
                 for (const auto& [slot, v] : c->animations->items()) {
                     out[qstr(slot)] = qstr(v.is_object() ? v.value("id", "") : "");
                     if (v.is_object() && v.contains("dur") && v.at("dur").is_number()) {
                         out[qstr(slot) + "Dur"] = v.at("dur").get<double>();
                     }
                 }
             }
             return out;
         }()},
        {"curve", [&] {
             const auto k = core::target_interp_item(*c, std::max(0.0, local), fps());
             if (!k) {
                 return QVariant(QString());
             }
             QVariantList bez;
             for (const double v : core::interp_bezier(*k)) {
                 bez << v;
             }
             const auto items = core::normalize_kf_items(
                 c->keyframes ? c->keyframes->value("items", Json::array()) : Json::array(), ids_);
             return QVariant(QVariantMap{
                 {"kind", qstr(std::string(core::interp_name(core::normalize_interp(k->value("interpolation", Json())))))},
                 {"bezier", bez},
                 {"first", !items.empty() && items.front().value("id", Json()) == k->value("id", Json())}});
         }()},
        {"flipH", c->flip_h},
        {"flipV", c->flip_v},
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
        preview_.request(playhead_);
        player_.seeked(playhead_); // si no lo mueve el reproductor, su reloj salta ahí
        request_crop_frame();      // con el modal Recortar de un vídeo abierto
        emit playheadChanged();
        if (!selection_.ids.empty() && !player_.playing()) {
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
    selected_track_.clear();
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
    selected_track_.clear();
    emit selectionChanged();
}

void EditorModel::selectTrack(const QString& id) {
    if (track(id)) {
        selection_ = {};
        selected_track_ = id.toStdString();
        emit selectionChanged();
    }
}

QVariantMap EditorModel::selectedTrack() const {
    const core::TimelineTrack* t = selected_track_.empty() ? nullptr : track(qstr(selected_track_));
    if (!t || !selection_.ids.empty()) {
        return {};
    }
    const Json style = t->kind == "text" ? t->style.value_or(core::default_text_style()) : Json::object();
    return {{"id", qstr(t->id)},
            {"kind", qstr(t->kind)},
            {"name", qstr(t->name)},
            {"style", to_variant(style)},
            {"themeId", qstr(core::selected_subtitle_theme_id(style).value_or(""))},
            {"lookId", qstr(core::active_look_id(style).value_or(""))}};
}

template <class Restyle>
bool EditorModel::restyle_track(Restyle restyle, bool theme) {
    const core::TimelineTrack* t = selected_track_.empty() || !selection_.ids.empty() ? nullptr : track(qstr(selected_track_));
    if (!t || t->kind != "text") {
        return false;
    }
    const std::string id = t->id;
    core::Timeline next = *session_->timeline();
    for (auto& tr : next.tracks) {
        if (tr.id == id) {
            tr.style = restyle(tr.style.value_or(theme ? Json::object() : core::default_text_style()));
        }
    }
    std::vector<std::string> changed;
    for (auto& c : next.clips) {
        if (c->kind == "text" && c->track_id == id) {
            c.mut().style = restyle(c->style.value_or(Json::object()));
            changed.push_back(c->id);
        }
    }
    commit(std::move(next), "track-style", changed);
    emit selectionChanged();
    return true;
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
void EditorModel::probe_material(const QString& key) const {
    const auto it = payloads_.find(key);
    if (it == payloads_.end()) {
        return;
    }
    Json& payload = it->second;
    const double d = core::js_number(payload.value("duration", Json(0)));
    const std::string kind = payload.value("asset_kind", std::string());
    if (!(std::isnan(d) || d == 0) || (kind != "sfx" && kind != "audios" && kind != "clips")) {
        return;
    }
    auto [known, fresh] = probed_.try_emplace(key, 0.0);
    if (fresh) {
        const std::string file = payload.value("filename", std::string());
        // El SFX por su ruta en la carpeta de SFX; el de una colección, por «<colección>/<ruta>».
        const auto path = key.startsWith("sfx:")   ? core::sfx_resolve(sfx_dir(), file)
                          : key.startsWith("col:") ? core::collection_resolve(collections_base(), file)
                                                   : std::nullopt;
#ifdef GC_STUDIO_PREVIEW
        known->second = path ? media::probe_duration(*path) : 0.0;
#else
        (void)path;
#endif
    }
    if (known->second > 0) {
        payload["duration"] = known->second;
    }
}

std::optional<std::pair<Json, core::Drop>> EditorModel::planMaterialDrop(const QString& key, double x,
                                                                         double y, bool alt) const {
    probe_material(key);
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
    const core::TimelineTrack* lane = nullptr;
    for (const auto& t : next.tracks) {
        lane = t.id == track_id ? &t : lane;
    }
    auto made = core::clip_from_json(
        payload.value("asset_kind", std::string()) == "text"
            ? core::place_text_clip(payload, lane && lane->style ? *lane->style : Json(), track_id, land.start, fit, ids_)
            : core::place_asset_clip(payload, track_id, land.start, fit, ids_));
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
    // Lo que entra o cambia se queda como objeto libre en el mismo paso (el efecto de VideoEditor.jsx).
    for (std::string& id : free_layouts(next)) {
        if (std::ranges::find(changed, id) == changed.end()) {
            changed.push_back(std::move(id));
        }
    }
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

void EditorModel::patch_marker(const QString& id, const std::optional<Json>& patch) {
    if (!session_) {
        return;
    }
    core::Timeline next = *session_->timeline();
    std::vector<Json> list;
    for (Json m : next.markers) {
        if (m.is_object() && m.value("id", Json()) == id.toStdString()) {
            if (!patch) {
                continue;
            }
            for (const auto& [key, value] : patch->items()) {
                if (value.is_null()) {
                    m.erase(key);
                } else {
                    m[key] = value;
                }
            }
        }
        list.push_back(std::move(m));
    }
    next.markers = core::normalize_markers(list, ids_);
    session_->replace(std::move(next), "markers", {});
}

void EditorModel::moveMarker(const QString& id, double t) {
    patch_marker(id, Json{{"t", core::snap_to_frame(std::max(0.0, t), fps())}});
}

void EditorModel::renameMarker(const QString& id, const QString& label) {
    const QString name = label.trimmed();
    patch_marker(id, Json{{"label", name.isEmpty() ? Json() : Json(name.toStdString())}});
}

void EditorModel::removeMarker(const QString& id) { patch_marker(id, std::nullopt); }

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
    const Json json = to_json(patch);
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
        return core::transform_pos_patch(c, x, y, static_cast<double>(tl.width), static_cast<double>(tl.height));
    });
}

void EditorModel::setScalePct(double pct) {
    pose_each("transform", [&](const core::TimelineClip& c) { return core::transform_scale_patch(c, pct, height_scale(c)); });
}

void EditorModel::setRotation(double deg) {
    pose_each("transform", [&](const core::TimelineClip&) { return Json{{"rotation", deg}}; });
}

void EditorModel::resetTransform() {
    pose_each("transform", [&](const core::TimelineClip& c) { return core::transform_reset_patch(c, height_scale(c)); });
}

std::optional<std::filesystem::path> EditorModel::media_path(const core::TimelineClip& c) const {
    const auto project = store_.get_project(project_id_);
    if (!project || !*project) {
        return std::nullopt;
    }
    const auto data_dir = store_.file().parent_path();
    const Json settings = settings_ ? settings_->load() : Json::object();
    return core::clip_media_path(**project, c, {store_.clips_dir(), data_dir,
                                                core::collections_root(settings, assets_dir(data_dir) / "material")});
}

std::optional<std::pair<double, double>> EditorModel::source_size(const core::TimelineClip& c) const {
    if (c.kind != "video" && c.kind != "image") {
        return std::nullopt;
    }
    // La clave del material: ámbito, tipo, id y archivo (el «|» no cabe en un nombre de archivo de Windows).
    const std::string key = c.asset_scope + '|' + c.asset_kind + '|' + c.asset_id + '|' + c.filename;
    if (const auto it = sizes_.find(key); it != sizes_.end()) {
        return it->second;
    }
    std::optional<std::pair<double, double>> size;
    if (const auto path = media_path(c)) {
#ifdef GC_STUDIO_PREVIEW
        if (const auto first = media::frame_at(*path, 0.0); first && first->has_value()) {
            size = std::pair<double, double>((*first)->width, (*first)->height);
        }
#endif
        if (!size && c.kind == "image") { // sin gc_media (o si FFmpeg no la lee)
            const QSize s = QImageReader(QString::fromStdString(core::path_to_utf8(*path))).size();
            if (!s.isEmpty()) {
                size = std::pair<double, double>(s.width(), s.height());
            }
        }
    }
    sizes_[key] = size;
    return size;
}

double EditorModel::height_scale(const core::TimelineClip& c) const {
    const auto size = source_size(c);
    return size && size->second > 0 && session_ ? static_cast<double>(session_->timeline()->height) / size->second : 1.0;
}

std::vector<std::string> EditorModel::free_layouts(core::Timeline& tl) const {
    std::vector<std::string> changed;
    for (auto& c : tl.clips) {
        if (!core::needs_free_layout(*c)) {
            continue;
        }
        const auto size = source_size(*c);
        if (!size) {
            continue;
        }
        auto free = core::free_layout(*c, size->first, size->second, static_cast<double>(tl.width),
                                      static_cast<double>(tl.height), playhead_);
        if (free && *free) {
            c.mut() = std::move(**free);
            changed.push_back(c->id);
        }
    }
    return changed;
}

QVariantMap EditorModel::openCrop() {
    const core::TimelineClip* c = selection_.ids.empty() ? nullptr : clip(qstr(selection_.ids.front()));
    if (!c || (c->kind != "video" && c->kind != "image") || !core::is_overlay(*c)) {
        return {};
    }
    // canCropClip: el medio tiene que estar, con su tamaño (el de un vídeo, con gc_media).
    const auto path = media_path(*c);
    const auto size = path ? source_size(*c) : std::nullopt;
    if (!size || size->first <= 0 || size->second <= 0) {
        setError(c->kind == "video" ? "No se puede leer el vídeo del clip." : "No se puede leer la imagen del clip.");
        return {};
    }
    const double src_aspect = size->first / size->second;
    const QString file = QString::fromStdString(core::path_to_utf8(*path));
    const double end = core::ui_clip_end(*c);
    if (playhead_ < c->start || playhead_ >= end) {
        setPlayhead(c->start + 0.02);
    }
    const double local = std::clamp(playhead_ - c->start, 0.0, end - c->start);
    const core::CropWindow crop =
        core::crop_window(*c, src_aspect, aspect(), core::source_time(*c, playhead_, c->start), local);
    crop_clip_ = c->id;
    crop_editor_.load({{"srcAspect", src_aspect}, {"outAspect", aspect()}, {"cx", crop.cx}, {"cy", crop.cy},
                       {"wf", crop.wf}, {"hf", crop.hf},
                       {"rotation", core::js_number(c->transform.value_or(Json::object()).value("rotation", Json(0)))}});
    crop_media_.clear();
    if (c->kind == "video") { // como la vista previa: el proxy si lo hay
        const auto& proxies = proxy_maker_.proxies();
        const auto proxy = proxies.find(*path);
        crop_media_ = proxy != proxies.end() ? proxy->second.file : *path;
        request_crop_frame();
    }
    // De una imagen, el archivo (la pinta CropModal); de un vídeo, cropFrame.
    return {{"clipId", qstr(c->id)},
            {"kind", qstr(c->kind)},
            {"source", c->kind == "image" ? QUrl::fromLocalFile(file).toString() : QString()},
            {"start", c->start},
            {"duration", end - c->start}};
}

// EdCropModal: el <video> oculto del editor llevado al cabezal, en segundos del archivo dentro del tramo
// del clip (safeMediaTime lo acota en el hilo, con la duración del archivo).
void EditorModel::request_crop_frame() {
    const core::TimelineClip* c = crop_media_.empty() ? nullptr : clip(qstr(crop_clip_));
    if (c) {
        crop_frames_.request(crop_media_, core::source_time(*c, playhead_, c->start), fps());
    }
}

void EditorModel::closeCrop() {
    crop_clip_.clear(); // ni confirma ni pide más fotogramas
    crop_frames_.clear();
}

void EditorModel::confirmCrop() {
    const core::TimelineClip* c = clip(qstr(std::exchange(crop_clip_, {})));
    if (!c) {
        return;
    }
    const double local = std::max(0.0, playhead_ - c->start);
    auto next = core::confirm_crop(*c, crop_editor_.crop(), crop_editor_.rotation(), local,
                                   core::source_time(*c, playhead_, c->start), fps(), ids_);
    if (!ok(next)) {
        return;
    }
    core::Timeline tl = *session_->timeline();
    for (auto& x : tl.clips) {
        if (x->id == c->id) {
            x.mut() = std::move(*next);
        }
    }
    commit(std::move(tl), "crop", {c->id});
}

namespace {

// clipFavRef de lib/favorites.js: un SFX se marca por su archivo (o su id).
std::optional<std::string> sfx_fav_id(const core::TimelineClip& c) {
    if (c.asset_kind != "sfx") {
        return std::nullopt;
    }
    return c.filename.empty() ? c.asset_id : c.filename;
}

} // namespace

QVariantMap EditorModel::clipMenu(const QString& clipId) const {
    const core::TimelineClip* c = clip(clipId);
    if (!c) {
        return {};
    }
    const core::LayerInfo layer = core::clip_layer_info(session_->timeline()->clips, c->id);
    const auto fav_id = sfx_fav_id(*c);
    const Json favs = core::normalize_favorites(settings_ ? settings_->load().value("favorites", Json()) : Json());
    const Json& sfx = favs.value("sfx", Json::array());
    return {{"kind", qstr(c->kind)},
            {"disabled", c->disabled},
            {"canLayer", core::can_layer_clip(*c)},
            {"canBack", layer.can_back},
            {"canFront", layer.can_front},
            {"hasBeats", clip_beats_view(*c).has_value()}, // clipBeats(clip), aunque caigan fuera del tramo
            {"sfx", fav_id.has_value()},
            {"favorite", fav_id && std::ranges::find(sfx, Json(*fav_id)) != sfx.end()},
            {"canPasteAttrs", attr_clipboard_ && attr_clipboard_->id != c->id},
            {"canCaption", core::can_caption_clip(*c)}};
}

void EditorModel::moveLayer(const QString& clipId, const QString& action) {
    if (!clip(clipId)) {
        return;
    }
    core::Timeline next = *session_->timeline();
    next.clips = core::move_clip_layer(next.clips, clipId.toStdString(), action.toStdString());
    commit(std::move(next), "layer", {clipId.toStdString()});
}

// toggleClipFav: la estrella del SFX en los ajustes (favorites.sfx).
void EditorModel::toggleClipFavorite(const QString& clipId) {
    const core::TimelineClip* c = clip(clipId);
    const auto id = c ? sfx_fav_id(*c) : std::nullopt;
    if (!id || !settings_) {
        return;
    }
    Json favs = core::normalize_favorites(settings_->load().value("favorites", Json()));
    Json& sfx = favs["sfx"];
    if (const auto it = std::ranges::find(sfx, Json(*id)); it != sfx.end()) {
        sfx.erase(it);
    } else {
        sfx.push_back(*id);
    }
    if (ok(settings_->save(Json{{"favorites", favs}}))) {
        load_sfx(sfx_query_.first, sfx_query_.second);
        emit projectChanged();
    }
}

// applyGlobalTemplate: el estilo completo de este texto en todas las pistas y textos.
void EditorModel::applyGlobalTemplate(const QString& clipId) {
    const core::TimelineClip* c = clip(clipId);
    if (!c || c->kind != "text") {
        return;
    }
    const core::TimelineTrack* own = track(qstr(c->track_id));
    const Json tmpl = core::effective_text_style(own && own->style ? *own->style : Json(), c->style.value_or(Json()));
    core::Timeline next = *session_->timeline();
    std::vector<std::string> changed;
    for (auto& t : next.tracks) {
        if (t.kind == "text") {
            t.style = tmpl;
        }
    }
    for (auto& x : next.clips) {
        if (x->kind == "text") {
            x.mut().style = tmpl;
            changed.push_back(x->id);
        }
    }
    commit(std::move(next), "global-template", changed);
}

void EditorModel::removeBeats(const QString& clipId) {
    if (!clip(clipId)) {
        return;
    }
    core::Timeline next = *session_->timeline();
    for (auto& x : next.clips) {
        if (x->id == clipId.toStdString()) {
            x.mut().beats.reset();
        }
    }
    commit(std::move(next), "beats", {clipId.toStdString()});
}

void EditorModel::toggleFlip(const QString& axis) {
    const core::TimelineClip* primary = selection_.ids.empty() ? nullptr : clip(qstr(selection_.ids.front()));
    if (!primary) {
        return;
    }
    const bool vertical = axis == "v";
    const bool next = vertical ? !primary->flip_v : !primary->flip_h;
    core::Timeline tl = *session_->timeline();
    for (auto& c : tl.clips) {
        if (c->kind != "audio" && std::ranges::find(selection_.ids, c->id) != selection_.ids.end()) {
            (vertical ? c.mut().flip_v : c.mut().flip_h) = next;
        }
    }
    commit(std::move(tl), "flip", selection_.ids);
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

// patchClipFx({speed}): primero los subtítulos enlazados de cada audio, luego la velocidad.
void EditorModel::setSpeed(double speed) {
    if (!session_ || selection_.ids.empty()) {
        return;
    }
    core::Timeline next = *session_->timeline();
    for (const auto& id : selection_.ids) {
        const auto it = std::ranges::find_if(next.clips, [&](const auto& c) { return c->id == id; });
        if (it != next.clips.end()) {
            const core::TimelineClip audio = it->get();
            core::apply_audio_speed_to_linked_text(next.clips, next.tracks, audio, speed);
        }
    }
    for (auto& c : next.clips) {
        if (c->kind != "text" && std::ranges::find(selection_.ids, c->id) != selection_.ids.end()) {
            c.mut().speed = std::clamp(speed, core::kSpeedMin, core::kSpeedMax);
        }
    }
    commit(std::move(next), "speed", selection_.ids);
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

void EditorModel::addText(const QString& themeId) {
    if (!session_) {
        return;
    }
    core::Timeline next = *session_->timeline();
    const core::TimelineClip* sel = selection_.ids.empty() ? nullptr : clip(qstr(selection_.ids.front()));
    const core::TimelineTrack* own = sel ? track(qstr(sel->track_id)) : nullptr;
    const auto text_track = std::ranges::find(next.tracks, std::string("text"), &core::TimelineTrack::kind);
    std::string tid;
    Json style;
    if (own && own->kind == "text") {
        tid = own->id;
        style = core::new_text_style(own->style.value_or(Json()));
    } else if (text_track != next.tracks.end()) {
        tid = text_track->id;
        style = core::new_text_style(text_track->style.value_or(Json()));
    } else { // ensureTextTrack: pista nueva con el tema de subtítulos; el texto aún no la ve
        core::TimelineTrack t = core::build_track("text", next.tracks, ids_.hex32().substr(0, 8));
        t.style = core::subtitle_style();
        tid = t.id;
        style = core::new_text_style(Json());
        core::insert_track(next.tracks, std::move(t));
    }
    const double dur = core::new_text_duration(next, playhead_);
    const Json* preset = core::theme_by_id(themeId.toStdString());
    const std::string sample = preset ? preset->value("sample", std::string()) : std::string();
    add_generated(std::move(next),
                  core::make_text_clip_json(tid, playhead_, dur, sample.empty() ? core::kDefaultText : sample,
                                            preset ? core::apply_theme_to_style(style, *preset) : style, ids_),
                  "add-text");
}

void EditorModel::add_generated(core::Timeline next, const Json& clip, const char* cause) {
    auto made = core::clip_from_json(clip);
    if (!ok(made)) {
        return;
    }
    const std::string id = made->id;
    next.clips.emplace_back(std::move(*made));
    selection_ = {{id}, id};
    commit(std::move(next), cause, {id});
    emit selectionChanged();
}

// targetTrackFor('video'): la pista del clip seleccionado si es de vídeo y no está bloqueada; si no,
// la primera así. Si ahí hay un clip, settle sube la figura a la pista de encima.
void EditorModel::addShape(const QString& type, const QString& label) {
    if (!session_) {
        return;
    }
    core::Timeline next = *session_->timeline();
    const core::TimelineClip* sel = selection_.ids.empty() ? nullptr : clip(qstr(selection_.ids.front()));
    const core::TimelineTrack* own = !selected_track_.empty() ? track(qstr(selected_track_))
                                     : sel                     ? track(qstr(sel->track_id))
                                                               : nullptr; // selTrackId
    const auto usable = [](const core::TimelineTrack& t) { return t.kind == "video" && !t.locked; };
    const auto first = std::ranges::find_if(next.tracks, usable);
    const std::string tid = own && usable(*own) ? own->id : first != next.tracks.end() ? first->id : "";
    if (tid.empty()) {
        return;
    }
    const Json preset = {{"type", type.toStdString()}, {"label", label.toStdString()}};
    add_generated(std::move(next), core::make_shape_clip_json(tid, playhead_, core::kShapeDefaultDuration, preset, ids_), "add-shape");
}

// topFreeVideoTrack: la pista de vídeo de arriba si está libre en ese tramo; si no, una nueva encima.
void EditorModel::addAdjustment() {
    if (!session_) {
        return;
    }
    constexpr double kDur = 5;
    core::Timeline next = *session_->timeline();
    const auto top = std::ranges::find_last(next.tracks, std::string("video"), &core::TimelineTrack::kind);
    const bool busy = top.empty() || top.front().locked ||
                      std::ranges::any_of(next.clips, [&](const auto& c) {
                          return c->track_id == top.front().id && c->start < playhead_ + kDur - 1e-6 &&
                                 core::ui_clip_end(*c) > playhead_ + 1e-6;
                      });
    std::string tid = busy ? "" : top.front().id;
    if (busy) {
        core::TimelineTrack t = core::build_track("video", next.tracks, ids_.hex32().substr(0, 8));
        tid = t.id;
        core::insert_track(next.tracks, std::move(t));
    }
    add_generated(std::move(next), core::make_adjustment_clip_json(tid, playhead_, kDur, ids_), "add-adjustment");
}

QVariantList EditorModel::curvePoints(const QString& kind, const QVariantList& bezier, int steps) {
    if (kind == "hold") {
        return {QPointF(0, 0), QPointF(0.999, 0), QPointF(1, 1)};
    }
    Json bez = Json::array();
    for (const QVariant& v : bezier) {
        bez.push_back(v.toDouble());
    }
    const core::Interp interp = core::normalize_interp(Json(kind.toStdString()));
    QVariantList out;
    for (int i = 0; i <= steps; ++i) {
        const double x = static_cast<double>(i) / steps;
        out << QPointF(x, core::ease_t(x, interp, bez));
    }
    return out;
}

void EditorModel::setCurve(const QString& mode, const QVariantList& bezier) {
    const core::TimelineClip* c = selection_.ids.empty() ? nullptr : clip(qstr(selection_.ids.front()));
    const auto target = c ? core::target_interp_item(*c, std::max(0.0, playhead_ - c->start), fps()) : std::nullopt;
    if (!target) {
        return;
    }
    Json bez = core::interp_bezier(*target);
    if (bezier.size() == 4) {
        for (int i = 0; i < 4; ++i) {
            bez[static_cast<std::size_t>(i)] = core::js_fixed(bezier[i].toDouble(), 3);
        }
    }
    const core::KeyframePatch patch{.t = {},
                                    .interpolation = mode.toStdString(),
                                    .bezier = mode == "bezier" ? bez : Json(),
                                    .props = {}};
    core::Timeline next = *session_->timeline();
    for (auto& item : next.clips) {
        if (item->id == c->id) {
            core::patch_keyframe(item.mut(), target->at("id").get<std::string>(), patch, fps(), ids_);
        }
    }
    commit(std::move(next), "curve", {c->id});
}

// startKfDrag + moveKeyframe: `fraction` del ancho del clip; con keyframes en su tiempo local, si no
// en el de la fuente (encuadre). El cabezal va al keyframe.
void EditorModel::moveKeyframe(const QString& clipId, const QString& kfId, double fraction) {
    const core::TimelineClip* c = clip(clipId);
    if (!c || kfId.isEmpty()) {
        return;
    }
    const bool local = core::keyframes_enabled(*c);
    const double dur = core::ui_clip_duration(*c);
    const double t = local ? std::clamp(fraction * dur, 0.0, dur)
                           : std::clamp(c->in_point + fraction * (c->out_point - c->in_point), c->in_point, c->out_point);
    const double snapped = core::snap_to_frame(t, fps());
    const double head = local        ? c->start + snapped // sourceToTimeline en el encuadre
                        : c->reverse ? c->start + (c->out_point - snapped) / core::ui_clip_speed(*c)
                                     : c->start + (snapped - c->in_point) / core::ui_clip_speed(*c);
    core::TimelineClip moved = *c;
    core::move_keyframe(moved, kfId.toStdString(), snapped, fps(), ids_);
    if (core::to_json(moved) != core::to_json(*c)) { // un clic sin arrastrar solo lleva el cabezal
        core::Timeline next = *session_->timeline();
        for (auto& item : next.clips) {
            if (item->id == moved.id) {
                item = std::move(moved);
                break;
            }
        }
        commit(std::move(next), "move-keyframe", {clipId.toStdString()});
    }
    setPlayhead(head);
}

bool EditorModel::copyKeyframe() {
    const core::TimelineClip* c = selection_.ids.empty() ? nullptr : clip(qstr(selection_.ids.front()));
    if (!c || c->kind == "adjustment") {
        return false;
    }
    const auto board =
        core::copy_keyframe_at(*c, std::clamp(playhead_ - c->start, 0.0, core::ui_clip_duration(*c)), fps());
    if (board) {
        kf_board_ = board;
    }
    return board.has_value();
}

bool EditorModel::pasteKeyframe() {
    const core::TimelineClip* c = selection_.ids.empty() ? nullptr : clip(qstr(selection_.ids.front()));
    if (!c || c->kind == "adjustment" || !kf_board_) {
        return false;
    }
    const std::string id = c->id;
    const double t = std::clamp(playhead_ - c->start, 0.0, core::ui_clip_duration(*c));
    core::Timeline next = *session_->timeline();
    for (auto& item : next.clips) {
        if (item->id == id) {
            core::paste_keyframe_at(item.mut(), t, *kf_board_, std::nullopt, fps(), ids_);
        }
    }
    commit(std::move(next), "paste-keyframe", {id});
    return true;
}

bool EditorModel::duplicateKeyframe() {
    const core::TimelineClip* c = selection_.ids.empty() ? nullptr : clip(qstr(selection_.ids.front()));
    const double dur = c ? core::ui_clip_duration(*c) : 0;
    const double head = c ? std::clamp(playhead_ - c->start, 0.0, dur) : 0;
    const auto kf = c ? core::keyframe_id_at(*c, head, fps()) : std::nullopt;
    if (!kf) {
        return false;
    }
    const std::string id = c->id;
    const double target = std::clamp(head + 1, 0.0, dur); // el cabezal no está libre: un segundo después
    core::Timeline next = *session_->timeline();
    for (auto& item : next.clips) {
        if (item->id == id) {
            core::duplicate_keyframe_at(item.mut(), *kf, target, fps(), ids_);
        }
    }
    commit(std::move(next), "duplicate-keyframe", {id});
    return true;
}

bool EditorModel::copyAttributes() {
    const core::TimelineClip* c = selection_.ids.empty() ? nullptr : clip(qstr(selection_.ids.front()));
    if (!c) {
        return false;
    }
    attr_clipboard_ = *c;
    return true;
}

QVariantList EditorModel::pasteableGroups() const {
    static const std::map<std::string_view, const char*> kLabels{
        {"transform", "Posición, escala y giro"},       {"flip", "Voltear"},
        {"blend", "Opacidad y modo de fusión"},          {"animation", "Animación (keyframes)"},
        {"transitions", "Entrada y salida"},             {"animations", "Animaciones (entrada, salida, bucle)"},
        {"crop", "Recorte y encuadre"},                  {"effects", "Filtro, efectos y ajustes"},
        {"vfx", "Efectos animados"},                     {"mask", "Máscara"},
        {"chroma", "Croma y contorno"},                  {"style", "Estilo"},
        {"speed", "Velocidad"},                          {"audio", "Volumen y efectos de audio"}};
    QVariantList out;
    if (!attr_clipboard_) {
        return out;
    }
    std::vector<const core::TimelineClip*> targets;
    for (const auto& id : selection_.ids) {
        const core::TimelineClip* t = clip(qstr(id));
        if (t && t->id != attr_clipboard_->id) {
            targets.push_back(t);
        }
    }
    for (const auto group : core::kAttrGroupIds) {
        const auto count = std::ranges::count_if(
            targets, [&](const core::TimelineClip* t) { return core::group_applies(group, *attr_clipboard_, *t); });
        if (count > 0) {
            out << QVariantMap{{"id", QString::fromUtf8(group.data(), static_cast<qsizetype>(group.size()))},
                               {"label", QString::fromUtf8(kLabels.at(group))},
                               {"count", static_cast<int>(count)},
                               {"total", static_cast<int>(targets.size())}};
        }
    }
    return out;
}

// applyPasteAttrs: los subtítulos enlazados de un audio siguen la velocidad pegada.
void EditorModel::pasteAttributes(const QStringList& groups) {
    if (!session_ || !attr_clipboard_ || groups.isEmpty()) {
        return;
    }
    std::vector<std::string> picked;
    for (const QString& g : groups) {
        picked.push_back(g.toStdString());
    }
    const core::TimelineClip& src = *attr_clipboard_;
    const auto target = [&](const core::TimelineClip& c) {
        return c.id != src.id && std::ranges::find(selection_.ids, c.id) != selection_.ids.end();
    };
    core::Timeline next = *session_->timeline();
    if (std::ranges::find(picked, "speed") != picked.end()) {
        const auto before = next.clips;
        for (const auto& c : before) {
            if (target(*c) && c->kind == "audio" && core::group_applies("speed", src, *c)) {
                core::apply_audio_speed_to_linked_text(next.clips, next.tracks, *c, src.speed);
            }
        }
    }
    for (auto& c : next.clips) {
        if (!target(*c)) {
            continue;
        }
        auto pasted = core::paste_attributes(*c, src, picked, ids_);
        if (!ok(pasted)) {
            return;
        }
        auto made = core::clip_from_json(*pasted);
        if (!ok(made)) {
            return;
        }
        c = std::move(*made);
    }
    commit(std::move(next), "paste-attrs", selection_.ids);
}

void EditorModel::changeShape(const QVariantMap& patch) {
    if (!session_ || selection_.ids.empty()) {
        return;
    }
    const Json json = to_json(patch);
    core::Timeline next = *session_->timeline();
    for (auto& c : next.clips) {
        if (std::ranges::find(selection_.ids, c->id) != selection_.ids.end()) {
            core::change_shape(c.mut(), json, playhead_ - c->start, fps(), ids_);
        }
    }
    commit(std::move(next), "shape", selection_.ids);
}

namespace {

QVariantMap swatch(const Json& item, const Json& style) {
    const auto text = [&](const char* key, const char* fallback) {
        const Json& v = style.contains(key) ? style.at(key) : Json();
        return QString::fromStdString(v.is_string() ? v.get<std::string>() : fallback);
    };
    return {{"id", QString::fromStdString(item.at("id").get<std::string>())},
            {"name", QString::fromStdString(item.value("name", item.at("id").get<std::string>()))},
            {"color", text("color", "#ffffff")},
            {"stroke", style.value("border_width", Json(0)).is_number() && style.at("border_width").get<double>() > 0
                           ? text("border_color", "#000000")
                           : QString()},
            {"bg", text("bg", "none")},
            {"glow", style.value("glow", false) ? text("glow_color", "") : QString()},
            {"hot", text("highlight_color", "")}};
}

} // namespace

QVariantList EditorModel::textThemes() {
    QVariantList out;
    for (const Json& t : core::subtitle_themes()) {
        out << swatch(t, t.at("style"));
    }
    return out;
}

QVariantList EditorModel::colorFilters() {
    QVariantList out;
    for (const auto& f : core::color_filters()) {
        out << QVariantMap{{"id", QString::fromUtf8(f.id.data(), static_cast<qsizetype>(f.id.size()))},
                           {"label", QString::fromUtf8(f.label.data(), static_cast<qsizetype>(f.label.size()))},
                           {"group", QString::fromUtf8(f.group.data(), static_cast<qsizetype>(f.group.size()))},
                           {"swatch", filter_swatch(f.id, 1)}};
    }
    return out;
}

// onChangeFx({filters, look: 'none'}): la pila del clip principal va a todos los seleccionados (no textos).
void EditorModel::set_filters(const std::vector<core::ClipFilter>& stack) {
    Json list = Json::array();
    for (const auto& f : stack) {
        list.push_back({{"id", f.id}, {"amount", f.amount}});
    }
    fx_each("filters", [&](core::TimelineClip& c) {
        c.filters = list;
        c.look = "none";
    });
}

void EditorModel::setEffect(const QString& key, const QVariant& value) {
    const core::TimelineClip* c = selection_.ids.empty() ? nullptr : clip(qstr(selection_.ids.front()));
    if (!c) {
        return;
    }
    Json effects = c->effects && c->effects->is_object() ? *c->effects : Json::object();
    const Json patch = to_json(QVariantMap{{key, value}});
    for (const auto& [k, v] : patch.items()) {
        effects[k] = v;
    }
    fx_each("effects", [&](core::TimelineClip& item) { item.effects = effects; });
}

void EditorModel::setAudioFx(const QString& id, double value) {
    const core::TimelineClip* c = selection_.ids.empty() ? nullptr : clip(qstr(selection_.ids.front()));
    if (!c) {
        return;
    }
    const double n = std::isfinite(value) ? std::clamp(value, 0.0, 1.0) : 0;
    if (core::keyframes_enabled(*c)) {
        setPose({{id, n}});
        return;
    }
    Json fx = c->audio_fx && c->audio_fx->is_object() ? *c->audio_fx : Json::object();
    fx[id.toStdString()] = n;
    fx_each("audio-fx", [&](core::TimelineClip& item) { item.audio_fx = fx; });
}

template <class Edit>
void EditorModel::mask_edit(const char* cause, Edit edit) {
    const core::TimelineClip* c = selection_.ids.empty() ? nullptr : clip(qstr(selection_.ids.front()));
    if (!c || !core::maskable(*c)) {
        return;
    }
    const std::string id = c->id;
    core::Timeline next = *session_->timeline();
    for (auto& item : next.clips) {
        if (item->id == id) {
            edit(item.mut());
        }
    }
    commit(std::move(next), cause, {id});
}

void EditorModel::addMask(const QString& type) {
    mask_edit("mask", [&](core::TimelineClip& c) { core::add_mask(c, type.toStdString(), "clip", aspect(), ids_); });
    setMaskMode(true); // addMask de VideoEditor.jsx: la deja lista para moverla en la vista previa
}

void EditorModel::removeMask() {
    mask_edit("mask", [](core::TimelineClip& c) { core::remove_mask(c); });
    setMaskDraw(false);
}

void EditorModel::duplicateMask() {
    mask_edit("mask", [&](core::TimelineClip& c) { core::duplicate_mask(c, ids_); });
}

void EditorModel::changeMask(const QVariantMap& patch) {
    const Json p = to_json(patch);
    mask_edit("mask", [&](core::TimelineClip& c) { core::change_mask(c, p, aspect(), ids_); });
}

void EditorModel::commitMask(const QVariantMap& patch) {
    const Json p = to_json(patch);
    mask_edit("mask", [&](core::TimelineClip& c) { core::commit_mask(c, p, playhead_ - c.start, fps(), ids_); });
}

void EditorModel::setMaskMode(bool on) {
    if (on != mask_mode_) {
        mask_mode_ = on;
        emit maskModeChanged();
    }
}

void EditorModel::setMaskDraw(bool on) {
    if (on != mask_draw_) {
        mask_draw_ = on;
        emit maskModeChanged();
    }
}

void EditorModel::maskPanel(bool open) {
    setMaskMode(open);
    if (!open) {
        setMaskDraw(false);
    }
}

// activeMask de interactions.js: la primera máscara en el cabezal del clip elegido (también las de ajuste).
std::optional<Json> EditorModel::edit_mask() const {
    if (!mask_mode_ || !session_ || selection_.ids.empty()) {
        return std::nullopt;
    }
    const core::Timeline& tl = mask_live_ ? *mask_live_ : *session_->timeline();
    for (const auto& c : tl.clips) {
        if (c->id == selection_.ids.front()) {
            auto masks = core::clip_masks_at(*c, std::max(0.0, playhead_ - c->start), true);
            return masks.empty() ? std::nullopt : std::optional<Json>(std::move(masks.front()));
        }
    }
    return std::nullopt;
}

QVariantMap EditorModel::editMask() const {
    const auto mask = edit_mask();
    return mask ? to_variant(*mask) : QVariantMap();
}

bool EditorModel::maskPress(double x, double y, const QRectF& frame, double scale) {
    auto mask = edit_mask();
    if (!mask || frame.isEmpty() || !(scale > 0)) {
        return false;
    }
    MaskDrag drag{.clip = selection_.ids.front(),
                  .mode = {},
                  .mask = std::move(*mask),
                  .frame = {frame.x() * scale, frame.y() * scale, frame.width() * scale, frame.height() * scale},
                  .scale = scale,
                  .p0 = {x * scale, y * scale}};
    const bool brush = mask_draw_ && drag.mask.value("type", "") == "brush"; // pinta desde cualquier punto
    if (brush) {
        drag.mode = "brush";
        drag.points = drag.mask.contains("brush") ? drag.mask["brush"].value("points", Json::array()) : Json::array();
        if (auto pt = core::mask_brush_point(drag.mask, drag.frame, drag.p0, true, nullptr)) {
            drag.points.push_back(std::move(*pt));
        }
    } else {
        auto mode = core::mask_hit_mode(drag.p0.x, drag.p0.y, drag.mask, drag.frame);
        if (!mode) {
            return false;
        }
        drag.mode = std::move(*mode);
    }
    if (player_.playing()) {
        player_.pause(); // startMaskDrag: arrastrar para la reproducción
    }
    mask_drag_ = std::move(drag);
    if (brush) {
        apply_mask_live();
    }
    return true;
}

void EditorModel::maskMove(double x, double y) {
    if (!mask_drag_) {
        return;
    }
    MaskDrag& d = *mask_drag_;
    const core::Point p{x * d.scale, y * d.scale};
    if (d.mode == "brush") {
        auto pt = core::mask_brush_point(d.mask, d.frame, p, false, d.points.empty() ? nullptr : &d.points.back());
        if (!pt) {
            return; // a menos de 0,004 del anterior
        }
        d.points.push_back(std::move(*pt));
    } else {
        Json patch = core::mask_drag_patch(d.mode, d.mask, d.frame, d.p0, p);
        if (patch.empty()) {
            return;
        }
        d.patch = std::move(patch);
    }
    apply_mask_live();
}

void EditorModel::maskRelease() {
    if (!mask_drag_) {
        return;
    }
    const bool moved = mask_live_ != nullptr;
    mask_live_.reset();
    if (moved) { // un solo paso de deshacer: en React, el debounce del historial junta los del arrastre
        mask_edit("mask", [this](core::TimelineClip& c) { mask_drag_edit(c); });
    }
    mask_drag_.reset();
    if (session_) {
        preview_.setTimeline(session_->timeline());
        preview_.request(playhead_);
    }
    emit editMaskChanged();
}

// El parche del arrastre sobre el clip: commitMask (geometría animable: en un clip animado, keyframe en
// el cabezal) o, con el pincel, changeMask({brush: {...mask.brush, points}}).
void EditorModel::mask_drag_edit(core::TimelineClip& c) const {
    const MaskDrag& d = *mask_drag_;
    if (d.mode == "brush") {
        Json brush = d.mask.value("brush", Json::object());
        brush["points"] = d.points;
        core::change_mask(c, {{"brush", std::move(brush)}}, aspect(), ids_);
    } else {
        core::commit_mask(c, d.patch, playhead_ - c.start, fps(), ids_);
    }
}

// Lo arrastrado sobre la timeline de la sesión, sin guardarlo: la vista previa y la guía lo enseñan ya.
void EditorModel::apply_mask_live() {
    core::Timeline next = *session_->timeline();
    for (auto& item : next.clips) {
        if (item->id == mask_drag_->clip) {
            mask_drag_edit(item.mut());
        }
    }
    mask_live_ = std::make_shared<const core::Timeline>(std::move(next));
    preview_.setTimeline(mask_live_);
    preview_.request(playhead_);
    emit editMaskChanged();
}

int EditorModel::maskCursor(double x, double y, const QRectF& frame, double scale) const {
    const auto mask = edit_mask();
    if (!mask || frame.isEmpty() || !(scale > 0)) {
        return Qt::ArrowCursor;
    }
    const core::Rect f{frame.x() * scale, frame.y() * scale, frame.width() * scale, frame.height() * scale};
    static const std::map<std::string, Qt::CursorShape, std::less<>> kShapes{
        {"crosshair", Qt::CrossCursor},     {"move", Qt::SizeAllCursor},       {"rotate", Qt::OpenHandCursor},
        {"ew-resize", Qt::SizeHorCursor},   {"nwse-resize", Qt::SizeFDiagCursor},
        {"ns-resize", Qt::SizeVerCursor},   {"nesw-resize", Qt::SizeBDiagCursor}};
    const auto it = kShapes.find(core::mask_cursor(x * scale, y * scale, *mask, f, mask_draw_));
    return it != kShapes.end() ? it->second : Qt::ArrowCursor;
}

namespace {

// Un texto del Job (o «» si falta o es null).
std::string job_text(const Json& job, const char* key) {
    const auto it = job.find(key);
    return it != job.end() && it->is_string() ? it->get<std::string>() : std::string();
}

} // namespace

void EditorModel::requestSubtitles(const QString& clipId) {
    const core::TimelineClip* c = clip(clipId);
    if (!session_ || !c || !core::can_caption_clip(*c)) {
        return;
    }
    // getSettings().transcribe.model: el modelo de Whisper elegido en los ajustes, si hay.
    const Json settings = settings_ ? settings_->load() : Json::object();
    const auto tr = settings.find("transcribe");
    const Json model = tr != settings.end() && tr->is_object() ? tr->value("model", Json()) : Json();
    const bool use_model = (model.is_string() && !model.get<std::string>().empty()) || (!model.is_null() && !model.is_string());
    const bool video = c->kind == "video";
    QString path;
    Json body = Json::object();
    if (video) { // transcribeClip(projectId, clip.asset_id || clip.index, model)
        path = QString("/api/projects/%1/clips/%2/transcribe").arg(qstr(project_id_), qstr(c->asset_id));
        if (use_model) {
            body["model"] = model;
        }
    } else { // generateSubtitles: {filename, asset_kind, model?, asset_scope}
        path = QString("/api/projects/%1/subtitles").arg(qstr(project_id_));
        body = {{"filename", c->filename}, {"asset_kind", c->asset_kind}};
        if (use_model) {
            body["model"] = model;
        }
        body["asset_scope"] = c->asset_scope.empty() ? std::string("project") : c->asset_scope;
    }
    subtitle_src_ = *c;
    subtitle_job_ = {{"status", "pending"}, {"progress", 0.0}, {"message", QString()}, {"error", QString()}, {"video", video}};
    emit subtitleJobChanged();
    subtitle_jobs_.start(path, body, [this, video](const Json& job) { subtitle_update(job, video); });
}

void EditorModel::subtitle_update(const Json& job, bool video) {
    const std::string status = job_text(job, "status");
    const auto transcript = job.find("transcript");
    if (status == "done") {
        if (transcript != job.end() && transcript->is_object() && subtitle_src_) {
            add_subtitles(*subtitle_src_, *transcript);
        }
        subtitle_src_.reset();
        subtitle_job_.clear();
    } else {
        if (status == "error") {
            subtitle_src_.reset();
        }
        const auto progress = job.find("progress");
        subtitle_job_ = {{"status", qstr(status)},
                         {"progress", progress != job.end() && progress->is_number() ? progress->get<double>() : 0.0},
                         {"message", qstr(job_text(job, "message"))},
                         {"error", qstr(job_text(job, "error"))},
                         {"video", video}};
    }
    emit subtitleJobChanged();
}

// buildSubtitleClips de useSubtitles: con el estilo de la primera pista de texto (o el de subtítulo), en la
// pista de ensureTextTrack (la elegida si es de texto, la primera de texto o una nueva con ese estilo).
void EditorModel::add_subtitles(const core::TimelineClip& src, const Json& transcript) {
    if (!session_) {
        return;
    }
    core::Timeline next = *session_->timeline();
    const auto text_track = std::ranges::find(next.tracks, std::string("text"), &core::TimelineTrack::kind);
    const Json style = text_track != next.tracks.end() && text_track->style ? *text_track->style : core::subtitle_style();
    const auto chosen = std::ranges::find(next.tracks, selected_track_, &core::TimelineTrack::id);
    std::string tid;
    bool created = false;
    if (chosen != next.tracks.end() && chosen->kind == "text") {
        tid = chosen->id;
    } else if (text_track != next.tracks.end()) {
        tid = text_track->id;
    } else {
        core::TimelineTrack t = core::build_track("text", next.tracks, ids_.hex32().substr(0, 8));
        t.style = style;
        tid = t.id;
        core::insert_track(next.tracks, std::move(t));
        created = true;
    }
    const auto segments = transcript.find("segments");
    std::vector<core::TimelineClip> news =
        core::caption_text_clips(src, segments != transcript.end() ? *segments : Json::array(), tid, style, transcript,
                                 core::source_ref_of(src), ids_);
    if (news.empty() && !created) {
        return;
    }
    std::vector<std::string> changed;
    changed.reserve(news.size());
    for (core::TimelineClip& c : news) {
        changed.push_back(c.id);
        next.clips.emplace_back(std::move(c));
    }
    commit(std::move(next), "subtitles", std::move(changed));
}

void EditorModel::dismissSubtitleJob() {
    if (!subtitle_job_.isEmpty()) {
        subtitle_job_.clear();
        emit subtitleJobChanged();
    }
}

void EditorModel::followMaskFace() {
    const core::TimelineClip* c = selection_.ids.empty() ? nullptr : clip(qstr(selection_.ids.front()));
    if (!session_ || !c || c->kind != "video" || core::clip_masks(*c).empty() || !mask_follow_.isEmpty()) {
        return;
    }
    if ((c->asset_scope.empty() ? std::string("project") : c->asset_scope) != "project" || c->asset_kind != "clips") {
        set_notice("error", "Seguir solo funciona con vídeos del material del proyecto.");
        return;
    }
    const auto size = source_size(*c); // mediaSize del <video>: sin él, nada
    if (!size || !(size->first > 0) || !(size->second > 0)) {
        return;
    }
    follow_clip_ = c->id;
    follow_size_ = *size;
    mask_follow_ = {{"clipId", qstr(c->id)}, {"progress", 0.02}, {"message", "Buscando la cara…"}};
    emit maskFollowChanged();
    face_jobs_.start(QString("/api/projects/%1/clips/%2/face-track")
                         .arg(qstr(project_id_), QString::fromLatin1(QUrl::toPercentEncoding(qstr(c->asset_id)))),
                     {{"start", c->in_point}, {"end", c->out_point}}, [this](const Json& job) { follow_update(job); });
}

void EditorModel::follow_update(const Json& job) {
    const std::string status = job_text(job, "status");
    if (status != "done" && status != "error") { // progress: job.progress || el de antes; message, igual
        const auto progress = job.find("progress");
        if (progress != job.end() && progress->is_number() && progress->get<double>() != 0) {
            mask_follow_["progress"] = progress->get<double>();
        }
        if (const std::string message = job_text(job, "message"); !message.empty()) {
            mask_follow_["message"] = qstr(message);
        }
        emit maskFollowChanged();
        return;
    }
    mask_follow_.clear();
    emit maskFollowChanged();
    if (status == "error") {
        const std::string error = job_text(job, "error");
        set_notice("error", error.empty() ? QString("No se pudo seguir la cara.") : qstr(error));
        return;
    }
    const core::TimelineClip* fresh = clip(qstr(follow_clip_));
    const auto prep = job.find("reframe_prep");
    const Json track = prep != job.end() && prep->is_object() ? prep->value("track", Json::array()) : Json::array();
    const auto keys = fresh ? core::follow_track_mask_keys(*fresh, track, follow_size_.first, follow_size_.second,
                                                           static_cast<double>(session_->timeline()->width),
                                                           static_cast<double>(session_->timeline()->height))
                            : std::vector<core::MaskFollowKey>();
    if (keys.empty()) {
        set_notice("error", "No se detectó ninguna cara en este tramo.");
        return;
    }
    core::Timeline next = *session_->timeline();
    for (auto& item : next.clips) {
        if (item->id == follow_clip_) {
            const double src = core::source_time(*item, playhead_, item->start); // srcTOf
            core::apply_follow_keys(item.mut(), keys, src, fps(), ids_);
        }
    }
    commit(std::move(next), "mask-follow", {follow_clip_});
    set_notice("success", QString("La máscara sigue la cara (%1 keyframes).").arg(keys.size()));
}

QVariantList EditorModel::vfxCatalog() {
    QVariantList out;
    const auto catalog = core::fx_catalog();
    if (!catalog) {
        return out;
    }
    std::map<std::string, std::string> cats;
    for (const Json& c : catalog->data().value("categories", Json::array())) {
        cats[c.value("id", "")] = c.value("label", "");
    }
    for (const Json& e : catalog->data().value("effects", Json::array())) {
        out << QVariantMap{{"id", qstr(e.value("id", ""))},
                           {"name", qstr(e.value("name", ""))},
                           {"category", qstr(cats[e.value("category", "")])},
                           {"description", qstr(e.value("description", ""))}};
    }
    return out;
}

QVariantMap EditorModel::shapeThumb(const QString& type) {
    const Json shape = core::default_shape(type.toStdString());
    const core::ShapeGeometry geo = core::shape_geometry(type.toStdString(), shape);
    const auto lines = [](const std::vector<core::Polyline>& list) {
        QVariantList out;
        for (const core::Polyline& line : list) {
            QVariantList pts;
            for (const core::Point& p : line) {
                pts << QPointF(p.x, p.y);
            }
            out << QVariant(pts);
        }
        return out;
    };
    const Json& fill = shape.value("fill", Json("#e53935"));
    return {{"fills", lines(geo.fills)},
            {"strokes", lines(geo.strokes)},
            {"fill", fill.is_string() && fill.get<std::string>() != "none" ? qstr(fill.get<std::string>()) : QString()},
            {"stroke", qstr(shape.value("stroke", Json("#fff")).is_string() ? shape.value("stroke", "#fff") : "#fff")}};
}

QVariantList EditorModel::vfxCategories() {
    QVariantList out;
    if (const auto catalog = core::fx_catalog()) {
        for (const Json& c : catalog->data().value("categories", Json::array())) {
            out << QVariantMap{{"id", qstr(c.value("id", ""))}, {"label", qstr(c.value("label", ""))}};
        }
    }
    return out;
}

QVariantList EditorModel::searchVfx(const QString& q, const QString& category) const {
    const auto norm = [](QString text) { // norm de fx/registry.js: minúsculas y sin diacríticos
        text = text.toLower().normalized(QString::NormalizationForm_D);
        text.removeIf([](QChar c) { return c.unicode() >= 0x300 && c.unicode() <= 0x36f; });
        return text;
    };
    const QStringList words = norm(q).trimmed().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    const auto catalog = core::fx_catalog();
    QVariantList out;
    for (const QVariant& v : vfxCatalog()) {
        const QVariantMap e = v.toMap();
        const Json* fx = catalog ? catalog->effect(Json(e["id"].toString().toStdString())) : nullptr;
        if (!fx || fx->value("engine", "") == "generator" ||
            (!category.isEmpty() && qstr(fx->value("category", "")) != category)) {
            continue;
        }
        QStringList hay{e["id"].toString(), e["name"].toString(), e["description"].toString()};
        for (const Json& tag : fx->value("tags", Json::array())) {
            hay << qstr(tag.is_string() ? tag.get<std::string>() : tag.dump());
        }
        const QString text = norm(hay.join(' '));
        if (std::ranges::all_of(words, [&](const QString& w) { return text.contains(w); })) {
            out << e;
        }
    }
    return out;
}

// addVfx: el efecto al final de la pila de los clips visuales seleccionados.
void EditorModel::addVfx(const QString& effectId) {
    if (!session_ || selection_.ids.empty()) {
        return;
    }
    core::Timeline next = *session_->timeline();
    for (auto& c : next.clips) {
        const bool visual = c->kind == "video" || c->kind == "image" || c->kind == "motion";
        if (!visual || std::ranges::find(selection_.ids, c->id) == selection_.ids.end()) {
            continue;
        }
        if (auto entry = core::make_vfx_entry(effectId.toStdString(), ids_)) {
            Json list = c->vfx && c->vfx->is_array() ? *c->vfx : Json::array();
            list.push_back(std::move(*entry));
            c.mut().vfx = std::move(list);
        }
    }
    commit(std::move(next), "vfx", selection_.ids);
}

// setClipVfx: la pila del clip principal; vacía, nula.
void EditorModel::set_vfx(Json list) {
    const core::TimelineClip* c = selection_.ids.empty() ? nullptr : clip(qstr(selection_.ids.front()));
    if (!c) {
        return;
    }
    const std::string id = c->id;
    core::Timeline next = *session_->timeline();
    for (auto& item : next.clips) {
        if (item->id == id) {
            item.mut().vfx = list.empty() ? std::nullopt : std::optional(std::move(list));
            break;
        }
    }
    commit(std::move(next), "vfx", {id});
}

void EditorModel::setVfxParam(int index, const QString& param, const QVariant& value) {
    Json list = vfx_list(selection_.ids.empty() ? nullptr : clip(qstr(selection_.ids.front())));
    if (index >= 0 && std::cmp_less(index, list.size())) {
        const Json v = to_json(QVariantMap{{param, value}});
        list[static_cast<std::size_t>(index)]["params"][param.toStdString()] = v.at(param.toStdString());
        set_vfx(std::move(list));
    }
}

void EditorModel::patchVfx(int index, const QVariantMap& patch) {
    Json list = vfx_list(selection_.ids.empty() ? nullptr : clip(qstr(selection_.ids.front())));
    if (index >= 0 && std::cmp_less(index, list.size())) {
        const Json p = to_json(patch);
        for (const auto& [key, value] : p.items()) {
            list[static_cast<std::size_t>(index)][key] = value;
        }
        set_vfx(std::move(list));
    }
}

void EditorModel::moveVfx(int index, int step) {
    Json list = vfx_list(selection_.ids.empty() ? nullptr : clip(qstr(selection_.ids.front())));
    const int to = index + step;
    if (index >= 0 && to >= 0 && std::cmp_less(index, list.size()) && std::cmp_less(to, list.size())) {
        std::swap(list[static_cast<std::size_t>(index)], list[static_cast<std::size_t>(to)]);
        set_vfx(std::move(list));
    }
}

void EditorModel::removeVfx(int index) {
    Json list = vfx_list(selection_.ids.empty() ? nullptr : clip(qstr(selection_.ids.front())));
    if (index >= 0 && std::cmp_less(index, list.size())) {
        list.erase(static_cast<std::size_t>(index));
        set_vfx(std::move(list));
    }
}

QVariantList EditorModel::animationSlots() {
    QVariantList out;
    const auto catalog = core::fx_catalog();
    if (!catalog) {
        return out;
    }
    const Json& data = catalog->data();
    for (const Json& slot : data.value("slots", Json::array())) {
        QVariantList items;
        for (const Json& a : data.value("anims", Json::array())) {
            if (a.value("slot", "") == slot.value("id", "")) {
                items << QVariantMap{{"id", qstr(a.value("id", ""))},
                                     {"name", qstr(a.value("name", ""))},
                                     {"dur", a.value("dur", 0.0)}};
            }
        }
        out << QVariantMap{{"id", qstr(slot.value("id", ""))}, {"label", qstr(slot.value("label", ""))}, {"items", items}};
    }
    return out;
}

void EditorModel::setAnimation(const QString& slot, const QString& id) {
    if (!session_ || selection_.ids.empty()) {
        return;
    }
    const auto catalog = core::fx_catalog();
    const Json* anim = catalog && !id.isEmpty() ? catalog->anim(id.toStdString()) : nullptr;
    Json value;
    if (anim) { // EdAnimPicker: la duración que ya tenía la ranura del clip principal se conserva
        const core::TimelineClip* primary = clip(qstr(selection_.ids.front()));
        const Json old = primary && primary->animations && primary->animations->is_object()
                             ? primary->animations->value(slot.toStdString(), Json())
                             : Json();
        value = {{"id", id.toStdString()}};
        if (slot != "combo") {
            value["dur"] = old.is_object() && old.contains("dur") && !old.at("dur").is_null() ? old.at("dur")
                                                                                              : anim->value("dur", Json());
        }
    }
    core::Timeline next = *session_->timeline();
    for (auto& c : next.clips) {
        const bool animable = c->kind == "video" || c->kind == "image" || c->kind == "motion" || c->kind == "text" ||
                              c->kind == "shape";
        if (!animable || std::ranges::find(selection_.ids, c->id) == selection_.ids.end()) {
            continue;
        }
        Json anims = c->animations && c->animations->is_object() ? *c->animations : Json::object();
        if (value.is_null()) {
            anims.erase(slot.toStdString());
        } else {
            anims[slot.toStdString()] = value;
        }
        c.mut().animations = anims.empty() ? std::nullopt : std::optional(anims);
    }
    commit(std::move(next), "animation", selection_.ids);
}

void EditorModel::setAnimationDur(const QString& slot, double dur) {
    const core::TimelineClip* c = selection_.ids.empty() ? nullptr : clip(qstr(selection_.ids.front()));
    const Json entry = c && c->animations && c->animations->is_object() ? c->animations->value(slot.toStdString(), Json())
                                                                       : Json();
    if (!entry.is_object() || !entry.contains("id") || slot == "combo") {
        return;
    }
    const double max = slot == "loop" ? 8 : 4;
    const Json value = {{"id", entry.at("id")}, {"dur", std::clamp(dur, 0.1, max)}};
    core::Timeline next = *session_->timeline();
    for (auto& item : next.clips) {
        if (std::ranges::find(selection_.ids, item->id) != selection_.ids.end() && item->animations) {
            Json anims = *item->animations;
            anims[slot.toStdString()] = value;
            item.mut().animations = anims;
        }
    }
    commit(std::move(next), "animation", selection_.ids);
}

void EditorModel::setAdjustIntensity(double value) {
    fx_each("opacity", [&](core::TimelineClip& c) {
        if (c.kind == "adjustment") {
            c.opacity = std::clamp(value, 0.0, 1.0);
        }
    });
}

void EditorModel::toggleFilter(const QString& id) {
    const core::TimelineClip* c = selection_.ids.empty() ? nullptr : clip(qstr(selection_.ids.front()));
    if (!c) {
        return;
    }
    auto stack = core::clip_filters(*c);
    const auto hit = std::ranges::find(stack, id.toStdString(), &core::ClipFilter::id);
    if (hit != stack.end()) {
        stack.erase(hit);
    } else {
        stack.push_back({id.toStdString(), 1});
    }
    set_filters(stack);
}

void EditorModel::setFilterAmount(int index, double amount) {
    const core::TimelineClip* c = selection_.ids.empty() ? nullptr : clip(qstr(selection_.ids.front()));
    auto stack = c ? core::clip_filters(*c) : std::vector<core::ClipFilter>{};
    if (index >= 0 && std::cmp_less(index, stack.size())) {
        stack.at(static_cast<std::size_t>(index)).amount = amount;
        set_filters(stack);
    }
}

void EditorModel::moveFilter(int index, int step) {
    const core::TimelineClip* c = selection_.ids.empty() ? nullptr : clip(qstr(selection_.ids.front()));
    auto stack = c ? core::clip_filters(*c) : std::vector<core::ClipFilter>{};
    const int to = index + step;
    if (index >= 0 && to >= 0 && std::cmp_less(index, stack.size()) && std::cmp_less(to, stack.size())) {
        std::swap(stack.at(static_cast<std::size_t>(index)), stack.at(static_cast<std::size_t>(to)));
        set_filters(stack);
    }
}

QVariantList EditorModel::textLooks() {
    QVariantList out;
    for (const Json& l : core::text_looks()) {
        out << swatch(l, l.at("style"));
    }
    return out;
}

void EditorModel::fragmentText() {
    if (!session_) {
        return;
    }
    core::Timeline next = *session_->timeline();
    const core::TimelineClip* c = selection_.ids.empty() ? nullptr : clip(qstr(selection_.ids.front()));
    const core::TimelineTrack* t = !c && !selected_track_.empty() ? track(qstr(selected_track_)) : nullptr;
    Result<core::ClipList> parts = core::ClipList{};
    if (c && c->kind == "text") {
        parts = core::split_one_text_clip(next.clips, c->id, c->style.value_or(Json()).value("max_words", Json()), ids_);
    } else if (t && t->kind == "text") {
        parts = core::split_track_text(next.clips, t->id, t->style.value_or(Json()).value("max_words", Json(8)), ids_);
    } else {
        return;
    }
    if (!ok(parts) || parts->size() == next.clips.size()) {
        return;
    }
    const auto index = c ? std::ranges::find(next.clips, c->id, [](const auto& x) { return x->id; }) - next.clips.begin() : -1;
    next.clips = std::move(*parts);
    if (index >= 0) { // el primer cuadro queda seleccionado
        const std::string first = next.clips[static_cast<std::size_t>(index)]->id;
        selection_ = {{first}, first};
    }
    commit(std::move(next), "fragment", selection_.ids);
    emit selectionChanged();
}


void EditorModel::toggleStyleKeyframe(const QString& section) {
    const core::TimelineClip* c = selection_.ids.empty() ? nullptr : clip(qstr(selection_.ids.front()));
    const auto keys = text_kf_sections().find(section.toStdString());
    if (!c || c->kind != "text" || keys == text_kf_sections().end()) {
        return;
    }
    const core::TimelineTrack* t = track(qstr(c->track_id));
    const Json track_style = t && t->style ? *t->style : Json();
    const std::string id = c->id;
    core::Timeline next = *session_->timeline();
    for (auto& item : next.clips) {
        if (item->id == id) {
            core::toggle_style_kf(item.mut(), track_style, playhead_ - item->start, keys->second, fps(), ids_);
        }
    }
    commit(std::move(next), "style-keyframe", {id});
}

QVariantList EditorModel::textFavorites() const {
    QVariantList out;
    if (!settings_) {
        return out;
    }
    const Json favs = core::normalize_favorites(settings_->load().value("favorites", Json()));
    for (const Json& f : favs["textStyles"]) {
        out << QVariantMap{{"id", qstr(f.value("id", ""))}, {"name", qstr(f.value("name", ""))}};
    }
    return out;
}

void EditorModel::saveTextFavorite() {
    const core::TimelineClip* c = selection_.ids.empty() ? nullptr : clip(qstr(selection_.ids.front()));
    const core::TimelineTrack* t = !c && !selected_track_.empty() ? track(qstr(selected_track_)) : nullptr;
    const Json style = c && c->kind == "text" ? c->style.value_or(Json::object())
                       : t && t->kind == "text" ? t->style.value_or(Json::object())
                                                : Json();
    if (!settings_ || style.is_null()) {
        return;
    }
    Json favs = core::normalize_favorites(settings_->load().value("favorites", Json()));
    favs["textStyles"].push_back(core::make_text_style_favorite(style, favs["textStyles"], ids_));
    if (ok(settings_->save({{"favorites", favs}}))) {
        emit favoritesChanged();
    }
}

void EditorModel::deleteTextFavorite(const QString& id) {
    if (!settings_) {
        return;
    }
    Json favs = core::normalize_favorites(settings_->load().value("favorites", Json()));
    Json kept = Json::array();
    for (const Json& f : favs["textStyles"]) {
        if (f.value("id", Json()) != id.toStdString()) {
            kept.push_back(f);
        }
    }
    favs["textStyles"] = kept;
    if (ok(settings_->save({{"favorites", favs}}))) {
        emit favoritesChanged();
    }
}

// applyTextFavorite: el estilo guardado entero a los textos seleccionados o a la pista elegida.
void EditorModel::applyTextFavorite(const QString& id) {
    if (!settings_ || !session_) {
        return;
    }
    Json style;
    const Json favs = core::normalize_favorites(settings_->load().value("favorites", Json()));
    for (const Json& f : favs["textStyles"]) {
        if (f.value("id", Json()) == id.toStdString()) {
            style = f.at("style");
        }
    }
    if (!style.is_object()) {
        return;
    }
    if (restyle_track([&](const Json&) { return style; })) {
        return;
    }
    core::Timeline next = *session_->timeline();
    for (auto& c : next.clips) {
        if (c->kind == "text" && std::ranges::find(selection_.ids, c->id) != selection_.ids.end()) {
            c.mut().style = style;
        }
    }
    commit(std::move(next), "text-favorite", selection_.ids);
}

void EditorModel::applyTextTheme(const QString& id) {
    const core::TimelineClip* source = selection_.ids.empty() ? nullptr : clip(qstr(selection_.ids.front()));
    const Json* theme = core::theme_by_id(id.toStdString());
    const core::TimelineTrack* t = selected_track_.empty() ? nullptr : track(qstr(selected_track_));
    if (!source && theme && t) { // applyTrackPreset: quitar si la pista ya lo tiene
        const bool clearing = core::selected_subtitle_theme_id(t->style.value_or(Json())) == id.toStdString();
        restyle_track(
            [&](const Json& style) {
                return clearing ? core::clear_text_theme(style) : core::apply_theme_to_style(style, *theme);
            },
            true); // applyTrackPreset parte del estilo de la pista tal cual

        return;
    }
    if (!source || !theme) {
        return;
    }
    const bool clearing = core::selected_subtitle_theme_id(source->style.value_or(Json())) == id.toStdString();
    core::Timeline next = *session_->timeline();
    for (auto& c : next.clips) {
        if (c->kind == "text" && std::ranges::find(selection_.ids, c->id) != selection_.ids.end()) {
            const Json style = c->style.value_or(Json());
            c.mut().style = clearing ? core::clear_text_theme(style) : core::apply_theme_to_style(style, *theme);
        }
    }
    commit(std::move(next), "text-theme", selection_.ids);
}

void EditorModel::applyTextLook(const QString& id) {
    const Json& looks = core::text_looks();
    const auto it = std::ranges::find_if(looks, [&](const Json& l) { return l.at("id") == id.toStdString(); });
    const Json patch = core::look_patch(it == looks.end() ? nullptr : &*it);
    changeStyle(to_variant(patch));
}

void EditorModel::changeText(const QString& text) {
    const core::TimelineClip* c = selection_.ids.empty() ? nullptr : clip(qstr(selection_.ids.front()));
    if (!c || c->kind != "text" || c->text.value_or("") == text.toStdString()) {
        return;
    }
    const std::string id = c->id;
    core::Timeline next = *session_->timeline();
    for (auto& item : next.clips) {
        if (item->id == id) {
            core::change_text(item.mut(), text.toStdString());
        }
    }
    commit(std::move(next), "text", {id});
}

void EditorModel::changeStyle(const QVariantMap& patch, bool fixed) {
    const Json json = to_json(patch);
    if (restyle_track([&](Json style) {
            for (const auto& [key, value] : json.items()) {
                style[key] = value;
            }
            return style;
        })) {
        return;
    }
    if (!session_ || selection_.ids.empty()) {
        return;
    }
    core::Timeline next = *session_->timeline();
    for (auto& c : next.clips) {
        if (c->kind == "text" && std::ranges::find(selection_.ids, c->id) != selection_.ids.end()) {
            core::change_text_style(c.mut(), json, playhead_ - c->start, fps(), fixed, ids_);
        }
    }
    commit(std::move(next), "text-style", selection_.ids);
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

QVariantList EditorModel::waveform(const QString& seed, int bars) {
    QVariantList out;
    for (const double h : core::pseudo_waveform(Json(seed.toStdString()), Json(bars))) {
        out.push_back(h);
    }
    return out;
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
