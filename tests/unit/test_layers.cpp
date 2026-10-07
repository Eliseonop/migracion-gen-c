// Reglas de capas del render C++ (kRenderLayers) donde difieren del preview de video-yt
// (kPreviewLayers, cuya paridad prueba tests/parity/test_layers_parity.cpp). Ver las
// notas de F3 en docs/TODO.md.

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "gc/core/layers.hpp"

using gc::Cow;
using namespace gc::core;

namespace {

TimelineClip clip(std::string id, std::string track, double start, double dur, std::string kind = "video") {
    TimelineClip c;
    c.id = std::move(id);
    c.track_id = std::move(track);
    c.kind = std::move(kind);
    c.asset_kind = "clips";
    c.start = start;
    c.out_point = dur;
    return c;
}

TimelineTrack track(std::string id, std::string kind = "video") {
    TimelineTrack t;
    t.id = id;
    t.name = id;
    t.kind = std::move(kind);
    return t;
}

std::vector<std::string> ids(const std::vector<Cow<TimelineClip>>& list) {
    std::vector<std::string> out;
    for (const auto& c : list) {
        out.push_back(c->id);
    }
    return out;
}

using Ids = std::vector<std::string>;

} // namespace

TEST_CASE("capas: en un corte se ve solo el clip que entra", "[layers]") {
    Timeline tl;
    tl.tracks = {track("V1")};
    tl.clips = {clip("a", "V1", 0, 1), clip("b", "V1", 1, 1)};
    CHECK(ids(layers_at(tl, 0.99)) == Ids{"a"});
    CHECK(ids(layers_at(tl, 1.0)) == Ids{"b"});
    CHECK(ids(layers_at(tl, 1.999)) == Ids{"b"});
    CHECK(ids(layers_at(tl, 2.0)).empty());
    // El preview ya muestra «b» 0,02 s antes.
    CHECK(ids(layers_at(tl, 0.99, kPreviewLayers)) == Ids{"a", "b"});
}

TEST_CASE("capas: sin el margen de 0,02 s del preview", "[layers]") {
    Timeline tl;
    tl.tracks = {track("V1")};
    tl.clips = {clip("a", "V1", 1, 1)};
    CHECK(ids(layers_at(tl, 0.99)).empty());
    CHECK(ids(layers_at(tl, 0.99, kPreviewLayers)) == Ids{"a"});
    CHECK(ids(layers_at(tl, 1.0)) == Ids{"a"});
}

TEST_CASE("capas: en un solape de una pista, el que empieza después va delante", "[layers]") {
    Timeline tl;
    tl.tracks = {track("V1")};
    // Solape de 15 ms con la lista al revés que el tiempo (move_clip no reordena la lista).
    tl.clips = {clip("entra", "V1", 0.985, 1), clip("sale", "V1", 0, 1)};
    CHECK(ids(layers_at(tl, 0.99)) == Ids{"sale", "entra"});
    CHECK(ids(layers_at(tl, 0.99, kPreviewLayers)) == Ids{"entra", "sale"});
    // Mismo inicio: desempata el índice en la lista.
    tl.clips = {clip("x", "V1", 0.5, 1), clip("y", "V1", 0.5, 1)};
    CHECK(ids(layers_at(tl, 0.7)) == Ids{"x", "y"});
}

TEST_CASE("capas: la pila manda sobre el inicio", "[layers]") {
    Timeline tl;
    tl.tracks = {track("V1"), track("T1", "text"), track("V2")};
    tl.clips = {clip("arriba", "V2", 0, 2), clip("texto", "T1", 0.5, 2, "text"), clip("abajo", "V1", 1, 2)};
    CHECK(ids(layers_at(tl, 1.5)) == Ids{"abajo", "texto", "arriba"});
    CHECK(ids(videos_at(tl, 1.5)) == Ids{"abajo", "arriba"});
}

TEST_CASE("capas: un inicio negativo cuenta desde 0, como el export", "[layers]") {
    Timeline tl;
    tl.tracks = {track("V1")};
    tl.clips = {clip("a", "V1", -0.5, 1)};
    CHECK(ids(layers_at(tl, 0.7)) == Ids{"a"});   // [0, 1)
    CHECK(ids(layers_at(tl, 1.0)).empty());
    CHECK(ids(layers_at(tl, 0.7, kPreviewLayers)).empty()); // [-0.52, 0.5)
}

TEST_CASE("capas: el inicio acotado a 0 también ordena", "[layers]") {
    Timeline tl;
    tl.tracks = {track("V1")};
    // Los dos empiezan en 0 para el render: desempata la lista, no el inicio sin acotar.
    tl.clips = {clip("b", "V1", 0, 2), clip("a", "V1", -1, 3)};
    CHECK(ids(layers_at(tl, 0.5)) == Ids{"b", "a"});
}

TEST_CASE("capas: el render solo dibuja clips que encajan en su pista", "[layers]") {
    Timeline tl;
    tl.tracks = {track("V1"), track("A1", "audio"), track("T1", "text")};
    tl.clips = {clip("v", "V1", 0, 2), clip("va", "A1", 0, 2), clip("tv", "V1", 0, 2, "text"),
                clip("t", "T1", 0, 2, "text"), clip("img_t", "T1", 0, 2, "image")};
    CHECK(ids(layers_at(tl, 1)) == Ids{"v", "t"});
    // El preview no lo mira: el de la pista de audio va al fondo (capa -1).
    CHECK(ids(layers_at(tl, 1, kPreviewLayers)) == Ids{"va", "v", "tv", "t", "img_t"});
}

TEST_CASE("capas: con ids de pista repetidos manda la última, también su tipo", "[layers]") {
    Timeline tl;
    tl.tracks = {track("V1"), track("V1", "audio")};
    tl.clips = {clip("v", "V1", 0, 2)};
    CHECK(ids(layers_at(tl, 1)).empty()); // la pista «V1» que vale es la de audio
    CHECK(ids(layers_at(tl, 1, kPreviewLayers)) == Ids{"v"});
}

TEST_CASE("capas: el render no dibuja clips de 0,02 s o menos, como el export", "[layers]") {
    Timeline tl;
    tl.tracks = {track("V1"), track("V2")};
    tl.clips = {clip("corto", "V1", 1, 0.02), clip("justo", "V2", 1, 0.021)};
    CHECK(ids(layers_at(tl, 1.01)) == Ids{"justo"});
    CHECK(ids(layers_at(tl, 1.01, kPreviewLayers)) == Ids{"corto", "justo"});
}

TEST_CASE("capas: un clip de duración 0 no se ve", "[layers]") {
    Timeline tl;
    tl.tracks = {track("V1")};
    tl.clips = {clip("z", "V1", 1, 0)};
    CHECK(ids(layers_at(tl, 1.0)).empty());
    CHECK(ids(layers_at(tl, 0.99)).empty());
    // El preview sí lo enseña en [inicio − 0,02, inicio): rareza del margen.
    CHECK(ids(layers_at(tl, 0.99, kPreviewLayers)) == Ids{"z"});
}
