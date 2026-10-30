// gc_render: qué capas sabe dibujar ya compose_frame (unsupported_reason) y la caché de las capas
// quietas de compose_into (LayerCache). Solo con Skia.

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
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
// byte, que dibujándolas. Cada escenario guarda el fondo en los primeros fotogramas y después cambia
// UNA cosa de las que deciden el dibujo (opacidad, píxeles, filtro, recorte, matriz, cortinilla,
// número de blits, tamaño del lienzo): si la caché no la comparara, el fotograma saldría distinto.
// Y lo que no se debe guardar (máscara, fusión, efectos, textos entre capas) nunca acierta.

namespace {

struct Frame {
    std::vector<FrameLayer> layers;
    int canvas = 0; // 0: el lienzo de 80×60; 1: otro de 96×72 y 2: otro de 60×80 (los mismos bytes), con la misma caché
};

// Compone `n` fotogramas con y sin caché y comprueba que son iguales; devuelve los aciertos.
std::size_t run_frames(int n, const std::function<Frame(int)>& frame, const gc::render::SourceFn& source,
                       const gc::render::BetweenFn& between = {}) {
    std::array<gc::Result<gc::render::Canvas>, 3> plain{gc::render::Canvas::make(80, 60), gc::render::Canvas::make(96, 72),
                                                         gc::render::Canvas::make(60, 80)};
    std::array<gc::Result<gc::render::Canvas>, 3> cached{gc::render::Canvas::make(80, 60), gc::render::Canvas::make(96, 72),
                                                          gc::render::Canvas::make(60, 80)};
    for (const auto& c : plain) {
        REQUIRE(c);
    }
    for (const auto& c : cached) {
        REQUIRE(c);
    }
    gc::render::LayerCache cache;
    for (int k = 0; k < n; ++k) {
        INFO("fotograma " << k);
        const Frame f = frame(k);
        const auto i = static_cast<std::size_t>(f.canvas);
        REQUIRE(gc::render::compose_into(*plain[i], f.layers, source, nullptr, between));
        REQUIRE(gc::render::compose_into(*cached[i], f.layers, source, nullptr, between, &cache));
        CHECK(cached[i]->snapshot().pixels == plain[i]->snapshot().pixels);
    }
    return cache.hits();
}

// El fondo recortado con margen en los cuatro lados, para poder mover cada borde por separado.
FrameLayer bg_layer(const TimelineClip& clip, gc::core::Affine m = {1.2, 0, 0, 1.2, 1.5, 0.5}) {
    FrameLayer a = layer(clip);
    a.blits = {blit(64, 48, m)};
    a.blits[0].src = {2, 2, 56, 40};
    return a;
}

} // namespace

TEST_CASE("compose_into: con LayerCache, las capas quietas no se redibujan y el resultado es el mismo", "[render]") {
    gc::render::Image bg = pattern(64, 48, 1);
    const gc::render::Image top = pattern(16, 16, 2);
    std::map<std::string, const gc::render::Image*> images{{"bg", &bg}, {"top", &top}};
    const gc::render::SourceFn source = [&](const FrameLayer& l) -> gc::Result<const gc::render::Image*> {
        return images.at(l.clip->id);
    };
    const TimelineClip bg_clip = still("bg");
    // Encima del fondo, una imagen que se mueve: la caché se queda con el fondo solo.
    const auto moving_top = [&](int k) {
        FrameLayer b = layer(still("top"));
        b.blits = {blit(16, 16, {1, 0, 0, 1, 3.0 + (k * 4.0), 10})};
        return b;
    };
    // Fotogramas 0–3 iguales (se guarda y acierta); del 4 en adelante, el cambio.
    const auto changed = [&](const std::function<void(FrameLayer&)>& change) {
        return [&, change](int k) {
            FrameLayer a = bg_layer(bg_clip);
            if (k >= 4) {
                change(a);
            }
            return Frame{{a, moving_top(k)}};
        };
    };

    SECTION("opacidad") {
        CHECK(run_frames(7, changed([](FrameLayer& a) { a.alpha = 0.5; }), source) >= 3);
    }
    SECTION("filtro de color") {
        TimelineClip vintage = bg_clip;
        vintage.look = "vintage";
        CHECK(run_frames(7, changed([&](FrameLayer& a) { a.clip = vintage; }), source) >= 3);
    }
    SECTION("recorte") {
        CHECK(run_frames(7, changed([](FrameLayer& a) { a.blits[0].src = {4, 3, 50, 40}; }), source) >= 3);
    }
    SECTION("zoom") {
        CHECK(run_frames(7, changed([](FrameLayer& a) { a.blits[0].m.a = 1.5; }), source) >= 3);
    }
    SECTION("escala vertical") {
        CHECK(run_frames(7, changed([](FrameLayer& a) { a.blits[0].m.d = 0.9; }), source) >= 3);
    }
    SECTION("giro (b y c de la matriz)") {
        CHECK(run_frames(7, changed([](FrameLayer& a) { a.blits[0].m.b = 0.2; }), source) >= 3);
        CHECK(run_frames(7, changed([](FrameLayer& a) { a.blits[0].m.c = -0.15; }), source) >= 3);
    }
    SECTION("recorte: cada borde por separado") {
        CHECK(run_frames(7, changed([](FrameLayer& a) { a.blits[0].src.sy = 3.5; }), source) >= 3);
        CHECK(run_frames(7, changed([](FrameLayer& a) { a.blits[0].src.sw = 50; }), source) >= 3);
        CHECK(run_frames(7, changed([](FrameLayer& a) { a.blits[0].src.sh = 44; }), source) >= 3);
    }
    SECTION("desplazamiento vertical") {
        CHECK(run_frames(7, changed([](FrameLayer& a) { a.blits[0].m.f = 6.25; }), source) >= 3);
    }
    SECTION("cortinilla") {
        CHECK(run_frames(7, changed([](FrameLayer& a) {
                  a.wipe = std::array<gc::core::Point, 4>{{{0, 0}, {40, 0}, {40, 60}, {0, 60}}};
              }),
              source) >= 3);
    }
    SECTION("doble encuadre (un blit más)") {
        CHECK(run_frames(7, changed([](FrameLayer& a) { a.blits.push_back(blit(64, 48, {0.5, 0, 0, 0.5, 40, 30})); }),
                         source) >= 3);
    }
    SECTION("píxeles del material") {
        CHECK(run_frames(8,
                         [&](int k) {
                             if (k == 4) {
                                 bg.pixels[1000] ^= 0xFFU;
                             }
                             return Frame{{bg_layer(bg_clip), moving_top(k)}};
                         },
                         source) >= 4);
    }
    SECTION("otro tamaño de lienzo con la misma caché") {
        CHECK(run_frames(7, [&](int k) { return Frame{{bg_layer(bg_clip), moving_top(k)}, k >= 4 ? 1 : 0}; }, source) >=
              3);
        // 60×80 tiene los mismos bytes que 80×60: solo lo distingue la comparación del tamaño.
        CHECK(run_frames(7, [&](int k) { return Frame{{bg_layer(bg_clip), moving_top(k)}, k >= 4 ? 2 : 0}; }, source) >=
              3);
    }
    SECTION("una capa de ajuste encima") {
        TimelineClip adjust;
        adjust.id = "adj";
        adjust.kind = "adjustment";
        adjust.look = "noir";
        CHECK(run_frames(6, [&](int k) { return Frame{{bg_layer(bg_clip), moving_top(k), layer(adjust)}}; }, source) >=
              3);
    }
    SECTION("lo que no se guarda: máscara, fusión y efectos en el fondo, o textos entre capas") {
        FrameLayer masked = bg_layer(bg_clip);
        masked.masks.push_back(Json{{"type", "linear"}, {"enabled", true}});
        CHECK(run_frames(4, [&](int k) { return Frame{{masked, moving_top(k)}}; }, source) == 0);
        FrameLayer blended = bg_layer(bg_clip);
        blended.blend = "multiply";
        CHECK(run_frames(4, [&](int k) { return Frame{{blended, moving_top(k)}}; }, source) == 0);
        TimelineClip blurred = bg_clip;
        blurred.effects = Json{{"blur", 2.5}};
        CHECK(run_frames(4, [&](int k) { return Frame{{bg_layer(blurred), moving_top(k)}}; }, source) == 0);
        const gc::render::BetweenFn between = [](gc::render::Canvas& c, const FrameLayer* next) -> gc::Result<void> {
            if (next != nullptr && next->clip->id == "top") {
                c.fill_rect(5, 5, 10, 10, {255, 255, 0, 255}); // un «texto» entre el fondo y la imagen
            }
            return {};
        };
        CHECK(run_frames(4, [&](int k) { return Frame{{bg_layer(bg_clip), moving_top(k)}}; }, source, between) == 0);
    }
    SECTION("un fondo que cambia en cada fotograma: no acierta, descansa y vuelve a probar") {
        // Cambia en los fotogramas 0–9 y después se queda quieto. Guarda en 0, 1 y 2 sin acertar,
        // descansa del 3 al 17 (aunque ya no cambie) y en el 18 vuelve a guardar: acierta del 19 al 31.
        const std::size_t hits = run_frames(
            32,
            [&](int k) {
                bg = pattern(64, 48, k < 10 ? 10 + k : 99);
                return Frame{{bg_layer(bg_clip)}};
            },
            source);
        CHECK(hits == 13);
    }
}
