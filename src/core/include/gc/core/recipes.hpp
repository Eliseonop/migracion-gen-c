#pragma once

// Recetas en un clic (#21): el catálogo (RECIPES de backend/app/recipes.py, lo que da
// GET /api/recipes) y si una receta se puede aplicar con lo seleccionado (recipeReady.js).
// La receta en sí es la operación apply_recipe (edit_ops_recipes.cpp).

#include <span>
#include <string>
#include <string_view>

namespace gc::core {

struct RecipeInfo {
    std::string_view id;
    std::string_view label;
    int trick = 0;          // el truco del vídeo «10 CapCut Tricks»
    std::string_view needs; // "" (nada), "text", "video" o "media2" (2 o más vídeos o imágenes)
    std::string_view desc;
};

// Las cinco recetas, en el orden de RECIPES.
[[nodiscard]] std::span<const RecipeInfo> recipe_catalog();
// nullptr si no hay ninguna con ese id.
[[nodiscard]] const RecipeInfo* find_recipe(std::string_view id);

struct RecipeReadiness {
    bool ok = true;
    std::string_view why; // «selecciona un texto»… cuando no se puede
};

// recipeReady(recipe, selected) con `needs` de la receta ("" si es null) y el `kind` de cada clip
// seleccionado: el mismo criterio que recipes._need del backend.
[[nodiscard]] RecipeReadiness recipe_ready(std::string_view needs, std::span<const std::string> kinds);

} // namespace gc::core
