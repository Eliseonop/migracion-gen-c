#pragma once

// Cabeceras de imagen leídas a mano, sin decodificar (espejo de images.py: probe_gif y
// _ext_from_magic). Lo que usa «Importar imagen» para decidir la extensión y si un GIF es
// un elemento animado con duración.

#include <optional>
#include <span>
#include <string_view>

namespace gc::core {

// Metadatos de un GIF (probe_gif): fotogramas, suma de retardos (0,1 s si un fotograma no
// trae retardo o es 0), dimensiones, transparencia y si repite (NETSCAPE2.0 con contador 0;
// sin NETSCAPE, un GIF animado repite).
struct GifInfo {
    bool animated = false;
    int frames = 1;
    double duration = 0.0; // s, a 3 decimales
    double fps = 0.0;      // a 3 decimales
    std::optional<int> width;
    std::optional<int> height;
    bool loop = false;
    bool has_alpha = false;
};

// Como Python: si los datos no empiezan por GIF87a/GIF89a (o tienen menos de 13 bytes), los
// valores por defecto; si el archivo está cortado en mitad de un bloque, solo las dimensiones
// (el IndexError lo recoge el `except`).
[[nodiscard]] GifInfo probe_gif(std::span<const unsigned char> data);

// Extensión por la firma: ".png", ".jpg", ".gif", ".webp" o "" si no la conoce.
[[nodiscard]] std::string_view image_ext_from_magic(std::span<const unsigned char> data);

} // namespace gc::core
