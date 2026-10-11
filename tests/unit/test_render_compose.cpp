// gc_render: qué capas sabe dibujar ya compose_frame (unsupported_reason). Solo con Skia.

#include <catch2/catch_test_macros.hpp>

#include <string>

#include "gc/core/frame.hpp"
#include "gc/render/compose.hpp"

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

    FrameLayer blend = layer(video());
    blend.blend = "multiply";
    CHECK(unsupported_reason(blend));

    FrameLayer masked = layer(video());
    masked.masks.push_back(Json::object());
    CHECK(unsupported_reason(masked));

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

    // Filtros, efectos (solo los activos) y efectos de vídeo.
    TimelineClip look = video();
    look.look = "vintage";
    CHECK(unsupported_reason(layer(look)));
    TimelineClip effects = video();
    effects.effects = Json{{"blur", 0}, {"grayscale", false}};
    CHECK_FALSE(unsupported_reason(layer(effects)));
    effects.effects = Json{{"blur", 2.5}};
    CHECK(unsupported_reason(layer(effects)));
    effects.effects = Json{{"blur", "2"}}; // como texto, float("2") en Python
    CHECK(unsupported_reason(layer(effects)));
    effects.effects = Json{{"blur", "0"}};
    CHECK_FALSE(unsupported_reason(layer(effects)));
    TimelineClip vfx = video();
    vfx.vfx = Json::array({Json{{"id", "glitch"}}});
    CHECK(unsupported_reason(layer(vfx)));
}
