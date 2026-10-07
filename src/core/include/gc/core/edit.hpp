#pragma once

// Motor de edición: la ÚNICA puerta para cambiar una timeline.
//
// Cada edición es un COMANDO: un dato (struct) con sus parámetros. La UI, el
// servidor MCP, los scripts y los tests construyen comandos y los pasan por
// apply_command, que hace lo mismo que timeline_store.apply_op de video-yt:
//
//   comando → operación pura → sin solapes (settle) → validación → resultado
//
// Las operaciones son PURAS: reciben `const Timeline&` y devuelven una timeline
// nueva (que comparte con la anterior los clips que no cambian, ver cow.hpp).
// El historial (deshacer/rehacer) y la publicación a otros hilos viven encima,
// en EditSession (session.hpp).

#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

#include "gc/core/error.hpp"
#include "gc/core/ids.hpp"
#include "gc/core/json.hpp"
#include "gc/core/model.hpp"

namespace gc::core {

// Resultado de una edición (EditResult de timeline_ops.py).
struct EditResult {
    Timeline timeline;
    std::vector<std::string> changed;  // ids afectados (clips, pistas, marcadores)
    std::vector<std::string> warnings; // avisos no fatales para la UI / el agente
};

// Parámetros de cada operación: mismos nombres que en video-yt. Un std::optional
// vacío es el `None` de Python (no se cambia / valor por defecto).
namespace ops {

// --- Pistas ---
struct AddTrack {
    std::string kind; // "video" | "audio" | "text"
    std::optional<std::string> name;
    std::optional<std::string> track_id;
};
struct ReorderTrack {
    std::string track_id;
    std::string target_track_id;
    std::string place = "above"; // "above" | "below", tal como se ve en pantalla
};
struct RenameTrack {
    std::string track_id;
    std::string name;
};
struct RemoveTrack {
    std::string track_id;
};
struct LinkTracks {
    std::string track_id;
    std::optional<std::string> to_track_id; // vacío o igual = desligar
};
struct UnlinkTrack {
    std::string track_id;
};

// --- Clips ---
struct AddClip {
    Json clip; // el clip tal cual (se valida dentro, como TimelineClip(**data))
};
struct MoveClip {
    std::string clip_id;
    std::optional<double> start;
    std::optional<std::string> track_id;
};
struct RemoveClip {
    std::string clip_id;
};
struct SplitClip {
    std::string clip_id;
    double at_time = 0.0; // instante ABSOLUTO de la timeline
};
struct DuplicateClip {
    std::string clip_id;
    std::optional<double> start; // por defecto, justo detrás del original
};

// --- Timeline ---
struct SetProjectFormat {
    std::optional<std::string> aspect; // "16:9", "9:16", "1:1", "4:3", "3:4", "4:5"
    std::optional<double> width;
    std::optional<double> height;
    std::optional<double> fps;
    std::optional<std::int64_t> resolution; // lado corto: 480, 720, 1080, 2160
};
struct SetMarkers {
    Json markers; // [{t, label?, color?, id?}] o null
};

// --- Propiedades de clip ---
struct SetClipOpacity {
    std::string clip_id;
    double opacity = 1.0; // 0 = transparente, 1 = opaco
};
struct SetClipSpeed {
    std::string clip_id;
    std::optional<double> speed; // 0.1–10; la fuente no cambia, la barra sí
    std::optional<bool> keep_pitch;
    std::optional<bool> reverse;
};
struct SetClipTransition {
    std::string clip_id;
    std::optional<std::string> appear; // none, fade, dissolve, wipe, zoom, slide_up, slide_left, pop
    std::optional<std::string> exit;   // none, fade, dissolve, wipe, zoom, slide_down, slide_right, pop
};
struct SetTextRole {
    std::string clip_id;
    std::string role; // "caption" | "free"
};
struct SetClipBlend {
    std::string clip_id;
    std::optional<std::string> blend_mode; // vacío = normal
};
struct SetClipFlip {
    std::string clip_id;
    std::optional<bool> flip_h;
    std::optional<bool> flip_v;
};
struct SetClipLayout {
    std::string clip_id;
    std::optional<std::string> position; // full | top | bottom | free
    std::optional<double> start;
    std::optional<double> duration;
};
struct SetClipNote {
    std::string clip_id;
    std::optional<std::string> note; // vacío = borrarla
    Json source = "user";            // "user" | "ai"
};
// Varias propiedades escalares en UNA operación (un paso de deshacer):
// opacity, speed, keep_pitch, reverse, appear, exit, position, start, duration,
// role, note, flip_h, flip_v, blend_mode, disabled.
struct UpdateClip {
    std::string clip_id;
    Json patch;
};

// --- Encuadre ---
struct ReframeClip {
    std::string clip_id;
    std::string mode = "center"; // center | manual | keyframes
    std::optional<double> zoom;  // 0.1–1
    Json pan_from;               // {cx, cy}
    Json pan_to;                 // {cx, cy}: paneo animado de in a out
    Json keyframes;              // [{t, cx, cy, zoom?}] en tiempo de FUENTE
};
struct CropClip {
    Json clip_ids; // lista de ids
    std::optional<std::string> track_id;
    bool reset = false;
    Json cx, cy, w, h; // ventana sobre la fuente (0–1)
};

// --- Animación, ritmo y efectos ---
struct SetClipKeyframes {
    std::string clip_id;
    Json keyframes; // {enabled, items} o null
};
struct SetClipBeats {
    std::string clip_id;
    Json beats; // {times, bpm, every: 1|2|4} o null
};
struct SetClipMasks {
    std::string clip_id;
    Json masks; // lista o null
};
struct SetClipEffects {
    std::string clip_id;
    Json effects; // dict (se fusiona salvo replace)
    bool replace = false;
    Json filters; // [{id, amount 0–1}]
};

// --- Audio ---
struct SetClipVolume {
    std::string clip_id;
    Json volume; // 0–2
    Json muted;
    Json fade;           // "in" | "out"
    Json fade_dur = 0.5; // s
};
struct SetTrackAudio {
    std::string track_id;
    Json volume;
    Json muted;
    Json audio_fx;
    bool replace_fx = false;
    Json fade;
    Json fade_dur = 0.5;
};
struct SetClipAudioFx {
    std::string clip_id;
    Json audio_fx; // {eq, compressor, reverb…: 0–1}
    bool replace = false;
    Json ramp; // {start, end, from?}: anima los efectos en ese tramo
};

// --- Material ---
struct ReplaceMedia {
    Json sources; // [{filename, delta}]
    Json target;  // {asset_id, filename, duration, media_version?}
};

// --- Efectos y animaciones del catálogo (fx_catalog.hpp) ---
struct ApplyEffect {
    Json clip_ids;
    Json effect;  // id del efecto a añadir
    Json params;  // parámetros (se sanean contra el catálogo)
    Json start;   // s locales
    Json end;
    bool remove = false; // quitar `uid` o todos los `effect`
    Json uid;            // efecto de la pila a cambiar
    Json stack;          // sustituye la pila entera
};
struct SetClipAnimation {
    Json clip_ids;
    Json slot;      // in | out | loop | combo
    Json animation; // id; vacío = quitarla
    Json duration;  // s (en loop, el periodo)
};

// --- Material nuevo ---
struct FreezeFrame {
    std::string clip_id;
    Json at_time;        // s de timeline
    Json image;          // {id, filename, label?}: el fotograma ya extraído
    Json duration = 3.0; // s que dura la imagen congelada
};
struct AddSubtitles {
    std::string source_clip_id;
    Json segments; // de la transcripción (con words[])
    Json style;    // por defecto, el de la pista
    std::optional<std::string> track_id;
    Json transcript_id;
};
struct AddShape {
    Json shape; // {type, fill, stroke…}
    std::optional<std::string> track_id;
    Json start = 0.0;
    Json duration;
    Json points; // [[x, y], …] en 0–1: trazado libre
};
struct AddCinemaBars {
    Json ratio = "2.39"; // "2.39" | "2" | "1.85" | "16:9" | número
    Json start = 0.0;
    Json duration; // por defecto, hasta el final de la timeline
    bool animate = false;
};
struct AddAdjustmentLayer {
    Json start = 0.0;
    Json duration = 5.0;
    Json filters;
    Json effects;
    Json intensity = 1.0;
    std::optional<std::string> track_id;
};
struct SetClipBgRemoval {
    std::string clip_id;
    Json bg_removal; // se fusiona por secciones (auto/chroma/outline); null = quitar
    bool replace = false;
};

// --- Compuestos ---
// Pega los grupos de atributos `groups` (clip_attrs.hpp; null = todos) de un clip en otros.
struct PasteClipAttributes {
    std::string source_clip_id;
    Json target_clip_ids; // lista de ids
    Json groups;          // lista de grupos o null
};
// El clip `follower_clip_id` acompaña al objeto seguido en el vídeo `video_clip_id`
// (follow.hpp): keyframes de pose lineales, sustituyendo los que había en el tramo.
struct FollowObject {
    std::string follower_clip_id;
    std::string video_clip_id;
    Json track;    // [{t, cx, cy, s?, rot?}] (s de la fuente)
    Json box_w;    // ancho del recuadro inicial, 0–1 de la fuente
    Json anchor_t; // s de timeline en que se marcó
    Json dims;     // {srcW, srcH, outW, outH} en px
    Json mode = "position_scale";
    Json min_gap = 0.1;
};
// Coloca los sonidos que propone «Sonorizar con IA» en pistas «SFX» libres (crea las
// que falten); el ambiente entra y sale con fundido.
struct AddSoundDesign {
    std::string clip_id; // la escena: los tiempos de `sounds` son locales a ella
    Json sounds;         // [{sfx: {id, name?, duration?}, start, duration, volume, kind, what}]
};
// Animación generada con un preset (clip_motion.hpp): zoom, giro, deslizar, fundido,
// pop, pulso o trazo. En los clips de relleno, el zoom también va al reframe.
struct AnimateClip {
    std::string clip_id;
    Json motion;          // nombre del preset o alias
    Json duration;        // s del movimiento; null = el del preset
    Json intensity = 1.0; // 0,15–2
    Json turns = 1.0;     // vueltas del giro, 0,25–4
    Json envelope;        // [{t, amp}] o [[t, amp]]: sigue un sonido
};
// Receta en un clic: cinema_grade, text_reflection, pass_through_text, film_strips o
// subject_pop (ver edit_ops_recipes.cpp). Una sola operación: se deshace de golpe.
struct ApplyRecipe {
    Json recipe;
    Json clip_ids; // los clips seleccionados (según la receta: texto, vídeo o 2–6 vídeos/imágenes)
    Json params;   // {zoom_time} en pass_through_text, {frames} en subject_pop
};

} // namespace ops

using Command =
    std::variant<ops::AddTrack, ops::ReorderTrack, ops::RenameTrack, ops::RemoveTrack, ops::LinkTracks,
                 ops::UnlinkTrack, ops::AddClip, ops::MoveClip, ops::RemoveClip, ops::SplitClip, ops::DuplicateClip,
                 ops::SetProjectFormat, ops::SetMarkers, ops::SetClipOpacity, ops::SetClipSpeed,
                 ops::SetClipTransition, ops::SetTextRole, ops::SetClipBlend, ops::SetClipFlip, ops::SetClipLayout,
                 ops::SetClipNote, ops::UpdateClip, ops::ReframeClip, ops::CropClip, ops::SetClipKeyframes,
                 ops::SetClipBeats, ops::SetClipMasks, ops::SetClipEffects, ops::SetClipVolume, ops::SetTrackAudio,
                 ops::SetClipAudioFx, ops::ReplaceMedia, ops::ApplyEffect, ops::SetClipAnimation, ops::FreezeFrame,
                 ops::AddSubtitles, ops::AddShape, ops::AddCinemaBars, ops::AddAdjustmentLayer,
                 ops::SetClipBgRemoval, ops::PasteClipAttributes,
                 ops::FollowObject, ops::AddSoundDesign, ops::AnimateClip,
                 ops::ApplyRecipe>;

// Nombre de la operación ("move_clip"…), el mismo del MCP y de video-yt.
[[nodiscard]] std::string_view command_name(const Command& command);

// Nombres de todas las operaciones disponibles.
[[nodiscard]] std::vector<std::string_view> command_names();

// Comando desde (nombre, parámetros JSON), con las reglas de los kwargs de Python:
// parámetro desconocido u obligatorio ausente = error.
[[nodiscard]] Result<Command> parse_command(std::string_view op, const Json& params);

// Solo la operación (sin settle ni validación). No muta `timeline`.
[[nodiscard]] Result<EditResult> run_command(const Timeline& timeline, const Command& command, IdSource& ids);

// Pipeline completo de timeline_store.apply_op: operación → sin solapes →
// validación (rechaza solo errores NUEVOS, no la suciedad previa del proyecto).
[[nodiscard]] Result<EditResult> apply_command(const Timeline& timeline, const Command& command, IdSource& ids);

} // namespace gc::core
