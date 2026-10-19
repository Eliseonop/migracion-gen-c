#include "gc/core/keywords.hpp"

#include <algorithm>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "gc/core/io.hpp"
#include "json_reader.hpp"
#include "py.hpp"

namespace gc::core {

namespace {

using detail::py_len;
using detail::py_lower;
using detail::py_strip;

const std::vector<std::string> kClassic{"ciencia", "películas", "cine", "épico", "futuro", "motion"};

const std::unordered_set<std::string>& stopwords() {
    static const std::unordered_set<std::string> kWords{
        "el", "la", "los", "las", "un", "una", "unos", "unas", "de", "del", "al", "y", "o", "u",
        "que", "qué", "cual", "cuál", "cuales", "cuáles", "como", "cómo", "cuando", "cuándo",
        "donde", "dónde", "porque", "porqué", "por", "para", "con", "sin", "sobre", "entre",
        "hasta", "desde", "hacia", "segun", "según", "durante", "mediante", "contra",
        "este", "esta", "estos", "estas", "ese", "esa", "esos", "esas", "aquel", "aquella",
        "aqui", "aquí", "alli", "allí", "ahi", "ahí", "muy", "mas", "más", "pero", "si", "sí",
        "no", "ni", "ya", "tan", "tambien", "también", "entonces", "pues", "bueno", "vale",
        "ok", "okay", "hola", "gracias", "favor", "porfavor", "please", "just", "really",
        "esto", "eso", "aquello", "algo", "nada", "todo", "todos", "todas",
        "me", "te", "se", "nos", "os", "le", "les", "lo", "mi", "mis", "tu", "tú", "tus",
        "su", "sus", "yo", "él", "ella", "ellos", "ellas", "usted", "ustedes",
        "ser", "soy", "es", "son", "era", "fue", "estar", "está", "estan", "están", "estoy",
        "haber", "hay", "ha", "han", "he", "hacer", "hace", "hacen", "tener", "tiene", "tienen",
        "poder", "puede", "pueden", "puedo", "decir", "dice", "ir", "va", "van", "voy",
        "ver", "ve", "ves", "veo", "veas", "vea", "vemos", "dar", "da", "saber", "sé", "quiere", "quiero",
        "quieren", "querer", "vamos", "the", "a", "an", "and", "or", "but", "of", "to", "in", "on",
        "for", "with", "at", "from", "by", "is", "are", "was", "were", "be", "been",
        "this", "that", "it", "as", "if", "not", "so", "can", "will", "would", "could",
        "should", "you", "we", "they", "he", "she", "i", "my", "your", "our"};
    return kWords;
}

// El siguiente punto de código UTF-8 en `s` desde `i` (avanza `i`); los bytes sueltos valen 0xFFFD.
char32_t next_code_point(std::string_view s, std::size_t& i) {
    const auto b = static_cast<unsigned char>(s[i]);
    const int extra = b < 0x80 ? 0 : b >= 0xF0 ? 3 : b >= 0xE0 ? 2 : b >= 0xC0 ? 1 : -1;
    if (extra < 0 || i + static_cast<std::size_t>(extra) >= s.size() + (extra == 0 ? 1 : 0)) {
        ++i;
        return extra == 0 ? b : 0xFFFD;
    }
    char32_t cp = extra == 0 ? b : b & (0x3FU >> static_cast<unsigned>(extra));
    for (int k = 1; k <= extra; ++k) {
        cp = (cp << 6U) | (static_cast<unsigned char>(s[i + static_cast<std::size_t>(k)]) & 0x3FU);
    }
    i += static_cast<std::size_t>(extra) + 1;
    return cp;
}

// _TOKEN = [a-záéíóúüñ0-9]+ con re.I: también sus mayúsculas y, por [a-z] con IGNORECASE, İ ı ſ K.
bool token_char(char32_t c) {
    if (c < 0x80) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
    }
    switch (c) {
    case U'á': case U'é': case U'í': case U'ó': case U'ú': case U'ü': case U'ñ':
    case U'Á': case U'É': case U'Í': case U'Ó': case U'Ú': case U'Ü': case U'Ñ':
    case U'İ': case U'ı': case U'ſ': case U'K': return true;
    default: return false;
    }
}

std::vector<std::string> tokens(std::string_view text) {
    std::vector<std::string> out;
    std::size_t start = std::string_view::npos;
    for (std::size_t i = 0; i <= text.size();) {
        const std::size_t at = i;
        const bool inside = i < text.size() && token_char(next_code_point(text, i));
        if (inside && start == std::string_view::npos) {
            start = at;
        } else if (!inside && start != std::string_view::npos) {
            out.push_back(py_lower(text.substr(start, at - start)));
            start = std::string_view::npos;
        }
        if (at == text.size()) {
            break;
        }
    }
    return out;
}

void push(std::vector<std::string>& parts, const std::optional<std::string>& v) {
    if (v) {
        if (std::string s = py_strip(*v); !s.empty()) {
            parts.push_back(std::move(s));
        }
    }
}

// Quita ``` y un «json» opcional al principio, y ``` al final (el re.sub de parse_keyword_list; los
// espacios de en medio se van con el strip de después).
std::string strip_fence(std::string s) {
    s.erase(0, 3);
    if (s.size() >= 4 && py_lower(s.substr(0, 4)) == "json") {
        s.erase(0, 4);
    }
    if (s.ends_with("```")) {
        s.resize(s.size() - 3);
    }
    return py_strip(s);
}

} // namespace

std::vector<std::string> extract_keywords(std::string_view text, std::size_t max_n) {
    std::vector<std::string> words;
    for (std::string& w : tokens(text)) {
        if (py_len(w) >= 3 && !stopwords().contains(w)) {
            words.push_back(std::move(w));
        }
    }
    if (words.empty()) {
        return {};
    }
    std::vector<std::pair<std::string, double>> freq; // en el orden en que aparecen, como el dict
    std::unordered_map<std::string, std::size_t> at;
    const auto bump = [&](const std::string& key, double n) {
        const auto [it, fresh] = at.try_emplace(key, freq.size());
        if (fresh) {
            freq.emplace_back(key, 0.0);
        }
        freq[it->second].second += n;
    };
    for (const std::string& w : words) {
        bump(w, 1);
    }
    for (std::size_t i = 0; i + 1 < words.size(); ++i) {
        bump(words[i] + " " + words[i + 1], 2.2);
    }
    for (std::size_t i = 0; i + 2 < words.size(); ++i) {
        bump(words[i] + " " + words[i + 1] + " " + words[i + 2], 2.6);
    }
    std::ranges::stable_sort(freq, [](const auto& a, const auto& b) {
        return a.second != b.second ? a.second > b.second : py_len(a.first) < py_len(b.first);
    });
    std::vector<std::string> out;
    const auto spaces = [](const std::string& s) { return std::ranges::count(s, ' '); };
    for (const auto& [key, n] : freq) {
        const bool covered = std::ranges::any_of(out, [&](const std::string& p) {
            return key != p && (p.contains(key) || (key.contains(p) && spaces(p) > spaces(key)));
        });
        if (covered) {
            continue;
        }
        out.push_back(key);
        if (out.size() >= max_n) {
            break;
        }
    }
    return out;
}

std::string gather_theme_text(const Project& project) {
    std::vector<std::string> parts;
    for (const AudioInfo& a : project.audios) {
        push(parts, a.description);
        push(parts, a.text);
    }
    for (const ClipInfo& c : project.clips) {
        push(parts, c.description);
        if (c.transcript) {
            for (const TranscriptSegment& s : c.transcript->segments) {
                push(parts, s.text);
            }
        }
    }
    for (const Transcript& t : project.transcripts) {
        for (const TranscriptSegment& s : t.segments) {
            push(parts, s.text);
        }
    }
    if (project.timeline) {
        for (const auto& c : project.timeline->clips) {
            if (c->kind == "audio" || c->kind == "text") {
                push(parts, c->description);
                push(parts, c->text);
            }
        }
    }
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        out += (i > 0 ? "\n" : "") + parts[i];
    }
    return out;
}

std::vector<std::string> parse_keyword_list(std::string_view raw) {
    std::string s = py_strip(raw);
    if (s.starts_with("```")) {
        s = strip_fence(std::move(s));
    }
    auto data = py_json_loads(s);
    if (!data) {
        const auto open = s.find('[');
        const auto close = s.rfind(']');
        data = open != std::string::npos && close != std::string::npos && close > open
                   ? py_json_loads(std::string_view(s).substr(open, close - open + 1))
                   : data;
    }
    if (!data || !data->is_array()) {
        return {};
    }
    std::vector<std::string> out;
    std::unordered_set<std::string> seen;
    for (const Json& item : *data) {
        std::string joined;
        for (const std::string& part : detail::py_split(detail::py_str(item))) {
            joined += (joined.empty() ? "" : " ") + part;
        }
        const std::string key = py_strip(detail::py_slice(joined, 48));
        const std::string low = py_lower(key);
        if (py_len(key) < 2 || seen.contains(low) || stopwords().contains(low)) {
            continue;
        }
        seen.insert(low);
        out.push_back(key);
        if (out.size() >= 10) {
            break;
        }
    }
    return out;
}

Json suggest_local_keywords(std::string_view text) {
    const std::string blob = py_strip(text);
    const auto local = blob.empty() ? std::vector<std::string>{} : extract_keywords(blob);
    if (!local.empty()) {
        return Json{{"keywords", local}, {"source", "local"}};
    }
    return Json{{"keywords", kClassic}, {"source", "classic"}};
}

} // namespace gc::core
