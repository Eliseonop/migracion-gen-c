#include "gc/core/segment_context.hpp"

#include <algorithm>
#include <array>
#include <unordered_set>

#include "py.hpp"

namespace gc::core {

namespace {

using detail::py_isdigit;
using detail::py_isupper;
using detail::py_len;
using detail::py_lower;
using detail::py_strip_accents;
using detail::py_word_char;
using detail::utf8_next;

// _STOPWORDS.
constexpr std::array<std::string_view, 72> kStopwords{
    // es
    "para", "porque", "cuando", "donde", "desde", "hasta", "sobre", "entre", "durante", "tiene", "tienen", "puede",
    "pueden", "estos", "estas", "aquel", "aquella", "otras", "otros", "mucho", "muchos", "muchas", "siempre", "nunca",
    "también", "tambien", "ahora", "después", "despues", "antes", "quedó", "quedo", "haber", "haberlo", "forma",
    "manera", "cosas", "hacer", "había", "habia", "sería", "seria", "mismo", "misma", "todos", "todas", "nuestra",
    "nuestro", "vuelve", "acaba", "afirma", "pero", "este", "esta", "esto", "cada", "solo", "sólo", "nada", "algo",
    // en
    "about", "their", "there", "which", "would", "could", "should", "these", "those", "because", "through",
    "between"};

bool stopword(std::string_view w) { return std::ranges::find(kStopwords, w) != kStopwords.end(); }

// str.strip("-").
std::string_view strip_dashes(std::string_view s) {
    while (!s.empty() && s.front() == '-') {
        s.remove_prefix(1);
    }
    while (!s.empty() && s.back() == '-') {
        s.remove_suffix(1);
    }
    return s;
}

// re.findall(r"[\wÁÉÍÓÚÜÑáéíóúüñ][\wÁÉÍÓÚÜÑáéíóúüñ\-]*", text): las letras con tilde ya son \w.
std::vector<std::string_view> tokens(std::string_view text) {
    std::vector<std::string_view> out;
    for (std::size_t i = 0; i < text.size();) {
        const std::size_t start = i;
        if (!py_word_char(utf8_next(text, i))) {
            continue;
        }
        std::size_t end = i;
        while (end < text.size()) {
            std::size_t j = end;
            const char32_t c = utf8_next(text, j);
            if (!py_word_char(c) && c != U'-') {
                break;
            }
            end = j;
        }
        out.push_back(text.substr(start, end - start));
        i = end;
    }
    return out;
}

bool has_digit(std::string_view tok) {
    for (std::size_t i = 0; i < tok.size();) {
        if (py_isdigit(utf8_next(tok, i))) {
            return true;
        }
    }
    return false;
}

} // namespace

std::vector<std::string> key_terms(std::string_view text, std::size_t limit) {
    std::vector<std::string> out;
    std::unordered_set<std::string> seen;
    const std::vector<std::string_view> toks = tokens(text);
    for (std::size_t i = 0; i < toks.size(); ++i) {
        const std::string_view tok = toks[i];
        const std::string lowered = py_lower(tok);
        const std::string_view low = strip_dashes(lowered);
        std::string norm = py_strip_accents(low);
        if (seen.contains(norm) || stopword(low) || stopword(norm)) {
            continue;
        }
        // Mayúscula a mitad de frase (OpenAI, Navier-Stokes); ≥4 letras evita «Una», «Pero»…
        std::size_t at = 0;
        const bool proper = py_isupper(utf8_next(tok, at)) && i > 0 && py_len(tok) >= 4;
        if (py_len(low) >= 6 || proper || has_digit(tok)) {
            seen.insert(std::move(norm));
            out.emplace_back(strip_dashes(tok));
        }
        if (out.size() >= limit) {
            break;
        }
    }
    return out;
}

} // namespace gc::core
