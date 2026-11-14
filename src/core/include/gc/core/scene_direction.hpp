#pragma once

// Dirección de escena (espejo de backend/app/scene_direction.py de video-yt), la parte que no
// necesita la IA: la escaleta de TRAMOS del guion, la dirección visual GLOBAL, el material y el pack
// de contexto de un tramo.
//
//   - El documento: normalize_component(s), normalize_segment, normalize_doc, load y save
//     (projects.save_scene_directions), update_segment, apply_plan_all y get_segment.
//   - La dirección global (Fase 5): normalize_blueprint, load_blueprint y save_blueprint, con el
//     catálogo de direcciones creativas de motion/directions.py (clave, nombre y resumen).
//   - El guion en frases con tiempos: script_units (subtítulos → transcripción del material →
//     texto del audio estimado → nada), text_in_range y auto_segments (la escaleta propuesta).
//   - timeline_duration y status_summary (el «estás aquí» de scene_direction_status).
//   - El material: su metadata semántica, el catálogo del proyecto y de la biblioteca y el ranking.
//   - El pack (scene_direction_pack): build_pack, pack_text, estimate_tokens, brief_defaults y
//     skeleton_beats, con caption_lines, range_words, material_usage y recent_materials.
//
// Lo que en Python depende del reloj y de uuid4 llega en SceneEnv: `now` es time.time() y los ids
// de un tramo nuevo («sd_» + 8 hex) salen de `ids`. Se llaman en el mismo orden que en Python, así
// que con las mismas secuencias el resultado es el mismo byte a byte.
//
// Errores como en Python: un LookupError («Proyecto no encontrado.», «Tramo no encontrado.») es
// NotFound con su texto; lo que en Python es un crash (recorrer un número, un `mode` que es una
// lista, int() de infinito en `complexity`) es ErrorCode::Internal.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "gc/core/error.hpp"
#include "gc/core/ids.hpp"
#include "gc/core/json.hpp"
#include "gc/core/library.hpp"
#include "gc/core/model.hpp"
#include "gc/core/projects_store.hpp"

namespace gc::core::scene {

struct SceneEnv {
    IdSource& ids;                // uuid.uuid4 (los ids «sd_…» de los tramos nuevos)
    std::function<double()> now;  // time.time() (updated_at)
};

// Una dirección creativa del catálogo (motion/directions.py), con lo que lista scene_get_blueprint.
struct DirectionInfo {
    std::string_view key;
    std::string_view label;
    std::string_view summary;
};

// _DIRECTIONS en su orden y DEFAULT_DIRECTION.
[[nodiscard]] const std::vector<DirectionInfo>& directions();
inline constexpr std::string_view kDefaultDirection = "sketchbook";
[[nodiscard]] bool is_direction(std::string_view key);

// MODES (clave, label y la instrucción para la IA), STATUSES, MATERIAL_ROLES y VOCABULARY.
struct ModeInfo {
    std::string_view key;
    std::string_view label;
    std::string_view ai;
};
[[nodiscard]] const std::vector<ModeInfo>& modes();
[[nodiscard]] const std::vector<std::string_view>& statuses();
[[nodiscard]] const std::vector<std::string_view>& material_roles();
[[nodiscard]] const std::vector<std::string_view>& vocabulary();

// --- El documento ------------------------------------------------------------------------------

// normalize_component: un componente de la composición híbrida, o nullopt si no vale.
[[nodiscard]] std::optional<Json> normalize_component(const Json& raw);
// normalize_components: como mucho 8. Un valor que no se puede recorrer (un número) es un crash.
[[nodiscard]] Result<Json> normalize_components(const Json& raw);
// normalize_segment: el tramo normalizado, o nullopt si no es un objeto con start/end válidos y
// de 0,2 s o más.
[[nodiscard]] Result<std::optional<Json>> normalize_segment(SceneEnv& env, const Json& raw);
// normalize_doc: {version, segments} sin tramos inválidos ni ids repetidos, ordenado por (start, end)
// y sin referencias rotas.
[[nodiscard]] Result<Json> normalize_doc(SceneEnv& env, const Json& raw);
// load(proj): la escaleta guardada, normalizada.
[[nodiscard]] Result<Json> load(SceneEnv& env, const Project& project);
// save: normaliza y guarda (projects.save_scene_directions). Sin el proyecto, NotFound.
[[nodiscard]] Result<Json> save(SceneEnv& env, ProjectsStore& store, std::string_view pid, const Json& raw);
// update_segment: {**tramo, **patch, id, updated_at} y guarda la escaleta.
[[nodiscard]] Result<Json> update_segment(SceneEnv& env, ProjectsStore& store, std::string_view pid,
                                          std::string_view segment_id, const Json& patch);
// apply_plan_all: los campos del plan (mode, composition_intent, complexity, no_visual) de cada
// patch, por id. La escaleta guardada con «changed».
[[nodiscard]] Result<Json> apply_plan_all(SceneEnv& env, ProjectsStore& store, std::string_view pid,
                                          const Json& patches);
// get_segment: (tramo, escaleta). Sin el tramo, NotFound «Tramo no encontrado.».
[[nodiscard]] Result<std::pair<Json, Json>> get_segment(SceneEnv& env, const Project& project,
                                                        std::string_view segment_id);

// --- La dirección visual global ----------------------------------------------------------------

// normalize_blueprint: solo las claves con contenido; {} si no queda nada.
[[nodiscard]] Json normalize_blueprint(SceneEnv& env, const Json& raw);
[[nodiscard]] Json load_blueprint(SceneEnv& env, const Project& project);
[[nodiscard]] Result<Json> save_blueprint(SceneEnv& env, ProjectsStore& store, std::string_view pid,
                                          const Json& raw);

// --- El guion y el estado ------------------------------------------------------------------------

// timeline_duration: el fin del último clip, redondeado a ms; 0.0 sin timeline.
[[nodiscard]] double timeline_duration(const Project& project);
// script_units: las frases [{start, end, text}] y la fuente («captions», «transcript»,
// «audio_text_estimate» o «none»).
[[nodiscard]] std::pair<Json, std::string> script_units(const Project& project);
// text_in_range: las frases cuyo centro cae en [start − 1 ms, end), unidas por espacios.
[[nodiscard]] std::string text_in_range(const Json& units, double start, double end);
// auto_segments: la escaleta propuesta (tramos de ≈6 s), conservando los de `keep` que ya tienen
// dirección (mode, instruction o composition_id). `keep` son tramos normalizados.
[[nodiscard]] Result<Json> auto_segments(SceneEnv& env, const Project& project, const Json& keep);
// status_summary: el mapa del montaje (scene_direction_status).
[[nodiscard]] Result<Json> status_summary(SceneEnv& env, const Project& project);

// --- El material -------------------------------------------------------------------------------

inline constexpr std::size_t kMaxCandidates = 5; // MAX_CANDIDATES

// normalize_semantic: la metadata semántica de un material (subjects y actions, listas de hasta 8;
// visual_content, environment, mood, composition y suggested_usage; visual_priority). {} si no queda
// nada.
[[nodiscard]] Json normalize_semantic(const Json& raw);
// semantic_blob: la metadata en texto plano, para el ranking. Un valor que no es texto (o una lista
// de textos) es un crash, como el TypeError de Python.
[[nodiscard]] Result<std::string> semantic_blob(const Json& semantic);
// set_material_meta: guarda la metadata normalizada de un clip o una imagen del proyecto
// (`scope` «project») o de la biblioteca («library»). `kind` que no sea «clips» ni «images» es un
// InvalidArgument; el material que falta, NotFound («Material no encontrado.» o «Recurso no
// encontrado.»).
[[nodiscard]] Result<Json> set_material_meta(ProjectsStore& store, LibraryFile& library, std::string_view pid,
                                             const Json& kind, std::string_view ident, const Json& meta,
                                             std::string_view scope = "project");
// material_catalog: los vídeos y las imágenes del proyecto y de la biblioteca, con título,
// descripción, duración, URL y metadata semántica. Si la biblioteca no se puede leer, sin ella.
[[nodiscard]] Result<Json> material_catalog(const Project& project, const LibraryFile& library);
// rank_materials: los `limit` del catálogo con más términos clave del texto (`match`); a igualdad,
// primero los que tienen descripción o metadata, y luego en el orden del catálogo.
[[nodiscard]] Result<Json> rank_materials(const Json& catalog, std::string_view text,
                                          std::size_t limit = kMaxCandidates);

// --- El paquete de contexto de un tramo (lo único que ve la IA) --------------------------------

inline constexpr double kRecentWindow = 20.0; // RECENT_WINDOW: s antes del tramo que cuentan como recientes

// caption_lines: las líneas de subtítulo del tramo, por orden de inicio y con tiempos relativos a él.
// Con palabras con tiempo, solo las que tienen el centro dentro (una línea que cruza el borde no
// arrastra texto ajeno).
[[nodiscard]] Json caption_lines(const Project& project, double start, double end);
// range_words: las palabras con tiempo de los subtítulos con el centro en el tramo (o, si no hay
// ninguna, las de la transcripción del material), relativas a él.
[[nodiscard]] Json range_words(const Project& project, double start, double end);
// material_usage: dónde aparece ya cada vídeo («clips:<id>») o imagen («images:<id>») en la timeline,
// por orden de inicio. No mira de dónde es el material (proyecto o biblioteca).
[[nodiscard]] Json material_usage(const Project& project);
// recent_materials: los materiales que terminan como mucho 20 s antes de `start`, del más reciente al
// más antiguo y 5 como mucho, con el título del catálogo.
[[nodiscard]] Json recent_materials(const Json& catalog, const Json& usage, double start);
// build_pack: el pack de un tramo normalizado (`segment`) dentro de su escaleta normalizada (`doc`):
// el guion del tramo con la frase de antes y la de después, los subtítulos y las palabras, la
// dirección, el material elegido (o 5 candidatos), la referencia y los vecinos, lo que ya ocupa la
// timeline, el material usado hace poco, la composición planificada, el estilo y la dirección global.
[[nodiscard]] Result<Json> build_pack(SceneEnv& env, const Project& project, const LibraryFile& library,
                                      const Json& segment, const Json& doc);
// pack_text: el texto exacto que recibe la IA. Lo que Python no puede escribir (sumar un texto y una
// lista de la metadata semántica, unir lo que no es texto) es un crash.
[[nodiscard]] Result<std::string> pack_text(const Json& pack);
// estimate_tokens: max(1, round(len(text) / 3.6)).
[[nodiscard]] std::int64_t estimate_tokens(std::string_view text);
// brief_defaults: el arranque del brief de Generar Escena del tramo (idea, guion, intención, imágenes
// que deben salir, estructura y notas). Un título que no es texto puede ser un crash, como en Python.
[[nodiscard]] Result<Json> brief_defaults(const Json& pack);
// skeleton_beats: los beats del tramo (tiempos relativos a él) con `pace` segundos de media: las
// palabras (o las líneas) se cortan en fin de frase, en coma o si el grupo se alarga, y un beat de
// menos de 0,8 s se une al anterior. Sin palabras ni líneas, partes iguales; con una duración NaN es
// el ValueError de round() (InvalidArgument, con su texto) e infinita, un crash.
[[nodiscard]] Result<Json> skeleton_beats(const Json& pack, double pace);

} // namespace gc::core::scene
