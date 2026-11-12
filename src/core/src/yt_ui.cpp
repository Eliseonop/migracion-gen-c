#include "gc/core/yt_ui.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <format>
#include <optional>
#include <string_view>
#include <vector>

#include "gc/core/timeline_ui.hpp"
#include "py.hpp"

namespace gc::core {

namespace {

// String(x) de un número de JS: entero sin decimales.
std::string js_num_text(double v) {
    if (std::isfinite(v) && v == std::floor(v) && std::abs(v) < 1e21) {
        return std::format("{}", static_cast<long long>(v));
    }
    return std::format("{}", v);
}

// x.trim() de un texto de JS (lo que no es texto pasa por String; null/undefined, «»).
std::string trimmed(const Json& v) {
    std::string s = v.is_string() ? v.get<std::string>() : v.is_null() ? std::string() : v.dump();
    const auto first = s.find_first_not_of(" \t\r\n\f\v");
    if (first == std::string::npos) {
        return {};
    }
    return s.substr(first, s.find_last_not_of(" \t\r\n\f\v") - first + 1);
}

// (x || '') de JS sobre un campo de un objeto.
Json field_or_empty(const Json& item, const char* key) {
    const Json v = item.is_object() ? item.value(key, Json()) : Json();
    const bool falsy = v.is_null() || (v.is_string() && v.get_ref<const std::string&>().empty()) ||
                       (v.is_boolean() && !v.get<bool>()) || (v.is_number() && (v.get<double>() == 0 || std::isnan(v.get<double>())));
    return falsy ? Json("") : v;
}


// Los espacios que quita String.prototype.trim (WhiteSpace y LineTerminator de ECMAScript).
constexpr std::array<std::string_view, 25> kJsSpaces{
    "\t",           "\n",           "\v",           "\f",           "\r",           " ",           "\xC2\xA0",
    "\xE1\x9A\x80", "\xE2\x80\x80", "\xE2\x80\x81", "\xE2\x80\x82", "\xE2\x80\x83", "\xE2\x80\x84", "\xE2\x80\x85",
    "\xE2\x80\x86", "\xE2\x80\x87", "\xE2\x80\x88", "\xE2\x80\x89", "\xE2\x80\x8A", "\xE2\x80\xA8", "\xE2\x80\xA9",
    "\xE2\x80\xAF", "\xE2\x81\x9F", "\xE3\x80\x80", "\xEF\xBB\xBF"};

std::string_view js_trim(std::string_view text) {
    for (bool again = true; again;) {
        again = false;
        for (const std::string_view space : kJsSpaces) {
            if (text.starts_with(space)) {
                text.remove_prefix(space.size());
                again = true;
            }
            if (text.ends_with(space)) {
                text.remove_suffix(space.size());
                again = true;
            }
        }
    }
    return text;
}

// String(x) de JS para un valor de JSON (nullptr es undefined).
std::string js_string(const Json* v) {
    if (v == nullptr) {
        return "undefined";
    }
    if (v->is_string()) {
        return v->get<std::string>();
    }
    if (v->is_null()) {
        return "null";
    }
    if (v->is_boolean()) {
        return v->get<bool>() ? "true" : "false";
    }
    if (v->is_number()) {
        const double n = v->get<double>();
        if (std::isnan(n)) {
            return "NaN";
        }
        if (std::isinf(n)) {
            return n > 0 ? "Infinity" : "-Infinity";
        }
        constexpr double kMaxExact = 1e21; // de ahí en adelante, JS pasa a la notación exponencial
        return std::trunc(n) == n && std::abs(n) < kMaxExact ? js_to_fixed(n, 0) : detail::py_repr(n);
    }
    if (v->is_array()) { // join(','): null y undefined, vacíos
        std::string out;
        bool first = true;
        for (const Json& x : *v) {
            out += (first ? "" : ",") + (x.is_null() ? std::string() : js_string(&x));
            first = false;
        }
        return out;
    }
    return "[object Object]";
}

const Json* member(const Json* obj, const char* key) {
    if (obj == nullptr || !obj->is_object()) {
        return nullptr;
    }
    const auto it = obj->find(key);
    return it == obj->end() ? nullptr : &*it;
}

// Verdad de JS: null, undefined, false, 0, NaN y «» son falsos.
bool js_truthy(const Json* v) {
    if (v == nullptr || v->is_null()) {
        return false;
    }
    if (v->is_boolean()) {
        return v->get<bool>();
    }
    if (v->is_number()) {
        const double n = v->get<double>();
        return n != 0 && !std::isnan(n);
    }
    if (v->is_string()) {
        return !v->get_ref<const std::string&>().empty();
    }
    return true;
}

// --- El host de `new URL(s).hostname` (WHATWG), o nullopt si `new URL` lanza ---

bool ascii_alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool ascii_digit(char c) { return c >= '0' && c <= '9'; }
char ascii_lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; }
std::string lower_ascii(std::string s) {
    std::ranges::transform(s, s.begin(), ascii_lower);
    return s;
}
int hex_value(char c) {
    if (ascii_digit(c)) {
        return c - '0';
    }
    const char l = ascii_lower(c);
    return l >= 'a' && l <= 'f' ? l - 'a' + 10 : -1;
}

// Un número de IPv4 de WHATWG («0x…» en hexadecimal, «0…» en octal, si no decimal); nullopt si no lo es.
std::optional<std::uint64_t> ipv4_number(std::string_view part) {
    std::uint64_t radix = 10;
    if (part.size() >= 2 && part[0] == '0' && (part[1] == 'x' || part[1] == 'X')) {
        radix = 16;
        part.remove_prefix(2);
    } else if (part.size() >= 2 && part[0] == '0') {
        radix = 8;
        part.remove_prefix(1);
    }
    constexpr std::uint64_t kCap = 0xFFFFFFFFFFULL; // ya no cabe en 32 bits: basta con saber que se pasa
    std::uint64_t n = 0;
    for (const char c : part) {
        const int d = hex_value(c);
        if (d < 0 || static_cast<std::uint64_t>(d) >= radix) {
            return std::nullopt;
        }
        n = std::min(kCap, (n * radix) + static_cast<std::uint64_t>(d));
    }
    return n;
}

std::vector<std::string_view> split_dots(std::string_view host) {
    std::vector<std::string_view> parts;
    std::size_t from = 0;
    for (std::size_t i = 0; i <= host.size(); ++i) {
        if (i == host.size() || host[i] == '.') {
            parts.push_back(host.substr(from, i - from));
            from = i + 1;
        }
    }
    return parts;
}

// «ends in a number» de WHATWG: la última etiqueta (sin la vacía del punto final) es un número.
bool ends_in_number(std::string_view host) {
    auto parts = split_dots(host);
    if (parts.back().empty()) {
        if (parts.size() == 1) {
            return false;
        }
        parts.pop_back();
    }
    const std::string_view last = parts.back();
    if (!last.empty() && std::ranges::all_of(last, ascii_digit)) {
        return true;
    }
    return last.size() >= 2 && last[0] == '0' && (last[1] == 'x' || last[1] == 'X') && ipv4_number(last).has_value();
}

// El IPv4 parser de WHATWG: si falla, `new URL` lanza.
bool valid_ipv4(std::string_view host) {
    auto parts = split_dots(host);
    if (parts.back().empty() && parts.size() > 1) {
        parts.pop_back();
    }
    constexpr std::size_t kMaxParts = 4;
    if (parts.size() > kMaxParts) {
        return false;
    }
    std::vector<std::uint64_t> nums;
    for (const std::string_view p : parts) {
        const auto n = p.empty() ? std::nullopt : ipv4_number(p);
        if (!n) {
            return false;
        }
        nums.push_back(*n);
    }
    constexpr std::uint64_t kByte = 256;
    for (std::size_t i = 0; i + 1 < nums.size(); ++i) {
        if (nums[i] >= kByte) {
            return false;
        }
    }
    std::uint64_t limit = 1;
    for (std::size_t i = nums.size(); i <= kMaxParts; ++i) {
        limit *= kByte;
    }
    return nums.back() < limit;
}

// El IPv6 parser de WHATWG: grupos de hasta 4 hexadecimales, un «::» como mucho y un IPv4 al final.
// El «::» ocupa al menos un grupo (WHATWG sube pieceIndex con él): con él caben 7 como mucho; sin él, 8
// justos. Pasarse de 8 a medias lo rechaza igual la cuenta del final.
bool valid_ipv6(std::string_view in) {
    constexpr int kGroups = 8;
    int groups = 0;
    bool compressed = false;
    std::size_t i = 0;
    if (in.starts_with("::")) {
        compressed = true;
        i = 2;
    } else if (in.starts_with(':')) {
        return false;
    }
    while (i < in.size()) {
        std::size_t j = i;
        while (j < in.size() && j - i < 4 && hex_value(in[j]) >= 0) {
            ++j;
        }
        if (j < in.size() && in[j] == '.') { // el IPv4 del final: desde el principio del grupo
            if (groups > kGroups - 2) {
                return false;
            }
            const auto parts = split_dots(in.substr(i));
            if (parts.size() != 4) {
                return false;
            }
            for (const std::string_view p : parts) {
                constexpr int kMaxByte = 255;
                if (p.empty() || (p.size() > 1 && p[0] == '0') || !std::ranges::all_of(p, ascii_digit) || p.size() > 3 ||
                    std::stoi(std::string(p)) > kMaxByte) {
                    return false;
                }
            }
            groups += 2;
            i = in.size();
            break;
        }
        if (j == i) {
            return false;
        }
        ++groups;
        i = j;
        if (i == in.size()) {
            break;
        }
        if (in[i] != ':') {
            return false;
        }
        ++i;
        if (i < in.size() && in[i] == ':') {
            if (compressed) {
                return false;
            }
            compressed = true;
            ++i;
        } else if (i == in.size()) {
            return false; // termina en un «:» suelto
        }
    }
    return compressed ? groups < kGroups : groups == kGroups;
}

std::string percent_decode(std::string_view s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size() && hex_value(s[i + 1]) >= 0 &&
            hex_value(s[i + 2]) >= 0) {
            constexpr int kHex = 16;
            out += static_cast<char>((hex_value(s[i + 1]) * kHex) + hex_value(s[i + 2]));
            i += 2;
        } else {
            out += s[i];
        }
    }
    return out;
}

// Los «forbidden host code points» de WHATWG (con el NUL).
constexpr std::string_view kForbiddenHost{"\0\t\n\r #/:<>?@[\\]^|", 17};

bool all_digits(std::string_view s) { return std::ranges::all_of(s, ascii_digit); }

std::optional<std::string> url_hostname(std::string_view input) {
    std::string s;
    for (const char c : input) {
        if (c != '\t' && c != '\n' && c != '\r') { // el analizador quita los tabuladores y los saltos de línea
            s += c;
        }
    }
    const std::size_t colon = s.find(':');
    const std::string scheme = lower_ascii(s.substr(0, colon));
    std::string_view rest = std::string_view(s).substr(colon + 1);
    const bool special = scheme == "http" || scheme == "https" || scheme == "ftp" || scheme == "ws" ||
                         scheme == "wss" || scheme == "file";
    const auto stop = [&](char c) { return c == '/' || c == '?' || c == '#' || (special && c == '\\'); };
    if (special) {
        while (!rest.empty() && (rest.front() == '/' || rest.front() == '\\')) {
            rest.remove_prefix(1);
        }
    } else {
        rest.remove_prefix(2); // el «//» que pide la regex
    }
    std::size_t end = 0;
    while (end < rest.size() && !stop(rest[end])) {
        ++end;
    }
    std::string_view authority = rest.substr(0, end);
    if (scheme == "file") { // sin usuario ni puerto; «localhost» es el host vacío
        const std::string host = lower_ascii(percent_decode(authority));
        if (host.find_first_of(kForbiddenHost) != std::string::npos || host.find('%') != std::string::npos) {
            return std::nullopt;
        }
        return host == "localhost" ? std::string() : host;
    }
    if (const std::size_t at = authority.rfind('@'); at != std::string_view::npos) {
        authority.remove_prefix(at + 1);
    }
    std::string_view host = authority;
    std::string_view port;
    bool has_port = false;
    if (host.starts_with('[')) {
        const std::size_t close = host.find(']');
        if (close == std::string_view::npos) {
            return std::nullopt;
        }
        std::string_view after = host.substr(close + 1);
        host = host.substr(0, close + 1);
        if (!after.empty()) {
            if (after.front() != ':') {
                return std::nullopt;
            }
            has_port = true;
            port = after.substr(1);
        }
    } else if (const std::size_t pc = host.find(':'); pc != std::string_view::npos) {
        has_port = true;
        port = host.substr(pc + 1);
        host = host.substr(0, pc);
    }
    if (has_port) {
        if (!all_digits(port)) {
            return std::nullopt;
        }
        constexpr std::uint64_t kMaxPort = 65535;
        std::uint64_t n = 0;
        for (const char c : port) {
            n = std::min<std::uint64_t>((n * 10) + static_cast<std::uint64_t>(c - '0'), kMaxPort + 1);
        }
        if (n > kMaxPort) {
            return std::nullopt;
        }
    }
    if (host.starts_with('[')) {
        if (!valid_ipv6(host.substr(1, host.size() - 2))) {
            return std::nullopt;
        }
        return std::string(); // una IPv6: no es YouTube (su texto da igual aquí)
    }
    if (!special) { // host opaco: tal cual
        if (host.find_first_of(kForbiddenHost) != std::string_view::npos) {
            return std::nullopt;
        }
        return std::string(host);
    }
    if (host.empty()) {
        return std::nullopt;
    }
    const std::string domain = lower_ascii(percent_decode(host));
    constexpr unsigned char kSpace = 0x20;
    constexpr unsigned char kDel = 0x7F;
    for (const char c : domain) {
        const auto u = static_cast<unsigned char>(c);
        if (u < kSpace || u == kDel || c == '%' || kForbiddenHost.find(c) != std::string_view::npos) {
            return std::nullopt;
        }
    }
    if (ends_in_number(domain)) {
        if (!valid_ipv4(domain)) {
            return std::nullopt;
        }
        return std::string(); // una IPv4
    }
    return domain;
}

} // namespace

Json yt_video_qualities(const Json& result) {
    const Json list = result.is_object() && result.value("video", Json()).is_object()
                          ? result["video"].value("qualities", Json())
                          : Json();
    std::vector<Json> out;
    if (list.is_array()) {
        for (const Json& x : list) {
            if (x.is_object() && js_number(x.value("quality", Json())) > 0) {
                out.push_back(x);
            }
        }
    }
    std::ranges::stable_sort(out, [](const Json& a, const Json& b) {
        return js_number(a.value("quality", Json())) > js_number(b.value("quality", Json()));
    });
    return out;
}

std::string yt_quality_short(const Json& quality) {
    const double q = js_number(quality);
    if (q >= 4320) {
        return "8K";
    }
    if (q >= 2160) {
        return "4K";
    }
    return (quality.is_string() ? quality.get<std::string>() : js_num_text(q)) + "p";
}

std::string yt_quality_tag(const Json& quality) {
    // TAGS[q]: la clave es el texto del número.
    const std::string key = quality.is_string() ? quality.get<std::string>() : quality.is_number() ? js_num_text(quality.get<double>()) : std::string();
    if (key == "4320") {
        return "8K";
    }
    if (key == "2160") {
        return "4K";
    }
    if (key == "1440") {
        return "2K";
    }
    if (key == "1080") {
        return "Full HD";
    }
    return key == "720" ? "HD" : "";
}

std::string yt_fmt_bytes(const Json& bytes) {
    const double raw = js_number(bytes);
    const double b = std::isnan(raw) ? 0 : raw;
    if (b <= 0) {
        return {};
    }
    constexpr double kGb = 1024.0 * 1024.0 * 1024.0;
    if (b >= kGb) {
        return js_to_fixed(b / kGb, 1) + " GB";
    }
    return js_num_text(std::max(1.0, std::floor((b / (1024.0 * 1024.0)) + 0.5))) + " MB";
}

std::string yt_quality_meta(const Json& item) {
    std::vector<std::string> parts;
    const Json fps = item.is_object() ? item.value("fps", Json()) : Json();
    if (fps.is_number() && fps.get<double>() != 0 && fps.get<double>() > 30) {
        parts.push_back(js_num_text(fps.get<double>()) + " fps");
    }
    if (const std::string size = yt_fmt_bytes(item.is_object() ? item.value("size", Json()) : Json()); !size.empty()) {
        parts.push_back("≈ " + size);
    }
    std::string out;
    for (const std::string& p : parts) {
        out += (out.empty() ? "" : " · ") + p;
    }
    return out;
}

Json yt_normalize_history(const Json& raw) {
    Json out = Json::array();
    if (raw.is_array()) {
        for (const Json& x : raw) {
            if (x.is_object() && !trimmed(field_or_empty(x, "url")).empty()) {
                out.push_back(x);
            }
        }
    }
    return out;
}

Json yt_history_without(const Json& history, const Json& item) {
    const auto key_of = [](const Json& x) {
        const Json id = field_or_empty(x, "video_id");
        return trimmed(id.is_string() && id.get_ref<const std::string&>().empty() ? field_or_empty(x, "url") : id);
    };
    const std::string needle = key_of(item);
    const std::string url = trimmed(field_or_empty(item, "url"));
    Json out = Json::array();
    for (const Json& x : history.is_array() ? history : Json::array()) {
        if (key_of(x) != needle && trimmed(field_or_empty(x, "url")) != url) {
            out.push_back(x);
        }
    }
    return out;
}

bool yt_is_url(std::string_view u) {
    const std::string_view s = js_trim(u);
    if (s.empty()) {
        return false;
    }
    std::size_t i = 0;
    while (i < s.size() && ascii_alpha(s[i])) {
        ++i;
    }
    const bool has_scheme = i > 0 && s.substr(i).starts_with("://");
    const auto host = url_hostname(has_scheme ? std::string(s) : "https://" + std::string(s));
    if (!host) { // /youtu\.be|youtube\.com/i
        const std::string low = lower_ascii(std::string(s));
        return low.find("youtu.be") != std::string::npos || low.find("youtube.com") != std::string::npos;
    }
    std::string h = lower_ascii(*host);
    if (h.starts_with("www.")) {
        h.erase(0, 4);
    }
    return h == "youtube.com" || h == "youtu.be" || h == "m.youtube.com" || h.ends_with(".youtube.com");
}

bool yt_can_upgrade_from(const Json& item) {
    if (!item.is_object()) {
        return false;
    }
    const Json* scope = member(&item, "scope");
    if (scope != nullptr && scope->is_string() && scope->get_ref<const std::string&>() == "library") {
        return false;
    }
    if (const Json* in = member(&item, "in_point"); in != nullptr && !in->is_null()) {
        return false;
    }
    const Json* provider = member(&item, "provider");
    if (provider != nullptr && provider->is_string() && provider->get_ref<const std::string&>() == "youtube") {
        return true;
    }
    const Json* url = member(&item, "source_url");
    return js_truthy(url) && url->is_string() && yt_is_url(url->get_ref<const std::string&>());
}

std::string yt_material_ident(const Json& item) {
    const Json* index = member(&item, "index");
    return js_string(index != nullptr && !index->is_null() ? index : member(&item, "id"));
}

Json yt_used_upgrades(const Json& list) {
    Json out = Json::array();
    if (!list.is_array()) {
        return out;
    }
    for (const Json& v : list) {
        const Json* compatible = member(&v, "compatible");
        const Json* uses = member(&v, "timeline_uses");
        if (v.is_object() && (compatible == nullptr || !compatible->is_boolean() || compatible->get<bool>()) &&
            uses != nullptr && js_number(*uses) > 0) {
            out.push_back(v);
        }
    }
    return out;
}

std::string yt_upgrade_message(const Json& versions, const Json& new_quality) {
    const Json list = yt_used_upgrades(versions);
    double total = 0;
    for (const Json& v : list) {
        total += js_number(v["timeline_uses"]);
    }
    const Json total_json(total);
    const std::string clips = js_string(&total_json) + " clip" + (total == 1 ? "" : "s");
    const std::string target = js_truthy(&new_quality) ? "la versión " + yt_quality_short(new_quality) : "esta versión";
    std::string head;
    if (list.size() == 1) {
        const Json* quality = member(&list[0], "quality");
        head = "La timeline usa «" + js_string(member(&list[0], "label")) + "»" +
               (js_truthy(quality) ? " en " + js_string(quality) + "p" : "") + " en " + clips + ".";
    } else {
        head = std::format("La timeline usa {} versiones de menos calidad de este vídeo en {}.", list.size(), clips);
    }
    return head + " ¿Cambiarlos por " + target +
           "? Se conservan los cortes, el encuadre y las animaciones (se puede deshacer).";
}

Json yt_check_upgrade(const Json& item, const Json& info) {
    const Json* upgradable = member(&info, "upgradable");
    if (!yt_used_upgrades(upgradable != nullptr ? *upgradable : Json()).empty()) {
        Json ask = Json::object();
        ask["index"] = yt_material_ident(item);
        if (const Json* q = member(member(&info, "material"), "quality")) { // info.material?.quality
            ask["quality"] = *q;
        }
        ask["versions"] = *upgradable;
        return {{"ask", std::move(ask)}, {"toast", nullptr}};
    }
    const Json* blocked = nullptr;
    if (const Json* versions = member(&info, "versions"); versions != nullptr && versions->is_array()) {
        for (const Json& v : *versions) {
            const Json* uses = member(&v, "timeline_uses"); // v.timeline_uses > 0: null y undefined, no
            if (uses != nullptr && !uses->is_null() && js_number(*uses) > 0 && !js_truthy(member(&v, "compatible"))) {
                blocked = &v;
                break;
            }
        }
    }
    const std::string message = blocked != nullptr ? "«" + js_string(member(blocked, "label")) +
                                                         "» no se puede cambiar: " + js_string(member(blocked, "reason")) + "."
                                                   : "La timeline no usa versiones de menos calidad de este vídeo.";
    return {{"ask", nullptr}, {"toast", {{"type", "info"}, {"message", message}}}};
}

Json yt_upgrade_olds(const Json& ask) {
    const Json* versions = member(&ask, "versions");
    Json out = Json::array();
    for (const Json& v : yt_used_upgrades(versions != nullptr ? *versions : Json())) {
        out.push_back(js_string(member(&v, "index")));
    }
    return out;
}

Json yt_upgrade_done_toast(const Json& ask, const Json& res) {
    const Json* quality = member(&ask, "quality");
    const std::string q = js_truthy(quality) ? " a " + yt_quality_short(*quality) : "";
    bool bg = false;
    if (const Json* warnings = member(&res, "warnings"); warnings != nullptr && warnings->is_array()) {
        for (const Json& w : *warnings) {
            bg = bg || (w.is_string() && w.get_ref<const std::string&>().find("Quitar fondo") != std::string::npos);
        }
    }
    const Json* replaced = member(&res, "replaced");
    const bool one = replaced != nullptr && replaced->is_number() && replaced->get<double>() == 1;
    const std::string message = js_string(replaced) + " clip" + (one ? "" : "s") + " pasado" + (one ? "" : "s") + q +
                                "." + (bg ? " Vuelve a generar «Quitar fondo» en los que lo usaban." : "");
    return {{"type", "success"}, {"message", message}};
}

} // namespace gc::core
