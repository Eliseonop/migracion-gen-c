#pragma once

// Las capacidades descubribles del editor para el servidor MCP (espejo de
// backend/app/mcp_server/capabilities.py de video-yt): qué dominios hay, qué verbos tiene cada
// uno y los valores válidos y el estado (modelos de transcripción, motores y voces de TTS,
// categorías de SFX…). Sin JSON-Schemas (eso es `tools/list`) ni la guía larga (`help://<domain>`).
//
// Lo que Python pregunta a la máquina y a los ajustes (motores de TTS, GPU, ffmpeg en el PATH…)
// llega ya resuelto en RuntimeEnv: los motores de TTS y la GPU son de F6 y el proveedor de IA de F7.

#include <array>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "gc/core/error.hpp"
#include "gc/core/json.hpp"
#include "gc/core/model.hpp"

namespace gc::core::mcp {

// Los enums de valores válidos (espejo de timeline_ops, clip_anim y clip_fx).
inline constexpr std::array<std::string_view, 6> kAspects{"9:16", "1:1", "16:9", "4:5", "4:3", "custom"};
inline constexpr std::array<std::string_view, 13> kMotions{
    "zoom_in",    "zoom_out", "spin",     "spin_in", "slide_left", "slide_right", "slide_up",
    "slide_down", "fade_in",  "fade_out", "pop",     "pulse",      "draw_in"};
inline constexpr std::array<std::string_view, 3> kDashStyles{"solid", "dash", "dot"};
inline constexpr std::array<std::string_view, 10> kTransitions{
    "none", "fade", "dissolve", "wipe", "zoom", "slide_left", "slide_right", "slide_up", "slide_down", "pop"};
inline constexpr std::array<std::string_view, 10> kVisualFx{"blur",       "grayscale", "sepia",  "brightness",
                                                            "contrast",   "saturation", "exposure", "whites",
                                                            "temperature", "hue"};
inline constexpr std::array<std::string_view, 8> kMaskTypes{"linear", "film", "circle", "rectangle",
                                                            "star",   "heart", "text",  "brush"};
inline constexpr std::array<std::string_view, 4> kPositions{"full", "top", "bottom", "free"};
inline constexpr std::array<std::string_view, 2> kReframeModes{"center", "manual"};
inline constexpr std::array<std::string_view, 2> kTextRoles{"caption", "free"};
inline constexpr std::array<std::string_view, 3> kTrackKinds{"video", "audio", "text"};
// schemas.CropMode, en su orden.
inline constexpr std::array<std::string_view, 4> kCropModes{"center", "smart_face", "split_left", "split_right"};

// AUDIO_FX: los ids de audio_fx.AUDIO_FX_IDS (los efectos y filtros de sonido, en su orden).
[[nodiscard]] std::vector<std::string> audio_fx_ids();
// LOOKS: los ids de clip_filters.FILTER_IDS (los filtros de color; los looks antiguos son filtros al 100 %).
[[nodiscard]] std::vector<std::string> look_ids();

// verbs_by_domain: {dominio: [tools ordenadas]}, con los dominios en el orden en que aparecen en TOOL_DOMAINS.
[[nodiscard]] Json verbs_by_domain();

// Lo que capabilities.py pregunta a la máquina y a los ajustes, ya resuelto.
struct RuntimeEnv {
    std::string transcribe_model = "base";   // transcribe_settings.load()["model"]
    Json tts_engines = Json::array();        // tts_engines(): [{id, available, reason?, voices}]
    std::vector<std::string> sfx_categories; // sfx_categories(): las categorías de la carpeta de SFX
    bool ffmpeg = false;                     // shutil.which("ffmpeg")
    bool ffprobe = false;                    // shutil.which("ffprobe")
    std::string data_dir;                    // str(config.DATA_DIR)
    Json gpu = nullptr;                      // gpu.summary()
    Json ai_provider = nullptr;              // providers.ai_config()
};

// transcribe_models: MODEL_INFO con `default` en el modelo de los ajustes.
[[nodiscard]] Json transcribe_models(const RuntimeEnv& env);
// overview: los dominios con sus verbos, los valores por defecto y la pista de qué leer después.
[[nodiscard]] Json overview(const RuntimeEnv& env);
// describe(domain): verbos, guía y valores válidos de UN dominio (sin espacios alrededor y en
// minúsculas, como Python). Error si no existe («Dominio desconocido: x. Válidos: [...]»).
[[nodiscard]] Result<Json> describe(std::string_view domain, const RuntimeEnv& env);
// runtime_info: ffmpeg y ffprobe, el modelo por defecto, los motores de TTS, data/, la GPU y el proveedor de IA.
[[nodiscard]] Json runtime_info(const RuntimeEnv& env);
// project_summary: el resumen minúsculo de un proyecto para listar y desambiguar.
[[nodiscard]] Json project_summary(const Project& project);

} // namespace gc::core::mcp
