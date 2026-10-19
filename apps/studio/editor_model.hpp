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

#include "gc/core/clip_filters.hpp"
#include "gc/core/clip_pose_edit.hpp"
#include "gc/core/history_store.hpp"
#include "gc/core/projects_store.hpp"
#include "gc/core/session.hpp"
#include "gc/core/settings.hpp"
#include "gc/core/timeline_drop.hpp"
#include "gc/core/timeline_ui.hpp"

#include "clip_list_model.hpp"
#include "crop_editor.hpp"

namespace gc::studio {

class EditorModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool isOpen READ isOpen NOTIFY projectChanged)
    Q_PROPERTY(QString projectName READ projectName NOTIFY projectChanged)
    Q_PROPERTY(QVariantList tracks READ tracks NOTIFY timelineChanged)
    Q_PROPERTY(QObject* clipModel READ clipModel CONSTANT)
    Q_PROPERTY(QObject* cropEditor READ cropEditor CONSTANT)
    Q_PROPERTY(double duration READ duration NOTIFY timelineChanged)
    Q_PROPERTY(int fps READ fps NOTIFY timelineChanged)
    Q_PROPERTY(double aspect READ aspect NOTIFY timelineChanged)
    Q_PROPERTY(QVariantList markers READ markers NOTIFY timelineChanged)
    Q_PROPERTY(QVariantList materials READ materials NOTIFY projectChanged)
    Q_PROPERTY(QVariantList textThemes READ textThemes CONSTANT)
    Q_PROPERTY(QVariantList textLooks READ textLooks CONSTANT)
    Q_PROPERTY(QVariantList colorFilters READ colorFilters CONSTANT)
    Q_PROPERTY(QVariantList animationSlots READ animationSlots CONSTANT)
    Q_PROPERTY(QVariantList vfxCatalog READ vfxCatalog CONSTANT)
    Q_PROPERTY(QVariantList vfxCategories READ vfxCategories CONSTANT)
    Q_PROPERTY(QVariantList sfxCategories READ sfxCategories NOTIFY projectChanged)
    Q_PROPERTY(QVariantList collections READ collections NOTIFY projectChanged)
    Q_PROPERTY(QString collectionsRoot READ collectionsRoot NOTIFY projectChanged)
    Q_PROPERTY(QVariantList textFavorites READ textFavorites NOTIFY favoritesChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY timelineChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY timelineChanged)
    Q_PROPERTY(QStringList selection READ selection NOTIFY selectionChanged)
    Q_PROPERTY(QVariantMap selectedClip READ selectedClip NOTIFY selectionChanged)
    // La pista elegida en su cabecera (sin clip seleccionado): {id, kind, name, style}.
    Q_PROPERTY(QVariantMap selectedTrack READ selectedTrack NOTIFY selectionChanged)
    Q_PROPERTY(double playhead READ playhead WRITE setPlayhead NOTIFY playheadChanged)
    Q_PROPERTY(double pps READ pps WRITE setPps NOTIFY zoomChanged)
    Q_PROPERTY(int rowHeight READ rowHeight WRITE setRowHeight NOTIFY timelineChanged)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

public:
    // `settings` (opcional) da la carpeta de los SFX; la biblioteca está junto a projects.json.
    explicit EditorModel(core::ProjectsStore& store, core::SettingsFile* settings = nullptr,
                         QObject* parent = nullptr);
    ~EditorModel() override;

    [[nodiscard]] bool isOpen() const { return session_ != nullptr; }
    [[nodiscard]] QString projectName() const { return project_name_; }
    [[nodiscard]] QVariantList tracks() const { return tracks_; }
    [[nodiscard]] QObject* clipModel() { return &clip_model_; }
    [[nodiscard]] QObject* cropEditor() { return &crop_editor_; }
    // Las filas de clipModel (para los tests).
    [[nodiscard]] QVariantList clips() const;
    [[nodiscard]] double duration() const;
    [[nodiscard]] QVariantList markers() const;
    // Material del proyecto (vídeos, imágenes y audios): [{key, assetKind, kind, name, duration}].
    [[nodiscard]] QVariantList materials() const { return materials_; }
    // Temas de subtítulos y estilos «Aa» con lo que pinta su muestra (color, trazo, fondo, brillo).
    [[nodiscard]] static QVariantList textThemes();
    [[nodiscard]] static QVariantList textLooks();
    // FILTERS de clipFilters.js con su muestra (piel, cielo, vegetación y gris filtrados).
    [[nodiscard]] static QVariantList colorFilters();
    // Ranuras de las animaciones de clip del catálogo: [{id, label, items: [{id, name, dur}]}].
    [[nodiscard]] static QVariantList animationSlots();
    // Efectos animados del catálogo: [{id, name, category, description}].
    [[nodiscard]] static QVariantList vfxCatalog();
    [[nodiscard]] static QVariantList vfxCategories();
    // La miniatura de una figura: anillos y líneas en 0–100 y sus colores por defecto.
    Q_INVOKABLE static QVariantMap shapeThumb(const QString& type);
    // Categorías de la carpeta de SFX ({id, name, count}; «Favoritos» delante).
    [[nodiscard]] QVariantList sfxCategories() const { return sfx_categories_; }
    // search de sfx_library: los sonidos que tienen todas las palabras de `q`, en `category`.
    Q_INVOKABLE void searchSfx(const QString& q, const QString& category);
    // Modal «Nuevo SFX» / «Editar SFX» (SfxClassifyModal.jsx): filas de borrador, crear una
    // categoría y guardar una fila (devuelve el error o nada).
    Q_INVOKABLE QVariantList sfxDrafts(const QList<QUrl>& files, const QString& defaultCategory) const;
    Q_INVOKABLE QVariantMap sfxDraft(const QString& key) const;
    Q_INVOKABLE QVariantMap createSfxCategory(const QString& label);
    Q_INVOKABLE QString saveSfx(const QVariantMap& row);
    // Colecciones de material (carpetas de `material_root` o assets/material): {id, name, count}.
    [[nodiscard]] QVariantList collections() const { return collections_; }
    [[nodiscard]] QString collectionsRoot() const { return collections_root_; }
    // search de collections: los medios de las colecciones activas (o de `collection`) que casan con `q`.
    Q_INVOKABLE void searchCollections(const QString& q, const QString& collection);
    // Gestionar las carpetas de material (EdLibrary.jsx): preferencias, raíz, crear y subir archivos.
    Q_INVOKABLE void setCollectionPrefs(const QString& id, const QVariantMap& patch);
    Q_INVOKABLE bool setCollectionsRoot(const QString& path); // ruta o file:// del diálogo
    Q_INVOKABLE QString createCollection(const QString& name);
    Q_INVOKABLE int addCollectionFiles(const QString& collection, const QList<QUrl>& files);
    [[nodiscard]] int fps() const;
    [[nodiscard]] double aspect() const;
    [[nodiscard]] bool canUndo() const { return session_ && session_->can_undo(); }
    [[nodiscard]] bool canRedo() const { return session_ && session_->can_redo(); }
    [[nodiscard]] QStringList selection() const;
    [[nodiscard]] QVariantMap selectedClip() const;
    [[nodiscard]] QVariantMap selectedTrack() const;
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
    // Clic en la cabecera: la pista (y nada de clips); en una de texto, el inspector edita su estilo general.
    Q_INVOKABLE void selectTrack(const QString& id);
    // Selección por área (startMarquee de EdTimeline): los clips que corta el rectángulo, en px de
    // las filas; con `additive` se suman a los que había.
    Q_INVOKABLE void selectInRect(double x0, double y0, double x1, double y1, bool additive);
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
    // patchMarker de VideoEditor.jsx: mover (al fotograma), renombrar (vacío = sin nombre) o quitar.
    Q_INVOKABLE void moveMarker(const QString& id, double t);
    Q_INVOKABLE void renameMarker(const QString& id, const QString& label);
    Q_INVOKABLE void removeMarker(const QString& id);
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
    // Panel de Transformación (EdTransform.jsx): X/Y como CapCut, escala en %, giro y restablecer.
    Q_INVOKABLE void setPosition(double x, double y);
    Q_INVOKABLE void setScalePct(double pct);
    Q_INVOKABLE void setRotation(double deg);
    Q_INVOKABLE void resetTransform();
    // Modal Recortar: carga cropEditor con el clip seleccionado (vacío si no se puede recortar) y lo aplica.
    Q_INVOKABLE QVariantMap openCrop();
    Q_INVOKABLE void confirmCrop();
    // toggleFlip: voltear en horizontal ("h") o vertical ("v"); todos quedan como el opuesto del principal.
    Q_INVOKABLE void toggleFlip(const QString& axis);
    // Velocidad, tono y reversa (patchClipFx de VideoEditor.jsx; los textos no cambian).
    Q_INVOKABLE void setSpeed(double speed);
    Q_INVOKABLE void toggleKeepPitch();
    Q_INVOKABLE void toggleReverse();
    Q_INVOKABLE void setTransition(const QString& appear, const QString& exit);
    Q_INVOKABLE void setNote(const QString& note);
    // Textos (addText, changeText y changeStyle de VideoEditor.jsx).
    Q_INVOKABLE void addText();
    // Figura del catálogo en el cabezal (addAsset('shape')) y capa de ajuste (addAdjustmentLayer).
    Q_INVOKABLE void addShape(const QString& type, const QString& label);
    Q_INVOKABLE void addAdjustment();
    // Filtros de color (EdFilters.jsx): clic añade o quita; intensidad y orden en la pila.
    Q_INVOKABLE void toggleFilter(const QString& id);
    // patchEffects: un valor de `effects` (Brillo, Contraste, Exposición…) sobre los del clip principal.
    Q_INVOKABLE void setEffect(const QString& key, const QVariant& value);
    // Efecto o filtro de sonido (setFx de EdEffects.jsx): 0–1; con keyframes, en el del cabezal.
    Q_INVOKABLE void setAudioFx(const QString& id, double value);
    // Máscara (EdMask.jsx): la primera del clip.
    Q_INVOKABLE void addMask(const QString& type);
    Q_INVOKABLE void removeMask();
    Q_INVOKABLE void duplicateMask();
    Q_INVOKABLE void changeMask(const QVariantMap& patch);
    Q_INVOKABLE void commitMask(const QVariantMap& patch);
    // Pila de efectos animados (addVfx y EdVfxStack): añadir, parámetros, activar, rango, orden y quitar.
    // listEffects de fx/registry.js para la biblioteca: categoría y palabras (sin tildes ni mayúsculas).
    Q_INVOKABLE QVariantList searchVfx(const QString& q, const QString& category) const;
    Q_INVOKABLE void addVfx(const QString& effectId);
    Q_INVOKABLE void setVfxParam(int index, const QString& param, const QVariant& value);
    Q_INVOKABLE void patchVfx(int index, const QVariantMap& patch);
    Q_INVOKABLE void moveVfx(int index, int step);
    Q_INVOKABLE void removeVfx(int index);
    // setClipAnimation: la animación `id` del catálogo en la ranura (in, out, loop o combo); vacío la quita.
    Q_INVOKABLE void setAnimation(const QString& slot, const QString& id);
    // La duración de la animación de la ranura (0,1–4 s; en bucle, cada cuánto: hasta 8 s).
    Q_INVOKABLE void setAnimationDur(const QString& slot, double dur);
    // Intensidad de una capa de ajuste (su opacidad).
    Q_INVOKABLE void setAdjustIntensity(double value);
    Q_INVOKABLE void setFilterAmount(int index, double amount);
    Q_INVOKABLE void moveFilter(int index, int step);
    // Copiar y pegar atributos (Ctrl+Alt+C / Ctrl+Alt+V): se copia el clip y al pegar se eligen los
    // grupos (posición, animación, audio…) que van a los seleccionados.
    Q_INVOKABLE bool copyAttributes();
    // pasteableGroups: [{id, label, count, total}] para la selección actual.
    Q_INVOKABLE QVariantList pasteableGroups() const;
    Q_INVOKABLE void pasteAttributes(const QStringList& groups);
    // Inspector de figuras (EdShape.jsx).
    Q_INVOKABLE void changeShape(const QVariantMap& patch);
    Q_INVOKABLE void changeText(const QString& text);
    Q_INVOKABLE void changeStyle(const QVariantMap& patch, bool fixed = false);
    // applyFragment: parte el texto seleccionado (o los subtítulos de la pista elegida) en cuadros de
    // `max_words` palabras, con sus tiempos.
    Q_INVOKABLE void fragmentText();
    // Rombo de una sección del estilo del texto (TEXT_KF_SECTIONS: color, stroke, bg, glow, curve,
    // stretch, shadow): keyframe en el cabezal con lo que se ve, o lo quita.
    Q_INVOKABLE void toggleStyleKeyframe(const QString& section);
    // «Mis estilos» (favoritos de los ajustes): {id, name}; guardar el del texto o la pista elegidos,
    // aplicarlo (el estilo entero) y quitarlo.
    [[nodiscard]] QVariantList textFavorites() const;
    Q_INVOKABLE void saveTextFavorite();
    Q_INVOKABLE void applyTextFavorite(const QString& id);
    Q_INVOKABLE void deleteTextFavorite(const QString& id);
    // applyPreset: un clic aplica el tema a los textos seleccionados; otro en el mismo lo quita.
    Q_INVOKABLE void applyTextTheme(const QString& id);
    // Estilo preestablecido «Aa» (vacío = «Ninguno»).
    Q_INVOKABLE void applyTextLook(const QString& id);
    // El rombo (toggleKeyframeAtPlayhead) del clip seleccionado; fundido de volumen de entrada o
    // salida y silenciar, en la selección.
    Q_INVOKABLE void toggleKeyframe();
    // Alt+C / Alt+V / Alt+D: copiar el keyframe del cabezal, pegarlo en el cabezal y duplicarlo
    // (al cabezal si está libre; si no, un segundo después).
    // Curva de velocidad (KfCurvePanel e interpAnimKf): la del keyframe al que se llega desde el cabezal.
    // `bezier` [x1, y1, x2, y2] de los tiradores; vacío = el de la curva actual.
    Q_INVOKABLE void setCurve(const QString& mode, const QVariantList& bezier = {});
    // curvePoints de EdCurve.jsx: la curva muestreada (x, y) para dibujarla.
    Q_INVOKABLE static QVariantList curvePoints(const QString& kind, const QVariantList& bezier, int steps = 48);
    Q_INVOKABLE void moveKeyframe(const QString& clipId, const QString& kfId, double fraction);
    Q_INVOKABLE bool copyKeyframe();
    Q_INVOKABLE bool pasteKeyframe();
    Q_INVOKABLE bool duplicateKeyframe();
    Q_INVOKABLE void fadeVolume(const QString& side);
    Q_INVOKABLE void toggleMuted();
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
    // Alt pulsado (soltar material encima de un clip lo reemplaza).
    Q_INVOKABLE static bool altHeld();

signals:
    void projectChanged();
    void timelineChanged();
    void selectionChanged();
    void favoritesChanged();
    void playheadChanged();
    void zoomChanged();
    void errorChanged();

private:
    void patch_marker(const QString& id, const std::optional<Json>& patch);
    void set_vfx(Json list);
    template <class Edit>
    void mask_edit(const char* cause, Edit edit);
    void set_filters(const std::vector<core::ClipFilter>& stack);
    void add_generated(core::Timeline next, Json clip, const char* cause);
    template <class Patch>
    void pose_each(const char* cause, Patch patch);
    template <class Edit>
    void fx_each(const char* cause, Edit edit);
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
    std::unique_ptr<core::Autosave> autosave_; // después de session_: se destruye antes
    mutable core::RandomIdSource ids_;
    std::string project_id_;
    QString project_name_;
    QVariantList tracks_;
    QVariantList materials_;
    std::vector<std::pair<core::TimelineClip, double>> clipboard_; // clip y desfase con el primero
    std::optional<core::TimelineClip> attr_clipboard_;
    std::optional<core::KeyframeBoard> kf_board_;
    std::string selected_track_;
    QVariantList sfx_categories_;
    QVariantList collections_;
    QString collections_root_;
    std::pair<std::string, std::string> collections_query_; // q y colección de la última búsqueda
    [[nodiscard]] std::optional<std::filesystem::path> collections_base() const;
    [[nodiscard]] std::optional<std::filesystem::path> media_path(const core::TimelineClip& c) const;
    void reload_collections();
    void load_collections(const std::string& q, const std::string& collection);
    void load_sfx(const std::string& q, const std::string& category);
    [[nodiscard]] std::optional<std::filesystem::path> sfx_dir() const;
    [[nodiscard]] Json real_sfx_categories() const;
    std::pair<std::string, std::string> sfx_query_; // q y categoría de la última búsqueda
    std::map<QString, Json> sfx_items_;              // material → el sonido como lo da la búsqueda
    core::SettingsFile* settings_ = nullptr;
    // changeTrackStyle / applyTrackPreset: el estilo de la pista y el de todos sus textos.
    template <class Restyle>
    bool restyle_track(Restyle restyle, bool theme = false);
    std::map<QString, Json> payloads_; // material → dragPayload
    ClipListModel clip_model_;
    CropEditor crop_editor_;
    std::string crop_clip_;
    core::ClipSelection selection_;
    double playhead_ = 0;
    double pps_ = 40;
    int row_height_ = 52;
    QString error_;
};

} // namespace gc::studio
