#include "gc/core/sfx_library.hpp"

#include <algorithm>
#include <mutex>
#include <set>
#include <string_view>

#include "json_reader.hpp"
#include "py.hpp"

namespace gc::core {

using detail::py_len;
using detail::py_str;
using detail::py_truthy;
using detail::utf8_next;

namespace {

std::mutex g_mutex;
std::shared_ptr<const SfxLibrary> g_library = std::make_shared<const SfxLibrary>();

const Json& field(const Json& obj, std::string_view key) {
    static const Json kNull;
    if (!obj.is_object()) {
        return kNull;
    }
    const auto it = obj.find(key);
    return it == obj.end() ? kNull : *it;
}

// Lo que queda en ASCII de un carácter tras NFKD + encode("ascii", "ignore"). Tabulado
// para Latin-1, Latin Extended-A, ligaduras, formas de ancho completo, superíndices y
// espacios; lo demás se pierde (como cualquier carácter sin descomposición ASCII).
std::string_view ascii_of(char32_t c) {
    // Latin-1 (U+00A0–U+00FF) y Latin Extended-A (U+0100–U+017F)
    static constexpr std::string_view kLatin[] = {
        // A0-AF:   NBSP ¡ ¢ £ ¤ ¥ ¦ § ¨ © ª « ¬ SHY ® ¯
        " ", "", "", "", "", "", "", "", " ", "", "a", "", "", "", "", " ",
        // B0-BF:   ° ± ² ³ ´ µ ¶ · ¸ ¹ º » ¼ ½ ¾ ¿
        "", "", "2", "3", " ", "", "", "", " ", "1", "o", "", "14", "12", "34", "",
        // C0-CF:   À Á Â Ã Ä Å Æ Ç È É Ê Ë Ì Í Î Ï
        "A", "A", "A", "A", "A", "A", "", "C", "E", "E", "E", "E", "I", "I", "I", "I",
        // D0-DF:   Ð Ñ Ò Ó Ô Õ Ö × Ø Ù Ú Û Ü Ý Þ ß
        "", "N", "O", "O", "O", "O", "O", "", "", "U", "U", "U", "U", "Y", "", "",
        // E0-EF
        "a", "a", "a", "a", "a", "a", "", "c", "e", "e", "e", "e", "i", "i", "i", "i",
        // F0-FF
        "", "n", "o", "o", "o", "o", "o", "", "", "u", "u", "u", "u", "y", "", "y",
        // 100-10F: Ā ā Ă ă Ą ą Ć ć Ĉ ĉ Ċ ċ Č č Ď ď
        "A", "a", "A", "a", "A", "a", "C", "c", "C", "c", "C", "c", "C", "c", "D", "d",
        // 110-11F: Đ đ Ē ē Ĕ ĕ Ė ė Ę ę Ě ě Ĝ ĝ Ğ ğ
        "", "", "E", "e", "E", "e", "E", "e", "E", "e", "E", "e", "G", "g", "G", "g",
        // 120-12F: Ġ ġ Ģ ģ Ĥ ĥ Ħ ħ Ĩ ĩ Ī ī Ĭ ĭ Į į
        "G", "g", "G", "g", "H", "h", "", "", "I", "i", "I", "i", "I", "i", "I", "i",
        // 130-13F: İ ı Ĳ ĳ Ĵ ĵ Ķ ķ ĸ Ĺ ĺ Ļ ļ Ľ ľ Ŀ
        "I", "", "IJ", "ij", "J", "j", "K", "k", "", "L", "l", "L", "l", "L", "l", "L",
        // 140-14F: ŀ Ł ł Ń ń Ņ ņ Ň ň ŉ Ŋ ŋ Ō ō Ŏ ŏ
        "l", "", "", "N", "n", "N", "n", "N", "n", "n", "", "", "O", "o", "O", "o",
        // 150-15F: Ő ő Œ œ Ŕ ŕ Ŗ ŗ Ř ř Ś ś Ŝ ŝ Ş ş
        "O", "o", "", "", "R", "r", "R", "r", "R", "r", "S", "s", "S", "s", "S", "s",
        // 160-16F: Š š Ţ ţ Ť ť Ŧ ŧ Ũ ũ Ū ū Ŭ ŭ Ů ů
        "S", "s", "T", "t", "T", "t", "", "", "U", "u", "U", "u", "U", "u", "U", "u",
        // 170-17F: Ű ű Ų ų Ŵ ŵ Ŷ ŷ Ÿ Ź ź Ż ż Ž ž ſ
        "U", "u", "U", "u", "W", "w", "Y", "y", "Y", "Z", "z", "Z", "z", "Z", "z", "s"};
    static constexpr std::string_view kDigits = "0123456789";
    static constexpr std::string_view kLigatures[] = {"ff", "fi", "fl", "ffi", "ffl", "st", "st"};
    if (c >= 0xA0 && c <= 0x17F) {
        return kLatin[c - 0xA0];
    }
    if (c >= 0xFF01 && c <= 0xFF5E) { // ancho completo
        static constexpr std::string_view kAscii =
            "!\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~";
        return kAscii.substr(c - 0xFF01, 1);
    }
    if (c >= 0xFB00 && c <= 0xFB06) {
        return kLigatures[c - 0xFB00];
    }
    if (c == 0x2070) {
        return "0";
    }
    if (c >= 0x2074 && c <= 0x2079) {
        return kDigits.substr(c - 0x2070, 1);
    }
    if (c >= 0x2080 && c <= 0x2089) {
        return kDigits.substr(c - 0x2080, 1);
    }
    if ((c >= 0x2000 && c <= 0x200A) || c == 0x202F || c == 0x205F || c == 0x3000) {
        return " ";
    }
    return "";
}

// Palabras de un texto ya normalizado (separadas por un espacio).
std::vector<std::string_view> tokens(std::string_view s) {
    std::vector<std::string_view> out;
    std::size_t i = 0;
    while (i < s.size()) {
        const auto j = std::min(s.find(' ', i), s.size());
        if (j > i) {
            out.push_back(s.substr(i, j - i));
        }
        i = j + 1;
    }
    return out;
}

} // namespace

void set_sfx_library(std::shared_ptr<const SfxLibrary> library) {
    const std::lock_guard lock(g_mutex);
    g_library = library ? std::move(library) : std::make_shared<const SfxLibrary>();
}

std::shared_ptr<const SfxLibrary> sfx_library() {
    const std::lock_guard lock(g_mutex);
    return g_library;
}

std::string sfx_norm(const Json& text) {
    const std::string raw = py_truthy(text) ? py_str(text) : std::string();
    std::string ascii;
    std::size_t i = 0;
    while (i < raw.size()) {
        const char32_t c = utf8_next(raw, i);
        if (c < 0x80) {
            ascii += static_cast<char>(c);
        } else {
            ascii += ascii_of(c);
        }
    }
    // re.sub(r"[^a-z0-9]+", " ", s.lower()).strip()
    std::string out;
    bool gap = false;
    for (char ch : ascii) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
        if ((ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9')) {
            if (gap && !out.empty()) {
                out += ' ';
            }
            out += ch;
            gap = false;
        } else {
            gap = true;
        }
    }
    return out;
}

double sfx_score(const Json& item, const std::vector<Json>& keywords) {
    const std::string name = " " + sfx_norm(field(item, "name")) + " ";
    const std::string meta = " " + sfx_norm(field(item, "category")) + " " + sfx_norm(field(item, "uso")) + " " +
                             sfx_norm(field(item, "folder")) + " ";
    const auto name_toks = tokens(name);
    const std::set<std::string_view> name_tokens(name_toks.begin(), name_toks.end());
    double score = 0.0;
    for (const Json& kw : keywords) {
        const std::string k = sfx_norm(kw);
        if (k.empty()) {
            continue;
        }
        const std::string padded = " " + k + " ";
        if (name.find(padded) != std::string::npos) {
            score += 3;
        } else if (k.size() >= 4 && name.find(k) != std::string::npos) {
            score += 2;
        } else {
            std::vector<std::string_view> toks;
            for (const auto t : tokens(k)) {
                if (t.size() >= 3) {
                    toks.push_back(t);
                }
            }
            auto known = [&](std::string_view t) { return name_tokens.contains(t); };
            if (!toks.empty() && std::ranges::all_of(toks, known)) {
                score += 2;
            } else if (std::ranges::any_of(toks, known)) {
                score += 1;
            }
        }
        if (meta.find(padded) != std::string::npos) {
            score += 0.5;
        }
    }
    return score;
}

std::pair<std::vector<Json>, std::vector<Json>> match_sounds(const std::vector<Json>& sounds,
                                                             const std::vector<Json>& items) {
    std::vector<Json> used;
    std::vector<Json> matched;
    std::vector<Json> missing;
    for (const Json& s : sounds) {
        const Json& kw_raw = field(s, "keywords");
        const std::vector<Json> keywords = kw_raw.is_array() ? kw_raw.get<std::vector<Json>>() : std::vector<Json>{};
        struct Ranked {
            double score;
            std::size_t name_len;
            const Json* item;
        };
        std::vector<Ranked> ranked;
        for (const Json& it : items) {
            if (std::ranges::find(used, field(it, "id")) != used.end()) {
                continue;
            }
            const Json& name = field(it, "name");
            ranked.push_back({sfx_score(it, keywords), py_len(py_truthy(name) ? py_str(name) : std::string()), &it});
        }
        std::ranges::stable_sort(ranked, [](const Ranked& a, const Ranked& b) {
            return a.score != b.score ? a.score > b.score : a.name_len < b.name_len;
        });
        if (ranked.empty() || ranked.front().score < 2) {
            missing.push_back(s);
            continue;
        }
        const Json& it = *ranked.front().item;
        used.push_back(field(it, "id"));
        Json alts = Json::array();
        for (std::size_t i = 1; i < ranked.size() && i < 4; ++i) {
            if (ranked[i].score >= 2) {
                alts.push_back(Json{{"id", field(*ranked[i].item, "id")}, {"name", field(*ranked[i].item, "name")}});
            }
        }
        Json m = s;
        m["sfx"] = Json{{"id", field(it, "id")},
                        {"name", field(it, "name")},
                        {"url", field(it, "url")},
                        {"category", field(it, "category")}};
        m["score"] = ranked.front().score;
        m["alternatives"] = std::move(alts);
        matched.push_back(std::move(m));
    }
    return {std::move(matched), std::move(missing)};
}

} // namespace gc::core
