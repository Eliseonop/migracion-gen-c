#pragma once

// El fotograma compuesto: las capas de core::evaluate dibujadas, de fondo a frente, sobre el
// fondo negro opaco del export (`color=c=black`, compose.py).
//
// Por ahora las capas con material (vídeo, imagen y motion: las que traen blits), las figuras
// (shape.hpp) y las capas de ajuste (su matriz sobre lo de debajo), con los 12 modos de fusión,
// con máscaras (también la de texto; mask.hpp), con «Eliminar fondo» (el croma, el matte de la
// caché y el contorno: los hace el decode, en el espacio del material), con los filtros de color (`filters` y `look`) y los efectos de clip
// (`effects`, effects.hpp: sobre el clip escalado a su destino, antes de girarlo) y sin los
// efectos de vídeo (`vfx`). Lo demás es un error Unsupported hasta que llegue, para no dar un
// fotograma incompleto o mal dibujado como bueno (unsupported_reason).

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "gc/core/error.hpp"
#include "gc/core/frame.hpp"
#include "gc/render/canvas.hpp"

namespace gc::render {

// El fotograma del material de una capa en su instante (layer.src_t). Error si no se puede
// obtener; el puntero tiene que seguir siendo válido hasta que vuelva compose_frame.
using SourceFn = std::function<Result<const Image*>(const core::FrameLayer&)>;

// Por qué el render aún no puede dibujar esta capa, o nada si puede. Una capa de material sin
// blits (evaluate no supo sus dimensiones) no cuenta aquí: compose_frame la da como error de
// argumento. Sin `fonts`, una máscara de texto no se puede dibujar.
[[nodiscard]] std::optional<std::string> unsupported_reason(const core::FrameLayer& layer,
                                                            const TextFonts* fonts = nullptr);

// `fonts`: las de las máscaras de texto (text.hpp).
[[nodiscard]] Result<Image> compose_frame(const std::vector<core::FrameLayer>& layers, int width, int height,
                                          const SourceFn& source, const TextFonts* fonts = nullptr);

// Lo que se dibuja entre las capas: se llama antes de cada capa (con ella) y al final (con
// nullptr). Así van en su sitio de la pila los textos que el export mete en la cadena de vídeo.
using BetweenFn = std::function<Result<void>(Canvas&, const core::FrameLayer* next)>;

// Las capas quietas de abajo de un fotograma para el siguiente, en el mismo lienzo: si las
// primeras capas son imágenes fijas que se dibujan igual (mismos píxeles, recorte, matriz,
// opacidad, cortinilla y filtros de color; sin máscaras, fusión ni efectos), compose_into
// restaura el lienzo tal como quedó tras ellas en vez de redibujarlas. El resultado es el mismo,
// byte a byte. Una por lienzo (no se comparte entre hilos). Solo sin `between`: un texto entre
// medias cambiaría lo guardado.
class LayerCache {
public:
    LayerCache();
    LayerCache(LayerCache&&) noexcept;
    LayerCache& operator=(LayerCache&&) noexcept;
    LayerCache(const LayerCache&) = delete;
    LayerCache& operator=(const LayerCache&) = delete;
    ~LayerCache();

    // Fotogramas que se sirvieron de lo guardado (para medir).
    [[nodiscard]] std::size_t hits() const noexcept;

    struct Impl; // interno (compose.cpp)
    [[nodiscard]] Impl& impl() noexcept { return *impl_; }

private:
    std::unique_ptr<Impl> impl_;
};

// Lo que hay debajo de las capas: el negro del export o, en la vista previa, un color y una imagen que
// cubre el cuadro sin deformarse (drawBackdrop de render/canvas.js). Nunca llega al export.
struct Backdrop {
    Rgba8 color{0, 0, 0, 255};
    const Image* image = nullptr;
};

// Lo mismo, pero sobre un lienzo que ya existe (se borra a negro opaco, o al `backdrop`): para poner
// encima los textos y reutilizar el lienzo de un fotograma al siguiente. El resultado es opaco. Con
// `cache`, las capas quietas de abajo no se redibujan (LayerCache).
[[nodiscard]] Result<void> compose_into(Canvas& canvas, const std::vector<core::FrameLayer>& layers,
                                        const SourceFn& source, const TextFonts* fonts = nullptr,
                                        const BetweenFn& between = {}, LayerCache* cache = nullptr,
                                        const Backdrop& backdrop = {});

} // namespace gc::render
