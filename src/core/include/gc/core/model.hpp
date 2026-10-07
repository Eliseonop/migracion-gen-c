#pragma once

// Modelo de datos del proyecto: espejo de backend/app/schemas.py (video-yt).
//
// Mismos nombres de campo, mismo orden y mismos valores por defecto que los
// modelos Pydantic, porque el formato en disco (data/projects.json) es el mismo:
// GenClipPro abre los proyectos de video-yt sin convertirlos.
//
// - Campo obligatorio en Pydantic (sin default) → miembro sin std::optional; al
//   leer, falta = error de validación.
// - `Optional[X] = None` → std::optional<X>.
// - Los `dict`/`list` sin tipar de Pydantic (style, effects, masks…) se guardan
//   como Json y se irán tipando a medida que el motor los use.
// - `extra` guarda las claves que el modelo no conoce. Pydantic las ignora al
//   validar, pero siguen en el archivo (save_timeline guarda el dict tal cual), así
//   que se conservan para no perder datos al reescribirlo (DumpMode::Preserve).

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "gc/core/cow.hpp"
#include "gc/core/json.hpp"

namespace gc::core {

// --- Reencuadre con keyframes -------------------------------------------------

// Centro de la ventana de recorte en un instante. `t` es relativo al inicio del
// clip; cx/cy, normalizados (0–1) respecto al fotograma fuente.
struct Keyframe {
    double t = 0.0;  // obligatorio
    double cx = 0.0; // obligatorio, 0–1
    double cy = 0.5; // 0–1
    std::optional<double> zoom; // 0.1–1
    std::optional<std::string> pan_mode;
    std::optional<std::string> fit; // "cover" | "contain"; ausente = cover
    Json extra = Json::object();
};

// Punto del tracking automático: centro + caja de la cara (normalizada).
struct TrackPoint : Keyframe {
    double w = 0.0;
    double h = 0.0;
};

struct Reframe {
    double zoom = 1.0; // 0.1–1: fracción de la altura del fotograma que ocupa la ventana 9:16
    std::vector<Keyframe> keyframes;
    std::string pan_mode = "smooth"; // "smooth" | "direct"
    bool dual_crop = false;
    std::string split_orientation = "vertical"; // "vertical" | "horizontal"
    std::optional<double> zoom2;
    std::vector<Keyframe> keyframes2;
    std::optional<double> crop_w; // 0.05–1
    std::optional<double> crop_h; // 0.05–1
    bool master = false;              // archivo en aspecto original; receta al ver/exportar
    std::string split_layout = "auto"; // "auto" | "vertical" | "horizontal"
    std::optional<std::string> face_track_mode; // "smooth" | "direct"
    Json extra = Json::object();
};

// Seguimiento de caras cacheado en un material de vídeo (tiempos del ARCHIVO).
struct FaceTrack {
    double start = 0.0; // obligatorio
    double end = 0.0;   // obligatorio
    std::int64_t width = 0;
    std::int64_t height = 0;
    double fps = 25.0;
    std::int64_t samples = 0;
    std::vector<TrackPoint> track;
    std::vector<Keyframe> keyframes;
    std::optional<std::string> created_at;
    Json extra = Json::object();
};

// --- Transcripción --------------------------------------------------------------

// Palabra con su tiempo real (segundos absolutos del audio transcrito).
struct Word {
    std::string text; // obligatorio
    double start = 0.0; // obligatorio
    double end = 0.0;   // obligatorio
    std::optional<double> prob;
    Json extra = Json::object();
};

struct TranscriptSegment {
    double start = 0.0; // obligatorio
    double end = 0.0;   // obligatorio
    std::string text;   // obligatorio
    std::vector<Word> words;
    Json extra = Json::object();
};

struct Transcript {
    std::string id; // obligatorio
    std::optional<std::string> source_url;
    std::optional<std::string> title;
    std::string model; // obligatorio
    std::optional<std::string> language;
    std::optional<double> duration;
    std::optional<std::string> created_at;
    std::optional<std::string> file; // ruta del .txt guardado en disco
    std::vector<TranscriptSegment> segments;
    Json extra = Json::object();
};

// --- Material del proyecto --------------------------------------------------------

struct ClipInfo {
    std::int64_t index = 0; // obligatorio
    std::string filename;   // obligatorio
    std::string url;        // obligatorio: ruta relativa para descargar/reproducir
    double start = 0.0;     // obligatorio
    double end = 0.0;       // obligatorio
    std::optional<std::string> id;
    std::optional<std::string> source_url;
    std::optional<std::string> created_at;
    std::optional<std::string> label;
    std::optional<std::string> description;
    std::optional<std::string> description_ai;
    std::optional<Json> semantic; // dict
    std::optional<Reframe> reframe;
    std::optional<Transcript> transcript;
    std::optional<std::string> origin;   // "youtube" | "compose" | "pexels" | "giphy"
    std::optional<std::string> source;   // "external" | "generated"
    std::optional<std::string> provider; // "pexels" | "giphy"
    std::optional<std::string> external_id;
    std::optional<std::string> author;
    std::optional<std::string> license_info;
    // Segmento por referencia: [in_point, out_point] del archivo original.
    std::optional<double> in_point;
    std::optional<double> out_point;
    std::optional<std::string> parent_id;
    std::optional<FaceTrack> face_track;
    Json extra = Json::object();
};

struct AudioInfo {
    std::string id;       // obligatorio
    std::string filename; // obligatorio
    std::string url;      // obligatorio
    std::optional<std::string> voice;
    std::optional<std::string> voice2;
    std::optional<double> blend;
    double speed = 1.0;
    std::optional<double> pause;
    std::optional<std::string> text;
    std::optional<double> duration;
    std::optional<std::string> created_at;
    std::optional<std::string> engine; // "kokoro" | "piper" | "gemini" | "azure"
    std::optional<std::string> label;
    std::optional<std::string> description;
    std::optional<std::string> origin; // "tts" | "youtube"
    std::optional<std::string> source; // "generated" | "external"
    std::optional<std::string> youtube_url;
    std::optional<std::string> youtube_id;
    Json extra = Json::object();
};

struct ImageInfo {
    std::string id;       // obligatorio
    std::string filename; // obligatorio
    std::string url;      // obligatorio
    std::optional<std::int64_t> width;
    std::optional<std::int64_t> height;
    std::optional<std::string> created_at;
    std::optional<std::string> label;
    std::optional<std::string> description;
    std::optional<std::string> description_ai;
    std::optional<Json> semantic; // dict
    std::optional<Json> analysis; // dict: análisis de Azure Vision
    std::optional<std::string> origin = "upload";
    std::optional<std::string> source = "external";
    std::optional<std::string> provider;
    std::optional<std::string> external_id;
    std::optional<std::string> source_url;
    std::optional<std::string> author;
    std::optional<std::string> license_info;
    // GIF animado
    std::optional<bool> animated;
    std::optional<std::int64_t> frames;
    std::optional<double> fps;
    std::optional<double> duration;
    std::optional<bool> loop;
    std::optional<bool> has_alpha;
    Json extra = Json::object();
};

// --- Timeline multipista ----------------------------------------------------------

struct TimelineTrack {
    std::string id;   // obligatorio
    std::string kind; // obligatorio: "video" | "audio" | "text"
    std::string name; // obligatorio
    bool hidden = false;
    bool muted = false;
    bool locked = false;
    std::optional<Json> style; // dict: plantilla de la pista de texto
    std::optional<std::string> linked_track_id;
    bool magnetic = false; // pista magnética: sin huecos desde 0
    Json extra = Json::object();
};

// Elemento colocado en una pista. `start` es la posición en la timeline (s); el
// tramo usado del material es [in_point, out_point] del archivo fuente y dura en
// la timeline (out_point - in_point) / speed.
struct TimelineClip {
    std::string id;         // obligatorio
    std::string track_id;   // obligatorio
    std::string kind;       // obligatorio: "video" | "audio" | "text" | "image" | "shape"
    std::string asset_kind; // obligatorio: "clips" | "audios" | "images" | "sfx" | "text" | "shape"
    std::string asset_id;   // obligatorio
    std::string filename;   // obligatorio
    std::optional<std::string> name;
    double start = 0.0;
    double in_point = 0.0;
    double out_point = 0.0;
    double source_duration = 0.0;
    std::optional<bool> ref_segment;
    double volume = 1.0;
    std::optional<std::string> media_version;
    std::optional<Reframe> reframe;
    std::optional<std::string> text;
    std::optional<Json> style;    // dict
    std::string layout = "fill";  // "fill" | "overlay"
    std::optional<Json> transform; // dict {x, y, scale, rotation}
    std::string frame = "full";   // full | top | bottom | free
    std::string appear = "none";
    std::string exit = "none";
    std::string look = "none";
    std::optional<Json> effects;  // dict
    std::optional<Json> filters;  // list [{id, amount}]
    std::optional<Json> audio_fx; // dict
    bool muted = false;
    double speed = 1.0;
    bool keep_pitch = true;
    bool reverse = false;
    std::optional<Json> speed_curve; // dict
    std::optional<double> opacity;
    bool flip_h = false;
    bool flip_v = false;
    std::optional<std::string> blend_mode;
    bool disabled = false;
    std::optional<Json> beats; // dict {times, bpm, every}
    std::optional<bool> loop;
    std::vector<Word> words; // (texto) tiempos por palabra, relativos al inicio del clip
    std::optional<Json> origin; // dict: procedencia del texto
    std::optional<std::string> text_role; // "caption" | "free"
    std::optional<Json> shape;  // dict
    std::vector<Json> masks;    // list[dict]
    std::optional<Json> bg_removal; // dict
    std::optional<Json> anim;       // dict (legado)
    std::optional<Json> vfx;        // list
    std::optional<Json> animations; // dict
    std::optional<Json> keyframes;  // dict {enabled, items}
    std::string asset_scope = "project"; // "project" | "library" | "collection"
    std::optional<std::string> description;
    std::optional<std::string> note;
    std::optional<std::string> note_source; // "user" | "ai"
    std::optional<std::string> dup_of;
    std::optional<std::string> composition_id;
    Json extra = Json::object();
};

inline constexpr int kCurrentSchemaVersion = 5;

struct Timeline {
    std::int64_t version = 1;
    std::int64_t schema_version = kCurrentSchemaVersion;
    std::int64_t fps = 30;
    std::int64_t width = 720;
    std::int64_t height = 1280;
    double audio_target_db = -14.0; // LUFS objetivo del render final
    std::vector<TimelineTrack> tracks;
    // Clips con estructura compartida (ver cow.hpp): copiar la timeline es barato
    // y los snapshots comparten los clips que no cambian.
    std::vector<Cow<TimelineClip>> clips;
    std::vector<Json> markers; // list[dict] {id, t, label?, color?}
    Json extra = Json::object();
};

// --- Proyecto -----------------------------------------------------------------------

struct Project {
    std::string id;         // obligatorio
    std::string name;       // obligatorio
    std::string created_at; // obligatorio
    std::optional<std::string> folder;   // carpeta base en disco; nullopt = por defecto
    std::optional<std::string> group_id; // carpeta del inicio; nullopt = sin carpeta
    std::vector<ClipInfo> clips;
    std::vector<Transcript> transcripts;
    std::vector<AudioInfo> audios;
    std::vector<ImageInfo> images;
    std::optional<Timeline> timeline;
    std::vector<Json> motion_compositions; // list[dict]
    Json scene_directions = Json::object(); // dict
    Json visual_blueprint = Json::object(); // dict
    Json extra = Json::object();
};

// Carpeta del inicio: agrupa proyectos en la lista (no mueve archivos).
struct ProjectGroup {
    std::string id;         // obligatorio
    std::string name;       // obligatorio
    std::string created_at; // obligatorio
    Json extra = Json::object();
};

// Contenido completo de data/projects.json.
struct ProjectsFile {
    std::vector<Project> projects;
    std::vector<ProjectGroup> groups;
    Json extra = Json::object();
};

} // namespace gc::core
