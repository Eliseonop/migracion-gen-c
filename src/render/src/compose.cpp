#include "gc/render/compose.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <format>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "gc/core/clip_adjust.hpp"
#include "gc/core/clip_effects.hpp"
#include "gc/core/clip_filters.hpp"
#include "gc/core/export_input.hpp"
#include "gc/core/masks.hpp"
#include "gc/render/effects.hpp"
#include "gc/render/shape.hpp"

#include "canvas_impl.hpp"

namespace gc::render {

namespace {

bool is_media(const core::TimelineClip& clip) {
    return clip.kind == "video" || clip.kind == "image" || clip.kind == "motion";
}

bool is_adjustment(const core::TimelineClip& clip) { return clip.kind == "adjustment"; }

bool is_shape(const core::TimelineClip& clip) { return clip.kind == "shape"; }

// El grano de un fotograma: cambia con el instante del clip y no con la pasada (la base y el
// fantasma de la máscara de ajuste llevan el mismo, como las dos cadenas del export, que
// empiezan con la misma semilla). FNV-1a del id, que no depende de la plataforma.
std::uint64_t grain_seed(const core::FrameLayer& layer, std::size_t blit) {
    std::string_view id = layer.clip->id;
    if (layer.ghost && id.ends_with(core::kGhostSuffix)) {
        id.remove_suffix(core::kGhostSuffix.size());
    }
    std::uint64_t h = 0xCBF29CE484222325ULL;
    for (const char c : id) {
        h = (h ^ static_cast<unsigned char>(c)) * 0x100000001B3ULL;
    }
    return h ^ std::bit_cast<std::uint64_t>(layer.local_t) ^ (blit * 0x9E3779B97F4A7C15ULL);
}

// Un blit con efectos de clip (effects.hpp): el recorte se dibuja primero en una imagen del
// tamaño de su destino (blit.dst: antes del giro, del volteo y del zoom de aparición), con los
// filtros de color, como el export, que escala el clip, le pasa el `look` y los efectos y
// después lo gira. Después, los efectos, y esa imagen va al cuadro con el resto de la
// colocación (blit.place), la opacidad y la cortinilla. Un destino enorme (más de 4 veces el
// cuadro) se reduce a esa área para no reservar memoria sin límite.
Result<void> draw_with_effects(Canvas& canvas, const Image& image, const core::Blit& blit,
                               const core::FrameLayer& layer, const std::optional<core::AffineColorMatrix>& color,
                               const core::ClipEffects& fx, std::size_t index) {
    const core::Box& dst = blit.dst;
    if (!(std::isfinite(dst.w) && std::isfinite(dst.h) && dst.w > 0 && dst.h > 0)) {
        return {};
    }
    double w = std::max(1.0, std::round(dst.w));
    double h = std::max(1.0, std::round(dst.h));
    const double cap = 4.0 * canvas.width() * canvas.height();
    if (w * h > cap) {
        const double k = std::sqrt(cap / (w * h));
        w = std::max(1.0, std::round(w * k));
        h = std::max(1.0, std::round(h * k));
    }
    auto clip = Canvas::make(static_cast<int>(w), static_cast<int>(h));
    if (!clip) {
        return std::unexpected(clip.error());
    }
    clip->clear({0, 0, 0, 0});
    const core::SourceCrop& src = blit.src;
    const core::Affine to_clip{w / src.sw, 0.0, 0.0, h / src.sh, -src.sx * w / src.sw, -src.sy * h / src.sh};
    if (auto drawn = clip->draw_image(image, src, to_clip, 1.0, std::nullopt, color); !drawn) {
        return drawn;
    }
    Image pixels = clip->snapshot();
    apply_effects(pixels, fx, grain_seed(layer, index));
    const core::Affine to_dst{dst.w / w, 0.0, 0.0, dst.h / h, dst.x, dst.y};
    return canvas.draw_image(pixels, {0.0, 0.0, w, h}, blit.place * to_dst, layer.alpha, layer.wipe);
}

// Una figura con efectos: el export la rasteriza en un PNG del cuadro (con su opacidad) y le
// pasa la cadena del clip; aquí, dibujada en una capa transparente del tamaño del cuadro, que
// después va encima.
Result<void> draw_shape_with_effects(Canvas& canvas, const core::FrameLayer& layer,
                                     const std::optional<core::AffineColorMatrix>& color,
                                     const core::ClipEffects& fx) {
    auto shape = Canvas::make(canvas.width(), canvas.height());
    if (!shape) {
        return std::unexpected(shape.error());
    }
    shape->clear({0, 0, 0, 0});
    if (auto drawn = draw_shape(*shape, layer, color); !drawn) {
        return drawn;
    }
    Image pixels = shape->snapshot();
    apply_effects(pixels, fx, grain_seed(layer, 0));
    const double w = canvas.width();
    const double h = canvas.height();
    return canvas.draw_image(pixels, {0.0, 0.0, w, h}, core::Affine{});
}

} // namespace

std::optional<std::string> unsupported_reason(const core::FrameLayer& layer, const TextFonts* fonts) {
    const core::TimelineClip& clip = *layer.clip;
    if (is_adjustment(clip)) {
        return std::nullopt;
    }
    if (!is_media(clip) && !is_shape(clip)) {
        return std::format("el render aún no dibuja capas de tipo «{}»", clip.kind);
    }
    if (auto reason = core::maskable(clip) ? mask_unsupported(layer.masks, fonts) : std::string(); !reason.empty()) {
        return reason;
    }
    if (clip.vfx && clip.vfx->is_array() && !clip.vfx->empty()) {
        return std::string("el render aún no aplica efectos de vídeo");
    }
    return std::nullopt;
}

Result<Image> compose_frame(const std::vector<core::FrameLayer>& layers, int width, int height,
                            const SourceFn& source, const TextFonts* fonts) {
    auto canvas = Canvas::make(width, height);
    if (!canvas) {
        return std::unexpected(canvas.error());
    }
    if (auto ok = compose_into(*canvas, layers, source, fonts); !ok) {
        return std::unexpected(ok.error());
    }
    return canvas->snapshot();
}

// --- LayerCache --------------------------------------------------------------------------------

struct LayerCache::Impl {
    // Una capa guardada: todo lo que decide cómo se dibuja (Canvas::draw_image por cada blit).
    struct Entry {
        std::string id;
        int width = 0;
        int height = 0;
        std::vector<std::uint8_t> pixels; // copia del material
        std::vector<core::Blit> blits;
        double alpha = 1.0;
        std::optional<std::array<core::Point, 4>> wipe;
        std::optional<core::AffineColorMatrix> color;
    };
    std::vector<Entry> entries;       // las capas de abajo que hay en `pixels`
    std::vector<std::uint8_t> pixels; // el lienzo (premultiplicado) tras dibujarlas
    int width = 0;
    int height = 0;
    std::size_t hits = 0;
    // Para no guardar en cada fotograma lo que cambia (un GIF): tras 3 guardados seguidos que el
    // fotograma siguiente no aprovechó, se descansa (restore_prefix).
    std::vector<std::string> run_ids;
    int failed = 0;
    int skipped = 0;
    bool built_last = false;
};

LayerCache::LayerCache() : impl_(std::make_unique<Impl>()) {}
LayerCache::LayerCache(LayerCache&&) noexcept = default;
LayerCache& LayerCache::operator=(LayerCache&&) noexcept = default;
LayerCache::~LayerCache() = default;
std::size_t LayerCache::hits() const noexcept { return impl_->hits; }

namespace {

std::optional<core::AffineColorMatrix> color_of(const core::FrameLayer& layer) {
    return core::stack_matrix(core::clip_filters(*layer.clip));
}

// Una capa que se puede guardar: una imagen fija que se dibuja con draw_image tal cual (sin
// máscaras, fusión, efectos de clip ni de vídeo).
bool cacheable(const core::FrameLayer& layer) {
    const core::TimelineClip& clip = *layer.clip;
    return is_media(clip) && core::is_still_clip(clip) && !layer.blits.empty() && layer.blend == "normal" &&
           layer.masks.empty() && !core::clip_effects(clip).any() && !(clip.vfx && clip.vfx->is_array() && !clip.vfx->empty());
}

bool same_wipe(const std::optional<std::array<core::Point, 4>>& a, const std::optional<std::array<core::Point, 4>>& b) {
    if (a.has_value() != b.has_value()) {
        return false;
    }
    if (!a) {
        return true;
    }
    for (std::size_t i = 0; i < 4; ++i) {
        if ((*a)[i].x != (*b)[i].x || (*a)[i].y != (*b)[i].y) {
            return false;
        }
    }
    return true;
}

bool same_blit(const core::Blit& a, const core::Blit& b) {
    return a.src.sx == b.src.sx && a.src.sy == b.src.sy && a.src.sw == b.src.sw && a.src.sh == b.src.sh &&
           a.m.a == b.m.a && a.m.b == b.m.b && a.m.c == b.m.c && a.m.d == b.m.d && a.m.e == b.m.e && a.m.f == b.m.f;
}

bool matches(const LayerCache::Impl::Entry& e, const core::FrameLayer& layer, const Image& image) {
    if (e.id != layer.clip->id || e.alpha != layer.alpha || e.width != image.width || e.height != image.height ||
        e.blits.size() != layer.blits.size() || !same_wipe(e.wipe, layer.wipe) || e.color != color_of(layer)) {
        return false;
    }
    for (std::size_t i = 0; i < e.blits.size(); ++i) {
        if (!same_blit(e.blits[i], layer.blits[i])) {
            return false;
        }
    }
    return e.pixels == image.pixels;
}

// Las `count` primeras capas (ya dibujadas en `canvas`) y el lienzo, en la caché.
void save_prefix(LayerCache::Impl& lc, Canvas& canvas, const std::vector<core::FrameLayer>& layers, std::size_t count,
                 const SourceFn& source) {
    lc.entries.clear();
    for (std::size_t i = 0; i < count; ++i) {
        const core::FrameLayer& layer = layers[i];
        auto got = source(layer);
        if (!got || *got == nullptr) {
            lc.entries.clear();
            return;
        }
        const Image& image = **got;
        lc.entries.push_back({layer.clip->id, image.width, image.height, image.pixels, layer.blits, layer.alpha,
                              layer.wipe, color_of(layer)});
    }
    if (!CanvasAccess::save_pixels(canvas, lc.pixels)) {
        lc.entries.clear();
        return;
    }
    lc.width = canvas.width();
    lc.height = canvas.height();
    lc.built_last = true;
}

// Si las capas de abajo son las guardadas, restaura el lienzo y devuelve cuántas son (0: no).
// `build`: tras cuántas capas guardar el lienzo en este fotograma (0: no se guarda).
std::size_t restore_prefix(LayerCache::Impl& lc, Canvas& canvas, const std::vector<core::FrameLayer>& layers,
                           const SourceFn& source, std::size_t& build) {
    build = 0;
    std::size_t run = 0;
    while (run < layers.size() && cacheable(layers[run])) {
        ++run;
    }
    std::vector<std::string> ids;
    ids.reserve(run);
    for (std::size_t i = 0; i < run; ++i) {
        ids.push_back(layers[i].clip->id);
    }
    if (ids != lc.run_ids) { // otras capas abajo: se vuelve a probar desde cero
        lc.run_ids = std::move(ids);
        lc.failed = 0;
        lc.skipped = 0;
    }
    // Cuántas de las guardadas siguen igual, desde abajo.
    std::size_t match = 0;
    if (lc.width == canvas.width() && lc.height == canvas.height()) {
        while (match < lc.entries.size() && match < run) {
            auto got = source(layers[match]);
            if (!got || *got == nullptr || !matches(lc.entries[match], layers[match], **got)) {
                break;
            }
            ++match;
        }
    }
    if (match > 0 && match == lc.entries.size() && CanvasAccess::restore_pixels(canvas, lc.pixels)) {
        ++lc.hits;
        lc.failed = 0;
        lc.built_last = false;
        return match;
    }
    if (match > 0) {
        lc.failed = 0;
    } else if (lc.built_last) {
        ++lc.failed; // nada de lo guardado la última vez sirvió
    }
    lc.built_last = false;
    // Si solo siguen igual las de más abajo (una imagen que se mueve encima de un fondo quieto),
    // se guardan esas; si ninguna, todas las que se pueden. Tras 3 guardados seguidos que no
    // sirvieron (un GIF), se descansa 16 fotogramas antes de volver a probar.
    if (match > 0) {
        build = match;
    } else if (run > 0) {
        if (lc.failed < 3) {
            build = run;
        } else if (++lc.skipped >= 16) {
            lc.failed = 0;
            lc.skipped = 0;
            build = run;
        }
    }
    return 0;
}

} // namespace

Result<void> compose_into(Canvas& canvas, const std::vector<core::FrameLayer>& layers, const SourceFn& source,
                          const TextFonts* fonts, const BetweenFn& between, LayerCache* cache) {
    const int width = canvas.width();
    const int height = canvas.height();
    // Con la caché (y sin textos entre capas), las capas quietas de abajo salen de lo guardado.
    LayerCache::Impl* lc = cache != nullptr && !between ? &cache->impl() : nullptr;
    std::size_t build = 0;
    const std::size_t first = lc != nullptr ? restore_prefix(*lc, canvas, layers, source, build) : 0;
    if (first == 0) {
        canvas.clear({0, 0, 0, 255});
    }
    for (std::size_t li = first; li < layers.size(); ++li) {
        const core::FrameLayer& layer = layers[li];
        if (li > 0 && li == build) {
            save_prefix(*lc, canvas, layers, build, source);
        }
        const std::string& id = layer.clip->id;
        if (auto reason = unsupported_reason(layer, fonts)) {
            return fail(ErrorCode::Unsupported, std::format("{} (clip {})", *reason, id));
        }
        if (between) {
            if (auto ok = between(canvas, &layer); !ok) {
                return ok;
            }
        }
        // Capa de ajuste (compose.py, #19): su matriz de color sobre todo lo compuesto debajo
        // durante su tramo, sin material ni máscaras (el export no las aplica).
        if (is_adjustment(*layer.clip)) {
            if (const auto m = core::adjustment_matrix(*layer.clip)) {
                canvas.apply_color_matrix(*m);
            }
            continue;
        }
        // Una figura no tiene material: se dibuja su geometría (shape.hpp).
        const bool shape = is_shape(*layer.clip);
        const Image* image = nullptr;
        if (!shape) {
            if (layer.blits.empty()) {
                // Con las reglas del preview, evaluate deja sin blits el material sin tamaño.
                return fail(ErrorCode::InvalidArgument,
                            std::format("el clip {} no tiene las dimensiones de su material", id));
            }
            auto got = source(layer);
            if (!got) {
                return std::unexpected(got.error());
            }
            if (*got == nullptr) {
                return fail(ErrorCode::Internal, std::format("no hay fotograma del material del clip {}", id));
            }
            image = *got;
        }
        // Máscaras (en coordenadas del cuadro): el clip va a una capa que después se recorta con
        // su alfa, como el maskedmerge del export tras componer el clip. Con un modo de fusión, la
        // capa se funde con lo de debajo en vez de ir encima (clip_blend.py: el clip en una capa
        // transparente del tamaño del cuadro y blend_steps; la máscara mezcla después, que con el
        // fondo opaco es lo mismo que recortar la capa antes de fundirla).
        std::optional<MaskAlpha> mask;
        // Solo en los tipos que el export enmascara (clip_mask.maskable: vídeo, imagen, figura y
        // texto); un motion con máscara sale entero, como allí.
        if (!layer.masks.empty() && core::maskable(*layer.clip)) {
            auto alpha = mask_alpha(layer.masks, width, height, fonts);
            if (!alpha) {
                return std::unexpected(alpha.error());
            }
            mask = std::move(*alpha);
        }
        const bool blended = layer.blend != "normal";
        if (mask || blended) {
            canvas.begin_layer(layer.blend);
        }
        // Filtros de color (o el `look` antiguo): una matriz por clip, sobre lo ya escalado.
        const auto color = core::stack_matrix(core::clip_filters(*layer.clip));
        const auto close_on_error = [&] {
            if (mask || blended) {
                (void)canvas.end_layer(MaskAlpha{}); // cierra la capa sin dibujarla
            }
        };
        if (shape) {
            const core::ClipEffects fx = core::clip_effects(*layer.clip);
            if (auto drawn = fx.any() ? draw_shape_with_effects(canvas, layer, color, fx)
                                      : draw_shape(canvas, layer, color);
                !drawn) {
                close_on_error();
                return std::unexpected(drawn.error());
            }
        } else {
            const core::ClipEffects fx = core::clip_effects(*layer.clip);
            const bool effects = fx.any();
            for (std::size_t i = 0; i < layer.blits.size(); ++i) {
                const core::Blit& blit = layer.blits[i];
                auto drawn = effects ? draw_with_effects(canvas, *image, blit, layer, color, fx, i)
                                     : canvas.draw_image(*image, blit.src, blit.m, layer.alpha, layer.wipe, color);
                if (!drawn) {
                    close_on_error();
                    return std::unexpected(drawn.error());
                }
            }
        }
        if (mask) {
            if (auto merged = canvas.end_layer(*mask); !merged) {
                return std::unexpected(merged.error());
            }
        } else if (blended) {
            canvas.end_layer();
        }
    }
    if (build > 0 && build == layers.size()) { // todas las capas eran de las que se guardan
        save_prefix(*lc, canvas, layers, build, source);
    }
    if (between) {
        return between(canvas, nullptr);
    }
    return {};
}

} // namespace gc::render
