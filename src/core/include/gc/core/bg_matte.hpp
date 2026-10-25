#pragma once

// El matte de «Eliminar fondo» leído de la caché de video-yt (bg/service.py y clip_bg.py).
//
// La caché tiene dos niveles: el matte crudo del modelo (nivel 1, `matte/<base_key>/000001.png…`, en
// gris, con su meta.json) y el derivado con los ajustes del clip (nivel 2, `mask/<derive_key>`). El
// export usa el derivado de cada clip (build_clip_bg_mask → ensure_derived) como una secuencia PNG
// que entra en el grafo de FFmpeg con `-framerate` y `-start_number`. Aquí no se lee ni se escribe
// el nivel 2: el derivado es la LUT de umbral, suavidad e invertir (matte_lut) sobre el crudo, y se
// calcula al vuelo. Lo que también cambia el derivado (pluma, expansión, opacidad y el pincel) aún
// no se aplica (`lut_only` falso).

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "gc/core/error.hpp"
#include "gc/core/json.hpp"
#include "gc/core/model.hpp"

namespace gc::core {

// Lo que build_clip_bg_mask da de un clip (con el nivel 2 vacío, la primera vez).
struct MatteSpec {
    std::filesystem::path folder; // <caché>/matte/<base_key>
    std::int64_t start_number = 1; // el primer fotograma del tramo más 1 (el `-start_number`)
    std::int64_t frames = 0;       // los que pide el tramo (i1 − i0 + 1)
    std::int64_t mask_fps = 15;    // el `-framerate`
    bool loop = false;             // un GIF: la secuencia da la vuelta con él (`-stream_loop -1`)
    double duration = 0;           // round(duración de la fuente, 3)
    std::int64_t width = 0;        // las del meta.json (las del matte crudo)
    std::int64_t height = 0;
    bool lut_only = true;          // el derivado es solo la LUT (sin pluma, expansión, opacidad ni pincel)
    std::array<std::uint8_t, 256> lut{}; // matte_lut
    Json settings;                 // el `auto` normalizado, para derive_matte cuando no es solo la LUT
    // De qué fotograma crudo (índice desde 0) sale cada fotograma del tramo: fuera del rango que
    // hay, el del extremo. Nada si ese derivado no existe (su crudo falta): la secuencia PNG del
    // export se corta ahí.
    std::vector<std::optional<std::int64_t>> sources;
};

// build_clip_bg_mask de bg/service.py con `cache_root` como `data/bgcache`. Nada si el clip no
// admite eliminar fondo, si su matte automático no está listo (`auto_active`) o si la caché no
// sirve (sin meta.json o sin un rango de dos). Un error donde Python lanza: un int() o un float()
// que falla con lo del meta, o un derivado que habría que copiar de un extremo que no existe (el
// clip cae fuera de lo que se procesó). El export, con un error, sigue sin el matte.
[[nodiscard]] Result<std::optional<MatteSpec>> clip_matte_spec(const TimelineClip& clip,
                                                               const std::filesystem::path& cache_root);

// La rama del matte de _bg_source_chain (compose.py) para un `bg` normalizado con el matte listo,
// sobre un material de iw×ih (detect.dims): lo que va tras la cadena del clip, que entra sin
// etiqueta, y con `[matte]` como la segunda entrada (la secuencia PNG). Sin croma, el material a
// `scale=iw:ih,format=gbrap,fps=F` y `alphamerge` con el matte, subido al tamaño del material con
// bicúbico; con croma, su alfa (con el difuminado o la expansión del borde) se multiplica por el
// del matte (`blend=all_mode=multiply`) y entra una sola vez. Después, el contorno, si lo hay
// (outline_ffmpeg_steps, con `outline_height`: el alto de detect.dims, 0 si no se supo, aunque iw×ih
// salga del meta.json). Empieza por «,» para ir detrás de la cadena.
[[nodiscard]] std::string matte_source_chain(const Json& normalized_bg, std::int64_t iw, std::int64_t ih,
                                             std::int64_t outline_height, int fps);

// derive_matte de clip_bg.py: un fotograma crudo del matte (gris de 8 bits, width × height, sin
// relleno) → el derivado, con la LUT, la expansión, la pluma, el pincel y la opacidad del `auto`
// normalizado, en float32 como numpy y OpenCV. Sin nada de eso, es la LUT (exacto); con pluma o
// expansión, el GaussianBlur y los resize de OpenCV (casi exactos); los trazos del pincel se
// parecen a los de cv2.polylines, pero no son iguales.
[[nodiscard]] std::vector<std::uint8_t> derive_matte(std::span<const std::uint8_t> raw, int width, int height,
                                                     const Json& normalized_auto);

// matte_lut de clip_bg.py sobre un `auto` normalizado (normalize_bg(...)["auto"]): la rampa de
// umbral y suavidad, invertida si toca, con la aritmética de doble del JS.
[[nodiscard]] std::array<std::uint8_t, 256> matte_lut(const Json& normalized_auto);

// matte_frame_index de clip_bg.py: el fotograma del matte para un instante de la fuente.
[[nodiscard]] std::int64_t matte_frame_index(double src_t, std::int64_t mask_fps);

// El archivo de un fotograma de la caché: «000001.png» para el índice 0.
[[nodiscard]] std::string matte_frame_name(std::int64_t index);

} // namespace gc::core
