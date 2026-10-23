#include "gc/core/asset_import.hpp"

#include <algorithm>
#include <array>
#include <string>

#include "py.hpp"
#include "py_url.hpp"

namespace gc::core {

namespace {

constexpr std::array<std::string_view, 6> kAllowedSuffixes{"pexels.com",  "giphy.com",   "vimeocdn.com",
                                                           "vimeo.com",   "pixabay.com", "unsplash.com"};

} // namespace

Result<bool> allowed_download_url(std::string_view url) {
    const auto parsed = detail::py_urlparse(detail::py_strip(url));
    if (!parsed) {
        return fail(ErrorCode::InvalidArgument, "URL no válida");
    }
    if ((parsed->scheme != "http" && parsed->scheme != "https") || parsed->netloc.empty()) {
        return false;
    }
    // host.lower().lstrip("www."): quita todos los «w» y «.» del principio.
    const std::string host = detail::py_lower(detail::py_url_hostname(parsed->netloc).value_or(""));
    const std::string_view h = std::string_view(host).substr(std::min(host.find_first_not_of("w."), host.size()));
    return std::ranges::any_of(kAllowedSuffixes, [&](std::string_view d) {
        return h == d || (h.size() > d.size() && h.ends_with(d) && h[h.size() - d.size() - 1] == '.');
    });
}

} // namespace gc::core
