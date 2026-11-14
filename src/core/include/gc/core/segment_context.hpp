#pragma once

// Lo de backend/app/motion/segment_context.py que usa la dirección de escena: los términos clave
// de un texto (`key_terms`). El resto del contexto de un tramo (guion, timeline, material y
// estilo, para Motion) llega con Motion.

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace gc::core {

inline constexpr std::size_t kMaxKeyTerms = 6; // MAX_KEY_TERMS

// key_terms: los términos clave baratos de un texto, en el orden en que aparecen y sin repetir
// (sin tildes ni mayúsculas): nombres propios a mitad de frase (de 4 letras o más), palabras de
// 6 letras o más y palabras con cifras, sin las muletillas de _STOPWORDS. Se para al llegar a
// `limit`.
[[nodiscard]] std::vector<std::string> key_terms(std::string_view text, std::size_t limit = kMaxKeyTerms);

} // namespace gc::core
