#pragma once

// Palabras clave para «Explorar» (espejo de la parte local de backend/app/explore_keywords.py):
// las más repetidas del guion sin muletillas, con frases de dos y tres palabras, y la lectura de
// la lista que devuelve una IA. Pedírsela a la IA es de gc_cloud (F7).

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "gc/core/json.hpp"
#include "gc/core/model.hpp"

namespace gc::core {

// extract_keywords: hasta `max_n` palabras o frases, de la más frecuente a la menos (las frases
// pesan más); sin las que ya contiene una elegida.
[[nodiscard]] std::vector<std::string> extract_keywords(std::string_view text, std::size_t max_n = 8);
// gather_theme_text: descripciones, textos y transcripciones del proyecto, una por línea.
[[nodiscard]] std::string gather_theme_text(const Project& project);
// parse_keyword_list: el array JSON de la respuesta de una IA (también dentro de ``` o de otro
// texto), sin repetidas ni muletillas y como mucho 10.
[[nodiscard]] std::vector<std::string> parse_keyword_list(std::string_view raw);
// suggest_keywords sin IA: {keywords, source} con las locales o, si no hay, las clásicas.
[[nodiscard]] Json suggest_local_keywords(std::string_view text);

} // namespace gc::core
