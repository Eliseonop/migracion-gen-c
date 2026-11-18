#include "gc/core/recipes.hpp"

#include <algorithm>
#include <array>
#include <cstddef>

namespace gc::core {

namespace {

constexpr std::array<RecipeInfo, 5> kRecipes{{
    {"cinema_grade", "Etalonaje de cine", 10, "",
     "Capa de ajuste (naranja y turquesa + desvaído) sobre todo el vídeo y barras de cine que entran."},
    {"text_reflection", "Texto con reflejo", 7, "text",
     "Una copia volteada debajo del texto, que se desvanece, en modo Superponer."},
    {"pass_through_text", "Texto que atraviesas", 6, "text",
     "Al final del clip el texto crece hasta ×60: la cámara lo atraviesa."},
    {"film_strips", "Franjas al ritmo", 3, "media2",
     "Cada clip seleccionado en una franja horizontal (rollo de película), entrando uno tras otro "
     "en los beats de la música (o cada 0,35 s)."},
    {"subject_pop", "Sujeto que se adelanta", 1, "video",
     "El sujeto del clip, recortado con IA, aparece unos fotogramas antes que su plano, con un "
     "destello blanco y sonido de transición."},
}};

std::size_t count(std::span<const std::string> kinds, std::initializer_list<std::string_view> wanted) {
    return static_cast<std::size_t>(std::ranges::count_if(
        kinds, [&](const std::string& k) { return std::ranges::find(wanted, k) != wanted.end(); }));
}

} // namespace

std::span<const RecipeInfo> recipe_catalog() {
    return kRecipes;
}

const RecipeInfo* find_recipe(std::string_view id) {
    for (const RecipeInfo& r : kRecipes) {
        if (r.id == id) {
            return &r;
        }
    }
    return nullptr;
}

RecipeReadiness recipe_ready(std::string_view needs, std::span<const std::string> kinds) {
    if (needs == "text") {
        return count(kinds, {"text"}) > 0 ? RecipeReadiness{} : RecipeReadiness{false, "selecciona un texto"};
    }
    if (needs == "video") {
        return count(kinds, {"video"}) > 0 ? RecipeReadiness{} : RecipeReadiness{false, "selecciona un vídeo"};
    }
    if (needs == "media2") {
        return count(kinds, {"video", "image"}) >= 2
                   ? RecipeReadiness{}
                   : RecipeReadiness{false, "selecciona 2 o más vídeos o imágenes"};
    }
    return {};
}

} // namespace gc::core
