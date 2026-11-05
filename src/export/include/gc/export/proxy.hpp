#pragma once

// Los proxies de la vista previa: una copia ligera de un vídeo grande (1440p, 4K) para que el editor
// no tenga que decodificar el original en cada fotograma. H.264 con el lado corto a 960 px (lo que la
// vista previa a ½ necesita de un vídeo horizontal encuadrado en vertical), a los fps de la timeline
// y con el mismo `fps=F` que aplica el export al leer el vídeo (con start_time=0): su fotograma k es
// el fotograma k que el export saca del original, así que la vista previa lo cambia sin moverse en el
// tiempo. Solo la vista previa los usa; el export, nunca.
//
// Van en una carpeta de caché de la app, no en la del proyecto: <caché>/<clave>.mp4 y <clave>.json,
// con la clave sacada del original (ruta, tamaño y fecha) y de los fps. El vídeo se escribe en
// <clave>.part.mp4 y se renombra al acabar: uno a medias nunca se usa.

#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <utility>

#include "gc/core/error.hpp"

namespace gc::exporter {

inline constexpr int kProxyShortSide = 960;      // el lado corto del proxy
inline constexpr int kProxyFromShortSide = 1200; // el lado corto del original a partir del cual se hace

struct ProxyFile {
    std::filesystem::path path; // el .mp4
    int source_width = 0;       // el original como lo decodifica el export (con el autogiro)
    int source_height = 0;
};

// El material (la ruta del archivo original) → su proxy.
using ProxyMap = std::map<std::filesystem::path, ProxyFile>;

enum class ProxyState {
    Missing,   // no hay (o es de otra versión del archivo): hay que hacerlo
    NotNeeded, // no hace falta: un vídeo pequeño, con alfa, sin fotogramas…
    Ready,
};

struct ProxyLookup {
    ProxyState state = ProxyState::Missing;
    ProxyFile file; // con Ready
};

// Las dimensiones del proxy de un original de width × height: el lado corto a kProxyShortSide y el
// otro en proporción, pares. nullopt si el lado corto no pasa de kProxyFromShortSide.
[[nodiscard]] std::optional<std::pair<int, int>> proxy_size(int width, int height);

// Lo que hay en la caché para `source` a `fps`: el proxy hecho (si sigue siendo del archivo tal como
// está), la marca de que no hace falta, o nada.
[[nodiscard]] ProxyLookup find_proxy(const std::filesystem::path& cache_dir, const std::filesystem::path& source, int fps);

// Hace el proxy (o la marca de que no hace falta) y lo deja en la caché. `progress` recibe la parte
// hecha (0–1); si devuelve false, se para sin dejar nada y devuelve Missing. Error si el original no
// se puede leer o el proxy no se puede escribir.
[[nodiscard]] Result<ProxyLookup> make_proxy(const std::filesystem::path& cache_dir, const std::filesystem::path& source,
                                             int fps, const std::function<bool(double)>& progress = {});

} // namespace gc::exporter
