#pragma once

// La pila de efectos animados de un clip (`clip.vfx`; normalizeVfxEntry y makeVfxEntry de
// fx/core/runtime.js): [{uid, effect, v, enabled, params, from, to}], con `from`/`to` en segundos
// locales del clip (`to` nulo = hasta el final). Los efectos salen del catálogo activo.

#include <optional>
#include <string_view>

#include "gc/core/ids.hpp"
#include "gc/core/json.hpp"

namespace gc::core {

// Una entrada saneada (parámetros completos y acotados); nada si el efecto no está en el catálogo.
[[nodiscard]] std::optional<Json> normalize_vfx_entry(const Json& raw, IdSource& ids);
// Entrada nueva con los valores por defecto (y su duración por defecto, si la tiene).
[[nodiscard]] std::optional<Json> make_vfx_entry(std::string_view effect_id, IdSource& ids);

} // namespace gc::core
