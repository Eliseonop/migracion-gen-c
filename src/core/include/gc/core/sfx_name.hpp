#pragma once

// Los borradores del modal «Nuevo SFX» / «Editar SFX» (espejo de lib/sfxName.js): el nombre que
// sale del archivo, las categorías sin «favoritos» y qué categoría manda guardar una fila.

#include <string>
#include <vector>

#include "gc/core/json.hpp"

namespace gc::core {

// sfxNameFromFile: el nombre del archivo sin ruta ni extensión.
[[nodiscard]] std::string sfx_name_from_file(const Json& filename);
// realSfxCategories: las que tienen id y no son «favoritos».
[[nodiscard]] std::vector<Json> real_sfx_categories(const Json& categories);
// upsertSfxCategory: `cat` ({id, label, count}) al final, sustituyendo la que tenga su id.
[[nodiscard]] std::vector<Json> upsert_sfx_category(const Json& categories, const Json& cat);
// matchSfxCategory: {categoryId, newCategory}; la existente con esa etiqueta o id (sin
// distinguir mayúsculas) o una nueva con ese texto.
[[nodiscard]] Json match_sfx_category(const Json& text, const Json& categories);
// sfxDraftFromSaved: la fila de un SFX ya guardado.
[[nodiscard]] Json sfx_draft_from_saved(const Json& sfx);
// sfxSavePayload: {name, categoryId, newCategory, uso} de una fila.
[[nodiscard]] Json sfx_save_payload(const Json& row, const Json& categories);

} // namespace gc::core
