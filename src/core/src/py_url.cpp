#include "py_url.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>

#include "py.hpp"

namespace gc::core::detail {

namespace {

constexpr std::array<char32_t, 19> kNfkcSpecials{0x2047, 0x2048, 0x2049, 0x2100, 0x2101, 0x2105, 0x2106,
                                                 0x2A74, 0xFE13, 0xFE16, 0xFE55, 0xFE56, 0xFE5F, 0xFE6B,
                                                 0xFF03, 0xFF0F, 0xFF1A, 0xFF1F, 0xFF20};

constexpr std::array<std::string_view, 16> kUsesParams{"",    "ftp",  "hdl",   "prospero", "http", "imap",
                                                       "https", "shttp", "rtsp", "rtsps",    "rtspu", "sip",
                                                       "sips", "mms",  "sftp",  "tel"};

bool is_ascii_alpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool is_ascii_digit(char c) { return c >= '0' && c <= '9'; }
bool is_hex(char c) { return is_ascii_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
bool is_scheme_char(char c) { return is_ascii_alpha(c) || is_ascii_digit(c) || c == '+' || c == '-' || c == '.'; }

unsigned hex_value(char c) {
    if (is_ascii_digit(c)) {
        return static_cast<unsigned>(c - '0');
    }
    return static_cast<unsigned>((c >= 'a' ? c - 'a' : c - 'A') + 10);
}

bool all_ascii(std::string_view s) {
    return std::ranges::all_of(s, [](char c) { return static_cast<unsigned char>(c) < 0x80; });
}

struct Partition {
    std::string_view before;
    bool found = false;
    std::string_view after;
};

Partition partition(std::string_view s, char sep) {
    const auto i = s.find(sep);
    if (i == std::string_view::npos) {
        return {s, false, {}};
    }
    return {s.substr(0, i), true, s.substr(i + 1)};
}

std::string_view rpartition_after(std::string_view s, char sep) {
    const auto i = s.rfind(sep);
    return i == std::string_view::npos ? s : s.substr(i + 1);
}

std::vector<std::string_view> split(std::string_view s, char sep, std::size_t maxsplit = SIZE_MAX) {
    std::vector<std::string_view> out;
    std::size_t start = 0;
    while (out.size() < maxsplit) {
        const auto i = s.find(sep, start);
        if (i == std::string_view::npos) {
            break;
        }
        out.push_back(s.substr(start, i - start));
        start = i + 1;
    }
    out.push_back(s.substr(start));
    return out;
}

// IPv4Address(str): cuatro octetos decimales ASCII, sin ceros delante, ≤ 255.
std::optional<std::uint32_t> parse_ipv4(std::string_view s) {
    if (s.empty() || s.contains('/')) {
        return std::nullopt;
    }
    const auto octets = split(s, '.');
    if (octets.size() != 4) {
        return std::nullopt;
    }
    std::uint32_t ip = 0;
    for (const std::string_view o : octets) {
        if (o.empty() || !std::ranges::all_of(o, is_ascii_digit) || o.size() > 3 || (o != "0" && o[0] == '0')) {
            return std::nullopt;
        }
        int v = 0;
        for (const char c : o) {
            v = (v * 10) + (c - '0');
        }
        if (v > 255) {
            return std::nullopt;
        }
        ip = (ip << 8U) | static_cast<std::uint32_t>(v);
    }
    return ip;
}

bool valid_hextet(std::string_view h) { return !h.empty() && h.size() <= 4 && std::ranges::all_of(h, is_hex); }

// IPv6Address(str): con zona «%…» opcional, «::» como mucho una vez y un IPv4 al final.
bool valid_ipv6(std::string_view s) {
    if (s.contains('/')) {
        return false;
    }
    const auto [addr, has_scope, scope] = partition(s, '%');
    if (has_scope && (scope.empty() || scope.contains('%'))) {
        return false;
    }
    if (addr.empty() || py_len(addr) > 45) {
        return false;
    }
    constexpr std::size_t kHextets = 8;
    constexpr std::size_t kMaxParts = kHextets + 1;
    std::vector<std::string> parts;
    for (const std::string_view p : split(addr, ':', kMaxParts)) {
        parts.emplace_back(p);
    }
    if (parts.size() < 3) {
        return false;
    }
    if (parts.back().contains('.')) {
        const auto v4 = parse_ipv4(parts.back());
        if (!v4) {
            return false;
        }
        parts.pop_back();
        parts.push_back(std::format("{:x}", (*v4 >> 16U) & 0xFFFFU));
        parts.push_back(std::format("{:x}", *v4 & 0xFFFFU));
    }
    if (parts.size() > kMaxParts) {
        return false;
    }
    std::optional<std::size_t> skip;
    for (std::size_t i = 1; i + 1 < parts.size(); ++i) {
        if (parts[i].empty()) {
            if (skip) {
                return false;
            }
            skip = i;
        }
    }
    std::size_t hi = 0;
    std::size_t lo = 0;
    if (skip) {
        hi = *skip;
        lo = parts.size() - *skip - 1;
        if (parts.front().empty() && --hi != 0) {
            return false;
        }
        if (parts.back().empty() && --lo != 0) {
            return false;
        }
        if (hi + lo >= kHextets) { // parts_skipped < 1
            return false;
        }
    } else {
        if (parts.size() != kHextets || parts.front().empty() || parts.back().empty()) {
            return false;
        }
        hi = parts.size();
    }
    for (std::size_t i = 0; i < hi; ++i) {
        if (!valid_hextet(parts[i])) {
            return false;
        }
    }
    for (std::size_t i = parts.size() - lo; i < parts.size(); ++i) {
        if (!valid_hextet(parts[i])) {
            return false;
        }
    }
    return true;
}

// _check_bracketed_host: un IPvFuture («v1.x») o un IPv6; un IPv4 entre corchetes no vale.
bool valid_bracketed_host(std::string_view h) {
    if (h.starts_with('v')) { // \Av[a-fA-F0-9]+\..+\z
        std::size_t i = 1;
        while (i < h.size() && is_hex(h[i])) {
            ++i;
        }
        return i > 1 && i < h.size() && h[i] == '.' && i + 1 < h.size() && !h.substr(i + 1).contains('\n');
    }
    if (parse_ipv4(h)) {
        return false;
    }
    return valid_ipv6(h);
}

bool valid_bracketed_netloc(std::string_view netloc) {
    const std::string_view hp = rpartition_after(netloc, '@');
    const auto [before, have, bracketed] = partition(hp, '[');
    std::string_view host;
    if (have) {
        if (!before.empty()) {
            return false;
        }
        const auto [h, _, port] = partition(bracketed, ']');
        if (!port.empty() && port[0] != ':') {
            return false;
        }
        host = h;
    } else {
        host = partition(hp, ':').before;
    }
    return valid_bracketed_host(host);
}

// _checknetloc: un netloc no ASCII no puede llevar caracteres que con NFKC den «/?#@:».
bool valid_netloc_nfkc(std::string_view netloc) {
    if (all_ascii(netloc)) {
        return true;
    }
    std::size_t i = 0;
    while (i < netloc.size()) {
        if (std::ranges::binary_search(kNfkcSpecials, utf8_next(netloc, i))) {
            return false;
        }
    }
    return true;
}

// _unquote_impl de un trozo ASCII: «%XX» → el byte; lo demás tal cual.
std::string unquote_bytes(std::string_view s) {
    std::string out;
    const auto bits = split(s, '%');
    out += bits[0];
    for (std::size_t k = 1; k < bits.size(); ++k) {
        const std::string_view item = bits[k];
        if (item.size() >= 2 && is_hex(item[0]) && is_hex(item[1])) {
            out += static_cast<char>((hex_value(item[0]) << 4U) | hex_value(item[1]));
            out += item.substr(2);
        } else {
            out += '%';
            out += item;
        }
    }
    return out;
}

} // namespace

std::span<const char32_t> nfkc_url_specials() { return kNfkcSpecials; }

std::string py_utf8_decode_replace(std::string_view bytes) {
    std::string out;
    const auto at = [&](std::size_t k) { return static_cast<unsigned char>(bytes[k]); };
    std::size_t i = 0;
    while (i < bytes.size()) {
        const unsigned char b = at(i);
        if (b < 0x80) {
            out += static_cast<char>(b);
            ++i;
            continue;
        }
        std::size_t need = 0;
        unsigned char lo = 0x80;
        unsigned char hi = 0xBF;
        if (b >= 0xC2 && b <= 0xDF) {
            need = 1;
        } else if (b >= 0xE0 && b <= 0xEF) {
            need = 2;
            lo = b == 0xE0 ? 0xA0 : 0x80;
            hi = b == 0xED ? 0x9F : 0xBF;
        } else if (b >= 0xF0 && b <= 0xF4) {
            need = 3;
            lo = b == 0xF0 ? 0x90 : 0x80;
            hi = b == 0xF4 ? 0x8F : 0xBF;
        } else {
            out += "\xEF\xBF\xBD";
            ++i;
            continue;
        }
        std::size_t k = 1;
        for (; k <= need && i + k < bytes.size(); ++k) {
            const unsigned char c = at(i + k);
            if (k == 1 ? (c < lo || c > hi) : (c < 0x80 || c > 0xBF)) {
                break;
            }
        }
        if (k <= need) { // trozo inválido o cortado: un U+FFFD por lo válido hasta aquí
            out += "\xEF\xBF\xBD";
            i += k;
            continue;
        }
        out.append(bytes.substr(i, need + 1));
        i += need + 1;
    }
    return out;
}

std::string py_unquote(std::string_view s) {
    if (!s.contains('%')) {
        return std::string(s);
    }
    // Los trozos no ASCII pasan tal cual; los ASCII se descodifican cada uno por su lado.
    std::string out;
    std::size_t i = 0;
    while (i < s.size()) {
        std::size_t j = i;
        const bool ascii = static_cast<unsigned char>(s[i]) < 0x80;
        while (j < s.size() && (static_cast<unsigned char>(s[j]) < 0x80) == ascii) {
            ++j;
        }
        out += ascii ? py_utf8_decode_replace(unquote_bytes(s.substr(i, j - i))) : std::string(s.substr(i, j - i));
        i = j;
    }
    return out;
}

std::string py_unquote_plus(std::string_view s) {
    std::string t(s);
    std::ranges::replace(t, '+', ' ');
    return py_unquote(t);
}

std::vector<std::pair<std::string, std::string>> py_parse_qsl(std::string_view qs) {
    std::vector<std::pair<std::string, std::string>> out;
    if (qs.empty()) {
        return out;
    }
    for (const std::string_view nv : split(qs, '&')) {
        if (nv.empty()) {
            continue;
        }
        const auto [name, _, value] = partition(nv, '=');
        if (!value.empty()) {
            out.emplace_back(py_unquote_plus(name), py_unquote_plus(value));
        }
    }
    return out;
}

std::optional<UrlParts> py_urlparse(std::string_view url) {
    const std::string_view raw = url;
    std::size_t lead = 0;
    while (lead < raw.size() && static_cast<unsigned char>(raw[lead]) <= 0x20) {
        ++lead;
    }
    std::string clean;
    for (const char c : raw.substr(lead)) {
        if (c != '\t' && c != '\r' && c != '\n') {
            clean += c;
        }
    }
    UrlParts p;
    std::string_view rest = clean;
    const auto colon = rest.find(':');
    if (colon != std::string_view::npos && colon > 0 && is_ascii_alpha(rest[0]) &&
        std::ranges::all_of(rest.substr(0, colon), is_scheme_char)) {
        p.scheme = std::string(rest.substr(0, colon));
        ascii_lower_inplace(p.scheme);
        rest = rest.substr(colon + 1);
    }
    if (rest.starts_with("//")) {
        const auto delim = std::min(rest.find_first_of("/?#", 2), rest.size());
        p.netloc = std::string(rest.substr(2, delim - 2));
        rest = rest.substr(delim);
        const bool open = p.netloc.contains('[');
        const bool close = p.netloc.contains(']');
        if (open != close) {
            return std::nullopt; // Invalid IPv6 URL
        }
        if (open && !valid_bracketed_netloc(p.netloc)) {
            return std::nullopt;
        }
    }
    if (const auto h = rest.find('#'); h != std::string_view::npos) {
        p.fragment = std::string(rest.substr(h + 1));
        rest = rest.substr(0, h);
    }
    if (const auto q = rest.find('?'); q != std::string_view::npos) {
        p.query = std::string(rest.substr(q + 1));
        rest = rest.substr(0, q);
    }
    if (!valid_netloc_nfkc(p.netloc)) {
        return std::nullopt;
    }
    if (std::ranges::find(kUsesParams, p.scheme) != kUsesParams.end() && rest.contains(';')) {
        std::size_t i = std::string_view::npos;
        if (const auto slash = rest.rfind('/'); slash != std::string_view::npos) {
            i = rest.find(';', slash);
        } else {
            i = rest.find(';');
        }
        if (i != std::string_view::npos) {
            p.params = std::string(rest.substr(i + 1));
            rest = rest.substr(0, i);
        }
    }
    p.path = std::string(rest);
    return p;
}

std::optional<std::string> py_url_hostname(std::string_view netloc) {
    const std::string_view hp = rpartition_after(netloc, '@');
    const auto [_, have, bracketed] = partition(hp, '[');
    const std::string_view host = have ? partition(bracketed, ']').before : partition(hp, ':').before;
    if (host.empty()) {
        return std::nullopt;
    }
    const auto [name, pct, zone] = partition(host, '%');
    std::string out = py_lower(name);
    if (pct) {
        out += '%';
        out += zone;
    }
    return out;
}

} // namespace gc::core::detail
