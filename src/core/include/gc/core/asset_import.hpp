#pragma once

// Importar recursos de bancos de stock (Pexels, GIPHY, Pixabay, Unsplash, Vimeo), espejo de
// asset_import.py de video-yt. Por ahora, solo la parte sin red: qué URLs se pueden descargar.

#include <string_view>

#include "gc/core/error.hpp"

namespace gc::core {

// allowed_download_url: http(s) con host (sin los «w» y «.» del principio, el lstrip("www.") de
// Python) en pexels.com, giphy.com, vimeocdn.com, vimeo.com, pixabay.com o unsplash.com, o un
// subdominio. Error si urlparse no acepta la URL (el ValueError de Python).
[[nodiscard]] Result<bool> allowed_download_url(std::string_view url);

} // namespace gc::core
