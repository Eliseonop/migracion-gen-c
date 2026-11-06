#pragma once

// Decode de un material como lo hace el export de video-yt: la entrada que piden sus
// argumentos de ffmpeg (core::ffmpeg_input_args) y la cadena de filtros que el export le
// aplica antes del encuadre (`[k:v]{color_pre}{trim},setpts=PTS-STARTPTS`, compose.py), a
// fotogramas RGBA. El grafo de filtros es el de libavfilter, así que los fotogramas son los
// mismos que da `ffmpeg <args> -vf "<cadena>,format=rgba" -f rawvideo`, también el autogiro del
// CLI: un vídeo con el giro en el contenedor (de móvil) sale girado, con sus dimensiones giradas.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "gc/core/bg_matte.hpp"
#include "gc/core/error.hpp"
#include "gc/core/model.hpp"

namespace gc::media {

struct RgbaFrame {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> pixels; // width · height · 4, sin relleno entre filas
    bool hardware = false;            // lo decodificó la GPU (DecodeSpec::hwaccel)
};

struct DecodeSpec {
    std::vector<std::string> input_args; // como ffmpeg_input_args: opciones, "-i" y la ruta (UTF-8)
    std::string chain;                   // filtros de libavfilter tras el decode (sin el format=rgba)
    // Para no decodificar lo que la cadena va a tirar: antes de empezar, saltar a un fotograma clave
    // en o antes de este instante (µs desde el principio del archivo). Solo vale si la cadena
    // descarta todo lo anterior (el trim del export, con margen): los fotogramas que salen son los
    // mismos. Si el salto cae después del instante (un contenedor sin índice), se decodifica desde
    // el principio.
    std::optional<std::int64_t> preroll_us;
    // «Eliminar fondo» con el matte de la caché: la secuencia PNG del derivado como segunda entrada
    // del grafo (la `[k:v]` de _bg_input_args), con la etiqueta `[matte]` en la cadena.
    std::optional<core::MatteSpec> matte;
    // Decode por hardware (D3D11VA, CUDA, VAAPI o VideoToolbox, el primero que abra): la GPU da los
    // mismos YUV que el decodificador por software, y el fotograma se pasa a su formato (NV12 →
    // YUV420P, P010 → YUV420P10, sin pérdidas) antes del grafo, así que salen los mismos píxeles. Si
    // el códec o el tamaño no se pueden decodificar así, o falla antes del primer fotograma, sigue
    // por software. Video-yt no lo usa: el export lo deja apagado.
    bool hwaccel = false;
};

// `ASSUME_BT709_FILTER` de compose.py: un vídeo sin etiqueta de color se lee como BT.709.
inline constexpr const char* kAssumeBt709Filter =
    "setparams=colorspace=bt709:color_primaries=bt709:color_trc=bt709:range=tv";

// La entrada y la cadena del export para un clip (sin el encuadre): `color_pre` si no es un
// still y el material no declara matriz (color_untagged), y luego ffmpeg_trim_filter y
// `setpts=PTS-STARTPTS`.
[[nodiscard]] DecodeSpec export_decode_spec(const core::TimelineClip& clip, const std::filesystem::path& path,
                                            int fps);

// Decodifica y llama a `on_frame` con cada fotograma que sale de la cadena; si devuelve false,
// para. Devuelve cuántos fotogramas salieron. Opciones de entrada que entiende: -c:v
// (decodificador), -t (duración de la entrada) y las del demuxer (-loop, -framerate,
// -start_number, -ignore_loop).
[[nodiscard]] Result<std::size_t> decode_frames(const DecodeSpec& spec,
                                                      const std::function<bool(const RgbaFrame&)>& on_frame);

// El fotograma del instante `t` como frame_grab.extract_frame (sin su escalado ni el JPEG):
// `-ss {max(0, t):.3f}` antes de `-i` y `-frames:v 1`, es decir, el primer fotograma con
// pts ≥ t (redondeado a ms). Nada (nullopt) si `t` queda después del último.
[[nodiscard]] Result<std::optional<RgbaFrame>> frame_at(const std::filesystem::path& path, double t);

// El fotograma compuesto que da `compose.render_frame(…, at)`: `-ss {max(0, at):.3f}` a la salida
// del grafo toma el más cercano de la rejilla de 1/fps (un empate, el siguiente). Devuelve su
// índice k (el instante es k/fps).
[[nodiscard]] std::int64_t export_frame_tick(double at, int fps);

// El fotograma de un clip que el export compone en el fotograma k de la timeline (k/fps s),
// como compose.py: la cadena de export_decode_spec más el `fps=F` que va tras el encuadre,
// `reverse` y `setpts=PTS/{velocidad:.6f}` (nada de eso en un still) y
// `setpts=PTS-STARTPTS+{max(0, start):.3f}/TB`, con el truncado a ticks enteros de las setpts;
// el overlay toma el primer fotograma con pts = k o, si no hay, el último anterior, y pasado el
// final el último (`eof_action=repeat`). Nada (nullopt) si la cadena no da ningún fotograma.
//
// `matte`: lo que da core::clip_matte_spec del clip (build_clip_bg_mask). Con él, el alfa del
// matte automático (y el del croma multiplicado, si lo hay) entra antes del encuadre, como la rama
// del matte de _bg_source_chain; sin él, como el export sin matte en la caché: solo el croma.
[[nodiscard]] Result<std::optional<RgbaFrame>> export_frame_at(const core::TimelineClip& clip,
                                                               const std::filesystem::path& path, int fps,
                                                               std::int64_t tick,
                                                               const core::MatteSpec* matte = nullptr);

// Los fotogramas de un clip que compone el export, en orden de ticks: los mismos que
// export_frame_at, pero sin volver a decodificar desde el principio en cada fotograma (el
// decodificador avanza con los ticks). Pedir un tick anterior al último empieza de nuevo. Al revés
// (`reverse`), el tramo se cuenta una vez y se decodifica por bloques que terminan en el fotograma
// pedido (hasta 256 MB): al reproducir, los siguientes ticks salen del bloque. Un vídeo que no empieza cerca del
// principio del archivo no se decodifica desde 0: salta antes a un fotograma clave (preroll_us),
// 1 s antes del tramo. Cada uno decodifica por adelantado en su propio hilo (unos pocos fotogramas).
class ClipFrames {
public:
    // `matte`, como en export_frame_at; `hwaccel`, como en DecodeSpec (los mismos fotogramas).
    [[nodiscard]] static Result<ClipFrames> open(const core::TimelineClip& clip, const std::filesystem::path& path,
                                                 int fps, const core::MatteSpec* matte = nullptr,
                                                 bool hwaccel = false);
    // El tramo que hornea fx_bake.prepare_input para los efectos de vídeo: la cadena del clip (trim,
    // croma) y después la velocidad (`reverse`, `setpts=PTS/sp`) y `fps=F`. at(n) da su fotograma n
    // (pasado el final, el último), sin el desplazamiento del inicio del clip.
    [[nodiscard]] static Result<ClipFrames> open_bake_input(const core::TimelineClip& clip,
                                                            const std::filesystem::path& path, int fps,
                                                            const core::MatteSpec* matte = nullptr,
                                                            bool hwaccel = false);

    ClipFrames(ClipFrames&&) noexcept;
    ClipFrames& operator=(ClipFrames&&) noexcept;
    ClipFrames(const ClipFrames&) = delete;
    ClipFrames& operator=(const ClipFrames&) = delete;
    ~ClipFrames();

    // El fotograma del tick (nullptr si la cadena no da ninguno). Vale hasta la siguiente llamada.
    [[nodiscard]] Result<const RgbaFrame*> at(std::int64_t tick);

    // Al revés: bloques de `frames` fotogramas (≥ 1) en vez de los que quepan en 256 MB. Para los
    // tests, que así pasan por varios bloques con clips cortos; da los mismos fotogramas.
    void set_reverse_block(std::size_t frames);

private:
    struct Impl;
    explicit ClipFrames(std::unique_ptr<Impl> impl);
    std::unique_ptr<Impl> impl_;
};

// El fotograma n del tramo que hornea fx_bake.prepare_input (ClipFrames::open_bake_input), decodificando
// desde el principio: la referencia de los tests y de `genclip frame`.
[[nodiscard]] Result<std::optional<RgbaFrame>> bake_input_frame_at(const core::TimelineClip& clip,
                                                                   const std::filesystem::path& path, int fps,
                                                                   std::int64_t n,
                                                                   const core::MatteSpec* matte = nullptr);

// MD5 en hexadecimal (av_md5): para comparar fotogramas con las fixtures.
[[nodiscard]] std::string md5_hex(std::span<const std::uint8_t> data);

} // namespace gc::media
