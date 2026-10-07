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

// Lo que queda en ASCII de un carácter no ASCII tras NFKD + encode("ascii", "ignore"):
// tabla generada desde unicodedata (tools/gen/gen_nfkd_ascii.py); el resto se pierde.
struct NfkdAscii {
    char32_t code;
    std::string_view ascii;
};
constexpr NfkdAscii kNfkdAscii[] = {
#include "nfkd_ascii.inc"
};

std::string_view ascii_of(char32_t c) {
    const auto it = std::ranges::lower_bound(kNfkdAscii, c, {}, &NfkdAscii::code);
    return it != std::end(kNfkdAscii) && it->code == c ? it->ascii : std::string_view();
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
