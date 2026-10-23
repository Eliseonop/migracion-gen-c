#include "gc/core/youtube_url.hpp"

#include <vector>

#include "py.hpp"
#include "py_url.hpp"

namespace gc::core {

using detail::py_parse_qsl;
using detail::py_strip;
using detail::py_url_hostname;
using detail::py_urlparse;

namespace {

// (url or "").strip() y, sin «://», https:// delante.
std::string with_scheme(std::string_view url) {
    std::string raw = py_strip(url);
    if (!raw.empty() && !raw.contains("://")) {
        raw = "https://" + raw;
    }
    return raw;
}

bool ends_with(std::string_view s, std::string_view tail) { return s.ends_with(tail); }

} // namespace

bool is_youtube_url(std::string_view url) {
    const std::string raw = with_scheme(url);
    if (raw.empty()) {
        return false;
    }
    const auto parts = py_urlparse(raw);
    if (!parts) {
        return false;
    }
    std::string host = py_url_hostname(parts->netloc).value_or(""); // .lower() ya hecho
    if (host.starts_with("www.")) {
        host = host.substr(4);
    }
    return host == "youtube.com" || host == "youtu.be" || host == "m.youtube.com" || ends_with(host, ".youtube.com");
}

std::optional<std::string> youtube_id_from_url(std::string_view url) {
    const std::string raw = with_scheme(url);
    if (raw.empty()) {
        return std::nullopt;
    }
    const auto parts = py_urlparse(raw);
    if (!parts) {
        return std::nullopt;
    }
    const std::string host = py_url_hostname(parts->netloc).value_or("");
    if (ends_with(host, "youtu.be")) {
        // parsed.path.strip("/").split("/")[0]
        std::string_view path = parts->path;
        while (path.starts_with('/')) {
            path.remove_prefix(1);
        }
        while (path.ends_with('/')) {
            path.remove_suffix(1);
        }
        const std::string vid(path.substr(0, path.find('/')));
        return vid.empty() ? std::nullopt : std::optional<std::string>(vid);
    }
    for (const auto& [name, value] : py_parse_qsl(parts->query)) {
        if (name == "v") { // qs["v"][0]: el primero; parse_qsl ya quita los vacíos
            return value;
        }
    }
    std::vector<std::string_view> segs;
    const std::string_view p = parts->path;
    std::size_t start = 0;
    while (start <= p.size()) {
        const auto i = p.find('/', start);
        const auto seg = p.substr(start, i == std::string_view::npos ? std::string_view::npos : i - start);
        if (!seg.empty()) {
            segs.push_back(seg);
        }
        if (i == std::string_view::npos) {
            break;
        }
        start = i + 1;
    }
    if (segs.size() >= 2 && (segs[0] == "shorts" || segs[0] == "live" || segs[0] == "embed")) {
        return std::string(segs[1]);
    }
    return std::nullopt;
}

} // namespace gc::core
