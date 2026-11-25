#pragma once

// Recetas en un clic (#21): el catálogo de backend/app/recipes.py (GET /api/recipes) y
// recipeReady.js, que dice si lo seleccionado basta. Aplicarlas es ops::ApplyRecipe (edit.hpp).

#include <span>
#include <string>

#include "gc/core/json.hpp"

namespace gc::core {

// RECIPES: [{id, label, trick, needs, desc}], con `needs` = null | "text" | "video" | "media2".
[[nodiscard]] const Json& recipe_catalog();
// recipeReady: {ok} o {ok: false, why} con los tipos (`kind`) de los clips seleccionados.
[[nodiscard]] Json recipe_ready(const Json& recipe, std::span<const std::string> kinds);

} // namespace gc::core
