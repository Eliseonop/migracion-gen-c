#pragma once

// Lo que video-yt usa de urllib.parse (Python 3.14) para leer URLs: urlparse, el hostname
// del resultado, parse_qsl y unquote. Todo sobre texto UTF-8 válido.

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace gc::core::detail {

// Las seis partes de urlparse(url) (scheme en minúsculas; vacías si no están).
struct UrlParts {
    std::string scheme;
    std::string netloc;
    std::string path;
    std::string params;
    std::string query;
    std::string fragment;
};

// urlparse(url). nullopt = el ValueError de Python (corchetes de IPv6 mal puestos o con
// algo que no es una IP, o un netloc que con NFKC da «/?#@:»).
[[nodiscard]] std::optional<UrlParts> py_urlparse(std::string_view url);

// .hostname de un netloc: sin usuario ni puerto, en minúsculas (salvo la zona «%…» de un
// IPv6). nullopt = None (sin host).
[[nodiscard]] std::optional<std::string> py_url_hostname(std::string_view netloc);

// parse_qsl(qs) con los valores por defecto: separador «&», sin los valores vacíos y con
// nombre y valor pasados por unquote_plus.
[[nodiscard]] std::vector<std::pair<std::string, std::string>> py_parse_qsl(std::string_view qs);

// unquote(s) (UTF-8 con errors="replace") y unquote_plus(s) («+» → espacio antes).
[[nodiscard]] std::string py_unquote(std::string_view s);
[[nodiscard]] std::string py_unquote_plus(std::string_view s);

// bytes.decode("utf-8", "replace") de CPython: un U+FFFD por cada trozo inválido más largo
// posible (la «maximal subpart» de Unicode).
[[nodiscard]] std::string py_utf8_decode_replace(std::string_view bytes);

// Los caracteres no ASCII cuyo NFKC lleva alguno de «/?#@:» (los que hacen fallar a
// _checknetloc), ordenados. Generados con unicodedata del Python de video-yt.
[[nodiscard]] std::span<const char32_t> nfkc_url_specials();

} // namespace gc::core::detail
