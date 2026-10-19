#pragma once

// ViewModel del editor (VideoEditor.jsx + EdTimeline.jsx): la timeline de un proyecto en una
// EditSession, con la lógica de arrastre y recorte de React (gc/core/timeline_ui.hpp). Cada cambio
// se guarda en projects.json, como el PUT /timeline de video-yt.

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>
#include <map>
#include <memory>
#include <optional>

#include "gc/core/projects_store.hpp"
#include "gc/core/session.hpp"
#include "gc/core/timeline_drop.hpp"
#include "gc/core/timeline_ui.hpp"

#include "clip_list_model.hpp"

namespace gc::studio {

class EditorModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool isOpen READ isOpen NOTIFY projectChanged)
    Q_PROPERTY(QString projectName READ projectName NOTIFY projectChanged)
    Q_PROPERTY(QVariantList tracks READ tracks NOTIFY timelineChanged)
    Q_PROPERTY(QObject* clipModel READ clipModel CONSTANT)
    Q_PROPERTY(double duration READ duration NOTIFY timelineChanged)
    Q_PROPERTY(int fps READ fps NOTIFY timelineChanged)
    Q_PROPERTY(double aspect READ aspect NOTIFY timelineChanged)
    Q_PROPERTY(QVariantList markers READ markers NOTIFY timelineChanged)
    Q_PROPERTY(QVariantList materials READ materials NOTIFY projectChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY timelineChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY timelineChanged)
    Q_PROPERTY(QStringList selection READ selection NOTIFY selectionChanged)
    Q_PROPERTY(QVariantMap selectedClip READ selectedClip NOTIFY selectionChanged)
    Q_PROPERTY(double playhead READ playhead WRITE setPlayhead NOTIFY playheadChanged)
    Q_PROPERTY(double pps READ pps WRITE setPps NOTIFY zoomChanged)
    Q_PROPERTY(int rowHeight READ rowHeight WRITE setRowHeight NOTIFY timelineChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

public:
    explicit EditorModel(core::ProjectsStore& store, QObject* parent = nullptr);
    ~EditorModel() override;

    [[nodiscard]] bool isOpen() const { return session_ != nullptr; }
    [[nodiscard]] QString projectName() const { return project_name_; }
    [[nodiscard]] QVariantList tracks() const { return tracks_; }
    [[nodiscard]] QObject* clipModel() { return &clip_model_; }
    // Las filas de clipModel (para los tests).
    [[nodiscard]] QVariantList clips() const;
    [[nodiscard]] double duration() const;
    [[nodiscard]] QVariantList markers() const;
    // Material del proyecto (vídeos, imágenes y audios): [{key, assetKind, kind, name, duration}].
    [[nodiscard]] QVariantList materials() const { return materials_; }
    [[nodiscard]] int fps() const;
    [[nodiscard]] double aspect() const;
    [[nodiscard]] bool canUndo() const { return session_ && session_->can_undo(); }
    [[nodiscard]] bool canRedo() const { return session_ && session_->can_redo(); }
    [[nodiscard]] QStringList selection() const;
    [[nodiscard]] QVariantMap selectedClip() const;
    [[nodiscard]] double playhead() const { return playhead_; }
    [[nodiscard]] double pps() const { return pps_; }
    [[nodiscard]] int rowHeight() const { return row_height_; }
    [[nodiscard]] QString error() const { return error_; }

    void setPlayhead(double t);
    void setPps(double pps);
    void setRowHeight(int height);

    Q_INVOKABLE bool openProject(const QString& pid);
    Q_INVOKABLE void closeProject();

    Q_INVOKABLE void select(const QString& id, bool additive = false, bool range = false);
    Q_INVOKABLE void clearSelection();
    // Dónde caería el clip (o la selección, si es un grupo) arrastrado `delta` s con el centro del
    // fantasma a `centerY` px de las filas: {group, delta | start, trackId, newTrack, lineY, times}.
    // `times` son las guías del imán. No cambia nada.
    Q_INVOKABLE QVariantMap previewDrag(const QString& id, double delta, double centerY) const;
    Q_INVOKABLE void dropDrag(const QString& id, double delta, double centerY);
    // Inicio y pista exactos (sin imán ni pista nueva): para scripts y tests.
    // Material arrastrado desde el panel con el puntero en (x, y) px de las filas; `alt` reemplaza el
    // clip de debajo: {start, trackId, newTrack, lineY, times, duration}. No cambia nada.
    Q_INVOKABLE QVariantMap previewMaterialDrop(const QString& key, double x, double y, bool alt) const;
    // Lo suelta (dropAsset de VideoEditor.jsx) y selecciona el clip nuevo.
    Q_INVOKABLE void dropMaterial(const QString& key, double x, double y, bool alt);
    Q_INVOKABLE void moveClip(const QString& id, double start, const QString& trackId);
    // {start, duration, times} del clip con el borde movido `delta` s. No cambia nada.
    Q_INVOKABLE QVariantMap previewTrim(const QString& id, bool left, double delta) const;
    Q_INVOKABLE void trimClip(const QString& id, bool left, double delta);
    Q_INVOKABLE void splitAtPlayhead();
    Q_INVOKABLE void deleteSelection();
    Q_INVOKABLE void duplicateSelection();
    // Atajos de VideoEditor.jsx: V (desactivar la selección), M (marcador en el cabezal), «,» y «.»
    // (anterior y siguiente marcador o beat) y las flechas (0,1 s; con Shift, 1 s).
    Q_INVOKABLE void toggleDisabled();
    Q_INVOKABLE void toggleMarker();
    Q_INVOKABLE void jumpSnap(int dir);
    Q_INVOKABLE void nudgePlayhead(double dt);
    // Cabecera de pista (EdTimeline.jsx): ver, silenciar, bloquear o magnética; nombre (3 letras
    // como mucho, TRACK_NAME_MAX); juntar sus clips desde 0; y cambiar el orden en pantalla.
    Q_INVOKABLE void toggleTrack(const QString& id, const QString& prop);
    Q_INVOKABLE void renameTrack(const QString& id, const QString& name);
    Q_INVOKABLE void compactTrack(const QString& id);
    Q_INVOKABLE void reorderTrack(const QString& id, const QString& targetId, bool below);
    // Inspector: commitPose a la selección (opacidad, volumen…) en el instante del cabezal; modo
    // de fusión (no el audio; «normal» lo quita); aparición y salida; nota de contexto.
    Q_INVOKABLE void setPose(const QVariantMap& patch);
    Q_INVOKABLE void setBlend(const QString& mode);
    Q_INVOKABLE void setTransition(const QString& appear, const QString& exit);
    Q_INVOKABLE void setNote(const QString& note);
    // Ctrl+C / Ctrl+X / Ctrl+V (copySelectedClips y pasteClips de VideoEditor.jsx): se pega en el
    // cabezal conservando la separación; en su pista si sigue, si no en una compatible.
    Q_INVOKABLE bool copySelection();
    Q_INVOKABLE void cutSelection();
    Q_INVOKABLE bool paste();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();

    Q_INVOKABLE void zoomBy(double factor, double viewWidth);
    Q_INVOKABLE void zoomToFit(double viewWidth);
    // Marcas de la regla visibles: [{t, x, major, label}].
    Q_INVOKABLE QVariantList ticks(double scrollX, double viewWidth) const;
    Q_INVOKABLE QString formatTime(double t) const;

signals:
    void projectChanged();
    void timelineChanged();
    void selectionChanged();
    void playheadChanged();
    void zoomChanged();
    void errorChanged();

private:
    void rebuild();
    void setError(const QString& message);
    template <class T>
    bool ok(const Result<T>& r);
    void apply(const core::Command& command);
    // Como setClips + settleClips de VideoEditor.jsx: resuelve los solapes nuevos, quita las pistas
    // de `emptied` que se quedan vacías y lo deja todo en un paso de deshacer.
    void commit(core::Timeline next, std::string cause, std::vector<std::string> changed,
                std::vector<std::string> emptied = {});
    struct DragPlan;
    [[nodiscard]] std::optional<DragPlan> planDrag(const QString& id, double delta, double centerY) const;
    [[nodiscard]] std::optional<core::ClipPatch> planTrim(const QString& id, bool left, double delta,
                                                          std::vector<double>* times = nullptr) const;
    [[nodiscard]] std::optional<std::pair<Json, core::Drop>> planMaterialDrop(const QString& key, double x,
                                                                              double y, bool alt) const;
    [[nodiscard]] std::vector<core::AlignClip> snapOthers(std::span<const std::string> moving,
                                                          bool beats_too) const;
    [[nodiscard]] const core::TimelineClip* clip(const QString& id) const;
    [[nodiscard]] const core::TimelineTrack* track(const QString& id) const;
    [[nodiscard]] bool canDrop(const core::TimelineClip& c, const core::TimelineTrack& t) const;

    core::ProjectsStore& store_;
    std::unique_ptr<core::EditSession> session_;
    mutable core::RandomIdSource ids_;
    std::string project_id_;
    QString project_name_;
    QVariantList tracks_;
    QVariantList materials_;
    std::vector<std::pair<core::TimelineClip, double>> clipboard_; // clip y desfase con el primero
    std::map<QString, Json> payloads_; // material → dragPayload
    ClipListModel clip_model_;
    core::ClipSelection selection_;
    double playhead_ = 0;
    double pps_ = 40;
    int row_height_ = 52;
    QString error_;
};

} // namespace gc::studio
