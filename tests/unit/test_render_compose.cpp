// gc_render: qué capas sabe dibujar ya compose_frame (unsupported_reason) y la caché de las capas
// quietas de compose_into (LayerCache). Solo con Skia.

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "gc/core/font_metrics.hpp"
#include "gc/core/frame.hpp"
#include "gc/core/io.hpp"
#include "gc/render/compose.hpp"
#include "gc/render/text.hpp"

using gc::Json;
using gc::core::FrameLayer;
using gc::core::TimelineClip;
using gc::render::unsupported_reason;

namespace {

FrameLayer layer(TimelineClip clip) {
    FrameLayer l;
    l.clip = std::move(clip);
    return l;
}

TimelineClip video() {
    TimelineClip c;
    c.id = "c1";
    c.kind = "video";
    c.asset_kind = "clips";
    return c;
}

} // namespace

TEST_CASE("unsupported_reason: lo que el render aún no dibuja", "[render]") {
    CHECK_FALSE(unsupported_reason(layer(video())));

    TimelineClip text = video();
    text.kind = "text";
    CHECK(unsupported_reason(layer(text)));

    // Los modos de fusión y la capa de ajuste ya se dibujan.
    FrameLayer blend = layer(video());
    blend.blend = "multiply";
    CHECK_FALSE(unsupported_reason(blend));
    TimelineClip adjustment = video();
    adjustment.kind = "adjustment";
    CHECK_FALSE(unsupported_reason(layer(adjustment)));
    TimelineClip shape = video();
    shape.kind = "shape";
    CHECK_FALSE(unsupported_reason(layer(shape)));

    // Las máscaras ya se dibujan; la de texto (si está activa), solo con las fuentes.
    FrameLayer masked = layer(video());
    masked.masks.push_back(Json{{"type", "linear"}, {"enabled", true}});
    CHECK_FALSE(unsupported_reason(masked));
    masked.masks.push_back(Json{{"type", "text"}, {"enabled", false}});
    CHECK_FALSE(unsupported_reason(masked));
    masked.masks.back()["enabled"] = true;
    CHECK(unsupported_reason(masked));
    const gc::render::TextFonts fonts(gc::core::FontDirs{gc::core::default_system_font_dir(),
                                                         gc::core::path_from_utf8(GC_RESOURCES_DIR) / "fonts"});
    CHECK_FALSE(unsupported_reason(masked, &fonts));

    // Eliminar fondo: el croma, el matte de la caché y el contorno ya los hace el decode.
    TimelineClip chroma = video();
    chroma.bg_removal = Json{{"enabled", true}, {"mode", "chroma"}, {"chroma", {{"enabled", true}}}};
    CHECK_FALSE(unsupported_reason(layer(chroma)));
    chroma.bg_removal = Json{{"enabled", true},
                             {"chroma", {{"enabled", true}}},
                             {"outline", {{"enabled", true}, {"width", 0.3}, {"opacity", 1}}}};
    CHECK_FALSE(unsupported_reason(layer(chroma)));
    TimelineClip matte = video();
    matte.bg_removal = Json{{"enabled", true}, {"auto", {{"enabled", true}, {"base_key", ""}, {"status", "idle"}}}};
    CHECK_FALSE(unsupported_reason(layer(matte)));
    matte.bg_removal = Json{{"enabled", true}, {"auto", {{"enabled", true}, {"base_key", "k"}, {"status", "ready"}}}};
    CHECK_FALSE(unsupported_reason(layer(matte)));

    // Los filtros de color y los efectos de clip ya se dibujan (también en una figura); los efectos
    // de vídeo, aún no.
    TimelineClip look = video();
    look.look = "vintage";
    look.filters = Json::array({Json{{"id", "noir"}, {"amount", 0.5}}});
    CHECK_FALSE(unsupported_reason(layer(look)));
    TimelineClip effects = video();
    effects.effects = Json{{"blur", 2.5}, {"grain", 12}, {"pixelate", 8}, {"vhs", true}, {"glow", 6}};
    CHECK_FALSE(unsupported_reason(layer(effects)));
    effects.kind = "shape";
    CHECK_FALSE(unsupported_reason(layer(effects)));
    TimelineClip vfx = video();
    vfx.vfx = Json::array({Json{{"id", "glitch"}}});
    CHECK(unsupported_reason(layer(vfx)));
}

namespace {

gc::render::Image pattern(int w, int h, int seed) {
    gc::render::Image img{w, h, std::vector<std::uint8_t>(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 4)};
    for (std::size_t i = 0; i < img.pixels.size(); ++i) {
        img.pixels[i] = static_cast<std::uint8_t>((i * 37 + static_cast<std::size_t>(seed) * 101 + (i / 7)) & 0xFFU);
    }
    return img;
}

TimelineClip still(const std::string& id) {
    TimelineClip c;
    c.id = id;
    c.kind = "image";
    c.asset_kind = "images";
    return c;
}

gc::core::Blit blit(double w, double h, gc::core::Affine m) {
    gc::core::Blit b;
    b.src = {0.0, 0.0, w, h};
    b.m = m;
    return b;
}

} // namespace

// LayerCache: las capas quietas de abajo salen de lo guardado y el fotograma es el mismo, byte a
// byte, que dibujándolas: un fondo quieto con una imagen que se mueve encima, un cambio de opacidad
// y otro de píxeles en el fondo, un fondo con filtro de color y uno que cambia en cada fotograma.
TEST_CASE("compose_into: con LayerCache, las capas quietas no se redibujan y el resultado es el mismo", "[render]") {
    gc::render::Image bg = pattern(64, 48, 1);
    const gc::render::Image top = pattern(16, 16, 2);
    std::map<std::string, const gc::render::Image*> images{{"bg", &bg}, {"top", &top}};
    const gc::render::SourceFn source = [&](const FrameLayer& l) -> gc::Result<const gc::render::Image*> {
        return images.at(l.clip->id);
    };
    auto plain = gc::render::Canvas::make(80, 60);
    auto cached = gc::render::Canvas::make(80, 60);
    REQUIRE(plain);
    REQUIRE(cached);
    gc::render::LayerCache cache;
    TimelineClip bg_clip = still("bg");
    for (int k = 0; k < 12; ++k) {
        INFO("fotograma " << k);
        if (k == 6) {
            bg.pixels[1000] ^= 0xFFU; // el fondo cambia: lo guardado ya no vale
        }
        if (k == 8) {
            bg_clip.look = "vintage"; // y con filtro de color
        }
        FrameLayer a = layer(bg_clip);
        a.blits = {blit(64, 48, {1.2, 0, 0, 1.2, 1.5, 0.5})};
        a.alpha = k == 4 ? 0.5 : 1.0;
        FrameLayer b = layer(still("top"));
        b.blits = {blit(16, 16, {1, 0, 0, 1, 3.0 + (k * 4.0), 10})}; // se mueve
        const std::vector<FrameLayer> layers{a, b};
        REQUIRE(gc::render::compose_into(*plain, layers, source));
        REQUIRE(gc::render::compose_into(*cached, layers, source, nullptr, {}, &cache));
        CHECK(cached->snapshot().pixels == plain->snapshot().pixels);
    }
    // Se guarda el fondo y se reutiliza mientras no cambia (la opacidad del 4, los píxeles del 6 y
    // el filtro del 8 obligan a volver a guardarlo).
    CHECK(cache.hits() >= 4);

    // Un fondo que cambia en cada fotograma (como un GIF): nunca sirve lo guardado y deja de guardar.
    gc::render::LayerCache gif_cache;
    for (int k = 0; k < 8; ++k) {
        bg = pattern(64, 48, 10 + k);
        FrameLayer a = layer(still("bg"));
        a.blits = {blit(64, 48, {1, 0, 0, 1, 0, 0})};
        const std::vector<FrameLayer> layers{a};
        REQUIRE(gc::render::compose_into(*plain, layers, source));
        REQUIRE(gc::render::compose_into(*cached, layers, source, nullptr, {}, &gif_cache));
        CHECK(cached->snapshot().pixels == plain->snapshot().pixels);
    }
    CHECK(gif_cache.hits() == 0);
}
