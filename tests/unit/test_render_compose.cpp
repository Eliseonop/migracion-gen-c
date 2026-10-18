// gc_render: qué capas sabe dibujar ya compose_frame (unsupported_reason). Solo con Skia.

#include <catch2/catch_test_macros.hpp>

#include <string>

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

    // Eliminar fondo: el croma solo ya lo hace el decode; con contorno, aún no.
    TimelineClip chroma = video();
    chroma.bg_removal = Json{{"enabled", true}, {"mode", "chroma"}, {"chroma", {{"enabled", true}}}};
    CHECK_FALSE(unsupported_reason(layer(chroma)));
    chroma.bg_removal = Json{{"enabled", true},
                             {"chroma", {{"enabled", true}}},
                             {"outline", {{"enabled", true}, {"width", 0.3}, {"opacity", 1}}}};
    CHECK(unsupported_reason(layer(chroma)));
    chroma.bg_removal = Json{{"enabled", true},
                             {"chroma", {{"enabled", true}}},
                             {"outline", {{"enabled", true}, {"width", 0}, {"opacity", 1}}}};
    CHECK_FALSE(unsupported_reason(layer(chroma)));
    // El automático solo cuenta con el matte listo.
    TimelineClip matte = video();
    matte.bg_removal = Json{{"enabled", true}, {"auto", {{"enabled", true}, {"base_key", ""}, {"status", "idle"}}}};
    CHECK_FALSE(unsupported_reason(layer(matte)));
    matte.bg_removal = Json{{"enabled", true}, {"auto", {{"enabled", true}, {"base_key", "k"}, {"status", "ready"}}}};
    CHECK(unsupported_reason(layer(matte)));

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
