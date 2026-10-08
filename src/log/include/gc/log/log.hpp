#pragma once

// Registro de GenClipPro sobre spdlog, para los ejecutables y las capas con E/S (medios,
// render, servicios, app). gc_core no lo usa: es lógica pura, solo STL y JSON.
//
// Uso: gc::log::init(...) una vez al arrancar y después spdlog::info/debug/warn/error con
// el formato de std::format. Los mensajes van a stderr («nivel: texto»), así que no se
// mezclan con la salida de una orden (stdout).
//
// El nivel sale de la variable de entorno GENCLIP_LOG (trace, debug, info, warn, error u
// off) si está y es válida; si no, del que pida el programa. El archivo de registro de la
// app (con rutas UTF-8 en Windows: SPDLOG_WCHAR_FILENAMES) llega con F5.

#include <optional>
#include <string_view>
#include <vector>

#include <spdlog/common.h>

namespace gc::log {

using Level = spdlog::level::level_enum;

inline constexpr std::string_view kEnvVar = "GENCLIP_LOG";

// «trace», «debug», «info», «warn» o «warning», «error», «critical» u «off», sin distinguir
// mayúsculas ni espacios alrededor. nullopt si no es ninguno.
[[nodiscard]] std::optional<Level> parse_level(std::string_view text);

// El nivel de GENCLIP_LOG si es válido; si no, `fallback`.
[[nodiscard]] Level level_from_env(Level fallback);

// Logger por defecto («genclip») con salida a stderr con colores y, además, `extra` (los
// tests o la app añaden los suyos). Se puede llamar otra vez: sustituye al anterior.
void init(Level level, std::vector<spdlog::sink_ptr> extra = {});

} // namespace gc::log
