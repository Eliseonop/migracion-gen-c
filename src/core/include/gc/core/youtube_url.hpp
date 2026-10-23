#pragma once

// Enlaces de YouTube, espejo de is_youtube_url y youtube_id_from_url de youtube_audio.py
// (video-yt), con el urlparse y el parse_qs de Python 3.14.

#include <optional>
#include <string>
#include <string_view>

namespace gc::core {

// El host (sin «www.») es youtube.com, youtu.be, m.youtube.com o un subdominio de
// youtube.com. Sin «://» se lee como https. Una URL que urlparse no acepta no lo es.
[[nodiscard]] bool is_youtube_url(std::string_view url);

// El id del vídeo: la primera parte de la ruta en youtu.be, el primer «v» no vacío de la
// consulta, o lo que sigue a shorts/, live/ o embed/. nullopt si no hay.
[[nodiscard]] std::optional<std::string> youtube_id_from_url(std::string_view url);

} // namespace gc::core
