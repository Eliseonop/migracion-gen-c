#pragma once

// Migración del JSON de la timeline entre versiones de schema.
// Espejo de backend/app/migrations.py y text_role.py (video-yt).
//
// Reglas (las mismas que en Python):
//   - Solo hacia delante: nunca se baja de versión.
//   - Idempotente: migrar algo que ya está al día no lo cambia.
//   - Trabaja sobre el JSON crudo, ANTES de validar contra el modelo.

#include <optional>
#include <string>

#include "gc/core/json.hpp"

namespace gc::core {

// Versión declarada de una timeline cruda. Ausente o inválida = 1 (legado).
[[nodiscard]] int timeline_version(const Json& timeline);

// Lleva una timeline cruda a kCurrentSchemaVersion. Devuelve la entrada tal cual
// si no es un objeto, está vacía o ya está en una versión igual o más nueva.
[[nodiscard]] Json migrate_timeline(const Json& timeline);

// Rol de un clip de texto: "caption" (subtítulo ligado al audio) o "free".
// nullopt si el clip no es de texto.
[[nodiscard]] std::optional<std::string> resolve_text_role(const Json& clip);

} // namespace gc::core
