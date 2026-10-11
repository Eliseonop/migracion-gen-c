#include "gc/core/image_probe.hpp"

#include <algorithm>
#include <cstddef>
#include <string_view>

#include "py.hpp"

namespace gc::core {

using detail::py_round;

namespace {

using Bytes = std::span<const unsigned char>;

// `data[a:a + len(lit)] == lit`: un corte de Python más allá del final es más corto, no falla.
bool slice_is(Bytes data, std::size_t a, std::string_view lit) {
    if (a > data.size() || data.size() - a < lit.size()) {
        return false;
    }
    return std::ranges::equal(data.subspan(a, lit.size()), lit,
                              [](unsigned char x, char y) { return x == static_cast<unsigned char>(y); });
}

// `int.from_bytes(data[a:b], "little")` con el corte acotado al final.
unsigned le(Bytes data, std::size_t a, std::size_t b) {
    unsigned v = 0;
    const std::size_t end = std::min(b, data.size());
    for (std::size_t i = end; i > a; --i) {
        v = (v << 8U) | data[i - 1];
    }
    return v;
}

// Tamaño de una tabla de color: 3 · 2^((packed & 7) + 1).
std::size_t color_table(unsigned char packed) { return std::size_t{3} << ((packed & 0x07U) + 1U); }

// _skip_subblocks: salta una cadena de sub-bloques (tamaño + datos, terminada en 0).
std::size_t skip_subblocks(Bytes data, std::size_t pos) {
    while (pos < data.size()) {
        const unsigned char size = data[pos];
        ++pos;
        if (size == 0) {
            break;
        }
        pos += size;
    }
    return pos;
}

} // namespace

GifInfo probe_gif(Bytes data) {
    GifInfo out;
    if (data.size() < 13 || !(slice_is(data, 0, "GIF87a") || slice_is(data, 0, "GIF89a"))) {
        return out;
    }
    out.width = static_cast<int>(le(data, 6, 8));
    out.height = static_cast<int>(le(data, 8, 10));
    // Un `data[i]` fuera de rango es un IndexError que el `except` de Python recoge: se devuelve
    // `out` tal como está (las dimensiones y lo demás por defecto, que se rellena al final).
    const unsigned char packed = data[10];
    std::size_t pos = 13;
    if ((packed & 0x80U) != 0) {
        pos += color_table(packed);
    }
    int frames = 0;
    double total_delay = 0.0;
    double pending_delay = 0.1;
    bool has_alpha = false;
    bool loop = false;
    bool netscape = false;
    const std::size_t n = data.size();
    while (pos < n) {
        const unsigned char b = data[pos];
        if (b == 0x3B) { // trailer
            break;
        }
        if (b == 0x21) { // extensión
            if (pos + 1 >= n) {
                return out;
            }
            const unsigned char label = data[pos + 1];
            if (label == 0xF9 && pos + 6 < n) { // Graphic Control Extension
                const unsigned delay_cs = le(data, pos + 4, pos + 6);
                pending_delay = delay_cs > 0 ? static_cast<double>(delay_cs) / 100.0 : 0.1;
                if ((data[pos + 3] & 0x01U) != 0) {
                    has_alpha = true;
                }
            } else if (label == 0xFF && slice_is(data, pos + 2, "\x0bNETSCAPE2.0")) {
                const std::size_t sb = pos + 14;
                if (slice_is(data, sb, "\x03\x01")) {
                    netscape = true;
                    loop = le(data, sb + 2, sb + 4) == 0;
                }
            }
            pos = skip_subblocks(data, pos + 2);
            continue;
        }
        if (b == 0x2C) { // descriptor de imagen: un fotograma
            ++frames;
            total_delay += pending_delay;
            pending_delay = 0.1;
            if (pos + 9 >= n) {
                return out;
            }
            const unsigned char img_packed = data[pos + 9];
            pos += 10;
            if ((img_packed & 0x80U) != 0) {
                pos += color_table(img_packed);
            }
            pos += 1; // tamaño mínimo de código LZW
            pos = skip_subblocks(data, pos);
            continue;
        }
        pos += 1; // byte inesperado
    }
    out.frames = std::max(1, frames);
    out.animated = frames > 1;
    out.duration = frames != 0 ? py_round(total_delay, 3) : 0.0;
    out.fps = total_delay > 0 ? py_round(static_cast<double>(frames) / total_delay, 3) : 0.0;
    out.has_alpha = has_alpha;
    out.loop = netscape ? loop : frames > 1;
    return out;
}

std::string_view image_ext_from_magic(Bytes data) {
    if (slice_is(data, 0, "\x89PNG\r\n\x1a\n")) {
        return ".png";
    }
    if (slice_is(data, 0, "\xff\xd8\xff")) {
        return ".jpg";
    }
    if (slice_is(data, 0, "GIF87a") || slice_is(data, 0, "GIF89a")) {
        return ".gif";
    }
    if (data.size() >= 12 && slice_is(data, 0, "RIFF") && slice_is(data, 8, "WEBP")) {
        return ".webp";
    }
    return "";
}

} // namespace gc::core
