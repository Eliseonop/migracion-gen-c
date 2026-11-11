#pragma once

// ViewModel del editor (VideoEditor.jsx + EdTimeline.jsx): la timeline de un proyecto en una
// EditSession, con la lógica de arrastre y recorte de React (gc/core/timeline_ui.hpp). Cada cambio
// se guarda en projects.json, como el PUT /timeline de video-yt.

#include <QImage>
#include <QObject>
#include <QRectF>
#include <QString>
#include <QStringList>
#include <QUrl> // moc pide el tipo completo de QList<QUrl> (Qt 6.11; la 6.4 lo dejaba pasar)
#include <QVariantList>
#include <QVariantMap>
#include <map>
#include <memory>
#include <optional>

#include "gc/core/clip_filters.hpp"
#include "gc/core/clip_pose_edit.hpp"
#include "gc/core/history_store.hpp"
#include "gc/core/mask_ui.hpp"
#include "gc/core/projects_store.hpp"
#include "gc/core/session.hpp"
#include "gc/core/settings.hpp"
#include "gc/core/timeline_drop.hpp"
#include "gc/core/timeline_ui.hpp"

#include "backend_jobs.hpp"
#include "narrator.hpp"
#include "beat_detector.hpp"
#include "clip_list_model.hpp"
#include "crop_editor.hpp"
#include "export_job.hpp"
#include "frame_grabber.hpp"
#include "player.hpp"
#include "preview_source.hpp"
#include "proxy_maker.hpp"

namespace gc::studio {

class EditorModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool isOpen READ isOpen NOTIFY projectChanged)
    Q_PROPERTY(QString projectName READ projectName NOTIFY projectChanged)
    Q_PROPERTY(QVariantList tracks READ tracks NOTIFY timelineChanged)
    Q_PROPERTY(QObject* clipModel READ clipModel CONSTANT)
    Q_PROPERTY(QObject* cropEditor READ cropEditor CONSTANT)
    // El fotograma del vídeo del modal Recortar en el cabezal (nulo con una imagen o hasta que llega).
    Q_PROPERTY(QImage cropFrame READ cropFrame NOTIFY cropFrameChanged)
    Q_PROPERTY(bool hasCropFrame READ hasCropFrame NOTIFY cropFrameChanged)
    // La máscara sobre la vista previa (maskMode y maskDraw de VideoEditor.jsx): se enciende con la pestaña
    // Máscara abierta, al añadir una o con «Editar en el preview»; maskDraw es el pincel de una de pincel.
    Q_PROPERTY(bool maskMode READ maskMode WRITE setMaskMode NOTIFY maskModeChanged)
    Q_PROPERTY(bool maskDraw READ maskDraw WRITE setMaskDraw NOTIFY maskModeChanged)
    // La que se edita: la primera activa del clip elegido en el cabezal, normalizada (vacía con maskMode
    // apagado). Durante un arrastre, como va quedando.
    Q_PROPERTY(QVariantMap editMask READ editMask NOTIFY editMaskChanged)
    // El job de subtítulos del backend (useSubtitles): {status, progress, message, error, video}; vacío si no hay.
    Q_PROPERTY(QVariantMap subtitleJob READ subtitleJob NOTIFY subtitleJobChanged)
    // «Seguir cara» en curso (maskFollow): {clipId, progress, message}; vacío si no.
    Q_PROPERTY(QVariantMap maskFollow READ maskFollow NOTIFY maskFollowChanged)
    // «Eliminar fondo»: el job del matte ({clipId, status, progress, message, error, notice}), los motores
    // (BG_PROVIDERS con lo que dice el backend de cada uno) y dónde se ejecutan (cpu, cuda…).
    Q_PROPERTY(QVariantMap bgJob READ bgJob NOTIFY bgJobChanged)
    Q_PROPERTY(QVariantList bgProviders READ bgProviders NOTIFY bgProvidersChanged)
    Q_PROPERTY(QString bgDevice READ bgDevice NOTIFY bgProvidersChanged)
    // El cuentagotas del croma (el siguiente clic en la vista previa toma el color) y el fondo de vista
    // previa ({mode: normal, checker, solid o media; color; kind: image o video}).
    Q_PROPERTY(bool bgPicking READ bgPicking WRITE setBgPicking NOTIFY bgPickingChanged)
    Q_PROPERTY(QVariantMap bgPreview READ bgPreview NOTIFY bgPreviewChanged)
    // «Cargar audio» del material (narrator.hpp): el narrador y el audio de un enlace, por el backend.
    Q_PROPERTY(QObject* narrator READ narrator CONSTANT)
    // El fotograma de la timeline en el cabezal (preview_source.hpp), para FramePreview.
    Q_PROPERTY(QObject* preview READ preview CONSTANT)
    // Reproducir y parar, con el reloj del audio (player.hpp).
    Q_PROPERTY(QObject* player READ player CONSTANT)
    // Los proxies de la vista previa (proxy_maker.hpp): su progreso.
    Q_PROPERTY(QObject* proxies READ proxies CONSTANT)
    // El export (export_job.hpp): su progreso y su resultado.
    Q_PROPERTY(QObject* exporter READ exporter CONSTANT)
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
    // «Detectar beats» (beat_detector.hpp): el clip que se está analizando («» si ninguno).
    Q_PROPERTY(QString beatBusy READ beatBusy NOTIFY beatBusyChanged)
    // El aviso flotante (Toast.jsx): {type: success|error|info, message}; vacío si no hay.
    Q_PROPERTY(QVariantMap notice READ notice NOTIFY noticeChanged)

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
    [[nodiscard]] QImage cropFrame() const { return crop_frames_.frame(); }
    [[nodiscard]] bool hasCropFrame() const { return !crop_frames_.frame().isNull(); }
    [[nodiscard]] bool maskMode() const { return mask_mode_; }
    void setMaskMode(bool on);
    [[nodiscard]] bool maskDraw() const { return mask_draw_; }
    void setMaskDraw(bool on);
    [[nodiscard]] QVariantMap editMask() const;
    [[nodiscard]] QVariantMap subtitleJob() const { return subtitle_job_; }
    // El puente con el backend de video-yt (para los tests: su dirección y cada cuánto se sondea).
    [[nodiscard]] BackendJobs& subtitleBackend() { return subtitle_jobs_; }
    [[nodiscard]] QVariantMap maskFollow() const { return mask_follow_; }
    [[nodiscard]] BackendJobs& faceBackend() { return face_jobs_; }
    [[nodiscard]] QVariantMap bgJob() const { return bg_job_; }
    [[nodiscard]] QVariantList bgProviders() const;
    [[nodiscard]] QString bgDevice() const { return bg_device_; }
    [[nodiscard]] BackendJobs& bgBackend() { return bg_jobs_; }
    [[nodiscard]] bool bgPicking() const { return bg_picking_; }
    void setBgPicking(bool on);
    [[nodiscard]] QVariantMap bgPreview() const { return bg_preview_; }
    // Vuelve a pedir los motores de «Eliminar fondo» (al abrir un proyecto ya se piden).
    void loadBgProviders() { load_bg_providers(); }
    [[nodiscard]] QObject* narrator() { return &narrator_; }
    [[nodiscard]] Narrator& narratorModel() { return narrator_; }
    [[nodiscard]] QObject* preview() { return &preview_; }
    [[nodiscard]] PreviewSource& previewSource() { return preview_; }
    [[nodiscard]] QObject* player() { return &player_; }
    [[nodiscard]] Player& playback() { return player_; }
    [[nodiscard]] QObject* proxies() { return &proxy_maker_; }
    [[nodiscard]] ProxyMaker& proxyMaker() { return proxy_maker_; }
    // La caché de los proxies (la de la app); sin ella, la vista previa lee siempre los originales.
    void setProxyDir(std::filesystem::path dir) { proxy_maker_.setCacheDir(std::move(dir)); }
    [[nodiscard]] QObject* exporter() { return &export_job_; }
    [[nodiscard]] ExportJob& exportJob() { return export_job_; }
    // Exporta la timeline a la carpeta exports del proyecto (ExportJob). false si no se puede empezar.
    Q_INVOKABLE bool exportVideo();
    // Importar material del disco (el botón de la pestaña Proyecto; upload_video, upload_audio y
    // upload_images de main.py, media_import.hpp): copia cada vídeo o audio a la carpeta del
    // proyecto, y cada imagen como PNG de trabajo (un GIF, tal cual), y lo da de alta; el aviso dice
    // cuántos entraron o qué falló. Como importMedia de EdMaterial.jsx, lo que no es vídeo, imagen
    // ni audio (por su tipo MIME o su extensión) no cuenta; si no queda nada, el aviso de error
    // (`dropped`: el de soltar archivos en el panel). Da cuántos entraron.
    Q_INVOKABLE int importFiles(const QList<QUrl>& files, bool dropped = false);
    // dragMediaKind de EdMaterial: lo que se arrastra desde el sistema («video», «image» o «audio»,
    // por ese orden; «other» si no hay nada de eso; «file» sin archivos), para el aviso del panel.
    [[nodiscard]] Q_INVOKABLE QString dropFileKind(const QList<QUrl>& files) const;
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
    // Menú contextual de un clip (ctxMenu de VideoEditor.jsx): qué ofrece y sus acciones propias.
    Q_INVOKABLE QVariantMap clipMenu(const QString& clipId) const;
    Q_INVOKABLE void moveLayer(const QString& clipId, const QString& action);
    Q_INVOKABLE void toggleClipFavorite(const QString& clipId);
    Q_INVOKABLE void applyGlobalTemplate(const QString& clipId);
    Q_INVOKABLE void removeBeats(const QString& clipId);
    // detectClipBeats de VideoEditor.jsx: los beats del audio de un clip de audio o vídeo, en un
    // hilo; al acabar se guardan en el clip ({times, bpm, every}, un paso de deshacer) y el aviso
    // dice cuántos y a qué BPM (o el error). Uno cada vez.
    Q_INVOKABLE void detectBeats(const QString& clipId);
    // La densidad de las marcas: uno de cada 1, 2 o 4 beats.
    Q_INVOKABLE void setBeatsEvery(const QString& clipId, int every);
    [[nodiscard]] QString beatBusy() const { return beat_busy_; }
    [[nodiscard]] QVariantMap notice() const { return notice_; }
    Q_INVOKABLE void clearNotice();
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
    // De un vídeo pide su fotograma en el cabezal (cropFrame) y lo sigue mientras está abierto.
    Q_INVOKABLE QVariantMap openCrop();
    Q_INVOKABLE void confirmCrop();
    // El modal se cerró, confirmado o no: deja de seguir el cabezal y olvida el fotograma.
    Q_INVOKABLE void closeCrop();
    // toggleFlip: voltear en horizontal ("h") o vertical ("v"); todos quedan como el opuesto del principal.
    Q_INVOKABLE void toggleFlip(const QString& axis);
    // Velocidad, tono y reversa (patchClipFx de VideoEditor.jsx; los textos no cambian).
    Q_INVOKABLE void setSpeed(double speed);
    Q_INVOKABLE void toggleKeepPitch();
    Q_INVOKABLE void toggleReverse();
    Q_INVOKABLE void setTransition(const QString& appear, const QString& exit);
    Q_INVOKABLE void setNote(const QString& note);
    // Textos (addText, changeText y changeStyle de VideoEditor.jsx).
    Q_INVOKABLE void addText(const QString& themeId = {}); // con un tema: su estilo y su texto de muestra
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
    // onMaskPanel: la edición en la vista previa vive con la pestaña Máscara abierta; al cerrarla, sin pincel.
    Q_INVOKABLE void maskPanel(bool open);
    // Arrastrar la máscara sobre la vista previa (handleMaskPointer, startMaskDrag y startMaskBrush de
    // interactions.js): el punto en píxeles del item, `frame` el recuadro de salida en él y `scale` la del
    // lienzo de React. maskPress dice si el arrastre es suyo; mientras dura se ve en la vista previa sin
    // guardarse, y al soltar es un solo paso de deshacer.
    Q_INVOKABLE bool maskPress(double x, double y, const QRectF& frame, double scale);
    Q_INVOKABLE void maskMove(double x, double y);
    Q_INVOKABLE void maskRelease();
    // maskCursor: el Qt::CursorShape de ese punto (el de girar, la mano abierta: el «grab» de React).
    Q_INVOKABLE [[nodiscard]] int maskCursor(double x, double y, const QRectF& frame, double scale) const;
    // requestSubtitles de useSubtitles: transcribe el clip en el backend de video-yt (un vídeo con
    // clips/{id}/transcribe, un audio o un SFX con subtitles; el modelo, el de los ajustes) y, al acabar, pone
    // sus subtítulos en la pista de texto en un paso de deshacer. Mientras, subtitleJob dice cómo va.
    Q_INVOKABLE void requestSubtitles(const QString& clipId);
    // Quita el aviso del job (el de error se cierra con un clic).
    Q_INVOKABLE void dismissSubtitleJob();
    // followMaskFace: la máscara del vídeo elegido sigue la cara (el seguimiento de caras del material, que hace
    // el backend con el job face-track, pasado a keyframes de su posición), en un paso de deshacer.
    Q_INVOKABLE void followMaskFace();
    // Eliminación automática del fondo del clip elegido (EdBgRemove y VideoEditor.jsx), en clip.bg_removal y
    // con un paso de deshacer cada una: encenderla o apagarla, un cambio del panel (motor, detalle, umbral…),
    // calcular el matte con el backend (POST /bg-removal; al acabar, su clave de caché va al clip) y cancelarlo.
    Q_INVOKABLE void toggleBgAuto(bool on);
    Q_INVOKABLE void changeBgAuto(const QVariantMap& patch);
    Q_INVOKABLE void applyBgAuto();
    Q_INVOKABLE void cancelBgAuto();
    // El croma y el contorno (propiedades puras del clip, sin job).
    Q_INVOKABLE void toggleBgChroma(bool on);
    Q_INVOKABLE void changeBgChroma(const QVariantMap& patch);
    Q_INVOKABLE void resetBgChroma();
    Q_INVOKABLE void changeBgOutline(const QVariantMap& patch);
    // pickChromaAt: el píxel compuesto bajo (x, y) (en el item, con el recuadro `frame`) es el color del croma.
    Q_INVOKABLE bool pickChromaAt(double x, double y, const QRectF& frame);
    // onBgPanel: al cerrar la pestaña se apaga lo que vive sobre la vista previa.
    Q_INVOKABLE void bgPanel(bool open);
    // onBgPreview: modo y color, o un archivo de prueba (imagen o vídeo).
    Q_INVOKABLE void setBgPreview(const QVariantMap& patch);
    Q_INVOKABLE void setBgPreviewFile(const QUrl& file);
    // copyPrompt de AudioTab: el prompt para pedirle el guion a una IA, con el material del proyecto, al portapapeles.
    Q_INVOKABLE void copyScriptPrompt(const QString& topic);
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
    // Las barras de un clip de audio (pseudoWaveform de EdTimeline: no salen del audio, sino del
    // asset_id): alturas de 0,12 a 1, `bars` de ellas.
    Q_INVOKABLE static QVariantList waveform(const QString& seed, int bars);
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
    void beatBusyChanged();
    void noticeChanged();
    void cropFrameChanged();
    void maskModeChanged();
    void editMaskChanged();
    void subtitleJobChanged();
    void maskFollowChanged();
    void bgJobChanged();
    void bgProvidersChanged();
    void bgPickingChanged();
    void bgPreviewChanged();

private:
    // Una tarjeta del material con su dragPayload; y las del proyecto (sus vídeos, imágenes y audios).
    void add_material(const char* source, const char* asset_kind, const QString& key, const Json& item,
                      const char* kind, QVariantList& into);
    void project_materials(const core::Project& project, QVariantList& into);
    void beats_detected(const QString& clipId, const QVariantList& times, double bpm, const QString& error);
    void set_notice(const QString& type, const QString& message);
    void patch_marker(const QString& id, const std::optional<Json>& patch);
    void set_vfx(Json list);
    template <class Edit>
    void mask_edit(const char* cause, Edit edit);
    void set_filters(const std::vector<core::ClipFilter>& stack);
    void add_generated(core::Timeline next, const Json& clip, const char* cause);
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
    // Un SFX o un audio o vídeo de colección sin duración: la de su archivo (la del <audio
    // preload="metadata"> de EdLibrary), una vez. Sin gc_export, se queda sin ella (y cuenta 1 s).
    void probe_material(const QString& key) const;
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
    // mediaSize de su medio (videoWidth / naturalWidth): el tamaño con el que lo dibuja el render (el
    // primer fotograma, con el autogiro de un vídeo de móvil). Una vez por material (sizes_).
    [[nodiscard]] std::optional<std::pair<double, double>> source_size(const core::TimelineClip& c) const;
    // heightScale de VideoEditor.jsx: alto de la salida / alto de la fuente (1 si no se sabe).
    [[nodiscard]] double height_scale(const core::TimelineClip& c) const;
    // El efecto de VideoEditor.jsx que deja como objeto libre (applyFreeLayout) todo clip visual cuyo
    // medio tiene medidas. Da los ids de los que cambian.
    std::vector<std::string> free_layouts(core::Timeline& tl) const;
    mutable std::map<std::string, std::optional<std::pair<double, double>>> sizes_;
    // El fotograma del vídeo del modal Recortar en el cabezal (del proxy si lo hay).
    void request_crop_frame();
    // La máscara que se edita (editMask) y el arrastre sobre la vista previa: la máscara y el recuadro (en
    // píxeles del lienzo) del principio, el punto de partida, y el último parche de commitMask o el trazo.
    [[nodiscard]] std::optional<Json> edit_mask() const;
    struct MaskDrag {
        std::string clip;
        std::string mode; // move, rotate, feather, width, height, corner; brush, el pincel
        Json mask;
        core::Rect frame;
        double scale = 1;
        core::Point p0;
        Json patch = Json::object();
        Json points = Json::array();
    };
    std::optional<MaskDrag> mask_drag_;
    core::TimelineSnapshot mask_live_; // la timeline con el arrastre a medias: la que ve la vista previa
    void apply_mask_live();
    void mask_drag_edit(core::TimelineClip& c) const;
    bool mask_mode_ = false;
    bool mask_draw_ = false;
    // Subtítulos del backend: el clip fuente tal como era al pedirlos (srcClip) y cómo va el job.
    void subtitle_update(const Json& job, bool video);
    void add_subtitles(const core::TimelineClip& src, const Json& transcript);
    BackendJobs subtitle_jobs_;
    std::optional<core::TimelineClip> subtitle_src_;
    QVariantMap subtitle_job_;
    // «Seguir cara»: el clip, el tamaño de su vídeo y cómo va el job.
    void follow_update(const Json& job);
    BackendJobs face_jobs_;
    std::string follow_clip_;
    std::pair<double, double> follow_size_;
    QVariantMap mask_follow_;
    // «Eliminar fondo»: el bg_removal nuevo de un clip en un paso de deshacer, el final del job y el catálogo
    // de motores (GET /api/bg/providers, una vez por proyecto abierto).
    void set_bg(const std::string& clip_id, Json bg_removal, const char* cause);
    void bg_update(const std::string& clip_id, const Json& job);
    void load_bg_providers();
    BackendJobs bg_jobs_;
    QVariantMap bg_job_;
    Json bg_catalog_ = Json::array();
    QString bg_device_;
    bool bg_picking_ = false;
    void push_bg_preview();
    QVariantMap bg_preview_{{"mode", "normal"}, {"color", "#3B82F6"}, {"kind", ""}};
    QString bg_preview_file_;
    Narrator narrator_;
    // Vuelve a leer el material del proyecto de projects.json y lo pone delante.
    void reload_project_materials();
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
    // material → dragPayload. Mutable: la duración de un SFX o de un audio o vídeo de colección se
    // sondea al arrastrarlo (probe_material), desde los métodos const del arrastre.
    mutable std::map<QString, Json> payloads_;
    mutable std::map<QString, double> probed_; // lo que dio la sonda de cada uno (0: nada)
    ClipListModel clip_model_;
    CropEditor crop_editor_;
    std::string crop_clip_; // el del modal Recortar abierto
    // Lo que se decodifica de su vídeo (el proxy si lo hay), resuelto al abrir: media_path lee projects.json.
    std::filesystem::path crop_media_;
    FrameGrabber crop_frames_; // con su hilo
    core::ClipSelection selection_;
    double playhead_ = 0;
    double pps_ = 40;
    int row_height_ = 52;
    QString error_;
    QString beat_busy_;
    std::string beat_project_; // el proyecto del clip que se analiza
    QVariantMap notice_;
    BeatDetector beat_detector_; // con su hilo, mientras analiza
    ProxyMaker proxy_maker_; // con su hilo
    ExportJob export_job_;   // con su hilo, mientras exporta
    Player player_;          // con su hilo de la mezcla
    PreviewSource preview_; // el último: su hilo se para antes de soltar la sesión
};

} // namespace gc::studio
