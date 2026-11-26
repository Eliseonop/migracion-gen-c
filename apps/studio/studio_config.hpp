#pragma once

// studio.json, en la carpeta de configuración de la app (AppConfigLocation): lo que el studio recuerda fuera de las
// carpetas de proyectos. Un objeto JSON con textos: «root», la carpeta de los proyectos (projects_folder.hpp), e
// «ytdlp», el yt-dlp elegido para «Directo» (live_recorder.hpp).

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

#include "gc/core/error.hpp"

namespace gc::studio::studio_config {

// El texto de `key` en `config`, si lo hay y no está vacío.
[[nodiscard]] std::optional<std::string> get(const std::filesystem::path& config, std::string_view key);
// Pone `key` (nullopt la quita) y deja lo demás que haya; crea la carpeta si hace falta.
Result<void> set(const std::filesystem::path& config, std::string_view key, const std::optional<std::string>& value);

} // namespace gc::studio::studio_config
