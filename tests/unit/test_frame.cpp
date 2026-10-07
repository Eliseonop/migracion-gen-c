// El fotograma con las reglas del render (kRenderLayers) donde difiere del preview, cuya
// paridad prueba tests/parity/test_frame_parity.cpp, y los casos de error.

#include <catch2/catch_test_macros.hpp>

#include <optional>
#include <string>

#include "gc/core/frame.hpp"

using namespace gc::core;

namespace {

TimelineClip clip(std::string id, double start, double dur, std::string kind = "video") {
    TimelineClip c;
    c.id = std::move(id);
    c.track_id = kind == "text" ? "T1" : "V1";
    c.kind = std::move(kind);
    c.asset_kind = "clips";
    c.start = start;
    c.in_point = 10.0;
    c.out_point = 10.0 + dur;
    return c;
}

Timeline timeline(std::vector<TimelineClip> clips) {
    Timeline tl;
    TimelineTrack v;
    v.id = "V1";
    v.name = "V1";
    v.kind = "video";
    TimelineTrack t;
    t.id = "T1";
    t.name = "T1";
    t.kind = "text";
    tl.tracks = {v, t};
    for (auto& c : clips) {
        tl.clips.emplace_back(std::move(c));
    }
    return tl;
}

std::optional<MediaSize> hd(const TimelineClip& /*clip*/) { return MediaSize{1920, 1080}; }

constexpr FrameSpec kRender{1080, 1920, kRenderLayers};
constexpr FrameSpec kPreview{1080, 1920, kPreviewLayers};

} // namespace

TEST_CASE("fotograma: el render aplica una escala de relleno de 1,0005 y el preview no", "[frame]") {
    TimelineClip c = clip("f", 0, 2);
    c.transform = gc::Json{{"scale", 1.0005}};
    const Timeline tl = timeline({c});
    const auto render = evaluate(tl, 1, kRender, hd);
    const auto preview = evaluate(tl, 1, kPreview, hd);
    REQUIRE(render);
    REQUIRE(preview);
    REQUIRE(render->at(0).blits.size() == 1);
    REQUIRE(preview->at(0).blits.size() == 1);
    // Relleno de 1920×1080 en 1080×1920: el recorte de 9:16 (607,5 px de ancho) a 1080 px.
    const double cover = 1080.0 / 607.5;
    CHECK(preview->at(0).blits[0].m.a == cover);
    CHECK(render->at(0).blits[0].m.a > cover);
}

TEST_CASE("fotograma: un material sin tamaño válido es un error en el render", "[frame]") {
    const Timeline tl = timeline({clip("a", 0, 2)});
    const MediaSizeFn zero = [](const TimelineClip&) { return std::optional(MediaSize{0, 1080}); };
    CHECK_FALSE(evaluate(tl, 1, kRender, zero));
    // El preview no lo dibuja, como drawComposite.
    const auto preview = evaluate(tl, 1, kPreview, zero);
    REQUIRE(preview);
    REQUIRE(preview->size() == 1);
    CHECK(preview->at(0).blits.empty());
    // Sin material conocido no hay error: la capa va sin blits.
    const auto none = evaluate(tl, 1, kRender, [](const TimelineClip&) { return std::optional<MediaSize>(); });
    REQUIRE(none);
    CHECK(none->at(0).blits.empty());
}

TEST_CASE("fotograma: las dimensiones de salida tienen que ser mayores que 0", "[frame]") {
    const Timeline tl = timeline({clip("a", 0, 2)});
    CHECK_FALSE(evaluate(tl, 1, FrameSpec{0, 1920}, hd));
    CHECK_FALSE(evaluate(tl, 1, FrameSpec{1080, -1}, hd));
}

TEST_CASE("fotograma: con inicio negativo el render cuenta desde 0, como el export", "[frame]") {
    const Timeline tl = timeline({clip("a", -0.5, 2)});
    const auto render = evaluate(tl, 1, kRender, hd);
    const auto preview = evaluate(tl, 1, kPreview, hd);
    REQUIRE(render);
    REQUIRE(preview);
    CHECK(render->at(0).local_t == 1.0);
    CHECK(render->at(0).src_t == 11.0);
    CHECK(preview->at(0).local_t == 1.5);
    CHECK(preview->at(0).src_t == 11.5);
}

TEST_CASE("fotograma: el tiempo de fuente con velocidad y al revés, dentro del clip", "[frame]") {
    TimelineClip c = clip("r", 0, 4);
    c.speed = 2;
    c.reverse = true;
    CHECK(source_time(c, 0.5, 0) == 13.0);
    CHECK(source_time(c, 5, 0) == 10.0); // acotado a in_point
    c.reverse = false;
    CHECK(source_time(c, 0.5, 0) == 11.0);
    CHECK(source_time(c, -1, 0) == 10.0);
}

TEST_CASE("fotograma: pasadas de la máscara de ajuste y textos sin material", "[frame]") {
    TimelineClip a = clip("a", 0, 2);
    a.masks = {gc::Json{{"type", "rectangle"}, {"target", "adjust"}}};
    a.effects = gc::Json{{"exposure", 0.5}};
    a.blend_mode = "screen";
    const Timeline tl = timeline({a, clip("t", 0, 2, "text")});
    const auto layers = evaluate(tl, 1, kRender, hd);
    REQUIRE(layers);
    REQUIRE(layers->size() == 3);
    CHECK(layers->at(0).clip->id == "a");
    CHECK_FALSE(layers->at(0).ghost);
    CHECK(layers->at(0).masks.empty()); // la de ajuste no recorta la base
    CHECK(layers->at(1).clip->id == "a__adj");
    CHECK(layers->at(1).ghost);
    CHECK(layers->at(1).masks.size() == 1); // en el fantasma recorta
    CHECK(layers->at(1).blend == "screen");
    CHECK(layers->at(1).blits.size() == 1);
    CHECK(layers->at(2).clip->id == "t");
    CHECK(layers->at(2).blits.empty());
}

TEST_CASE("fotograma: la cortinilla recorta el destino sin girar", "[frame]") {
    TimelineClip c = clip("w", 0, 2);
    c.layout = "overlay";
    c.appear = "wipe";
    c.transform = gc::Json{{"x", 0.5}, {"y", 0.5}, {"scale", 0.5}, {"rotation", 90}};
    const Timeline tl = timeline({c});
    const auto layers = evaluate(tl, 0.2, kRender, hd);
    REQUIRE(layers);
    REQUIRE(layers->at(0).wipe);
    const auto& q = *layers->at(0).wipe;
    // Sin recorte libre, el overlay recorta la fuente a 9:16 (607,5 × 1080 px); a escala 0,5,
    // un destino de 303,75 × 540 centrado en (540, 960). A la mitad de la aparición se ve la
    // mitad izquierda, y el recorte no gira con el clip.
    CHECK(q[0].x == 388.125);
    CHECK(q[0].y == 690.0);
    CHECK(q[1].x == 540.0);
    CHECK(q[2].y == 1230.0);
}
