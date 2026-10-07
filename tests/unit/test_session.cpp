// Sesión de edición: snapshots inmutables, estructura compartida, deshacer/rehacer,
// grupos (un paso de deshacer) y observadores.

#include <catch2/catch_test_macros.hpp>

#include <memory>

#include "gc/core/edit.hpp"
#include "gc/core/serialize.hpp"
#include "gc/core/session.hpp"

using gc::Json;
using namespace gc::core;

namespace {

Json clip(const char* id, const char* track, double start, double out) {
    return {{"id", id},   {"track_id", track},  {"kind", "video"},  {"asset_kind", "clips"},
            {"asset_id", id}, {"filename", "a.mp4"}, {"start", start}, {"out_point", out}};
}

Timeline sample() {
    const Json raw = {{"schema_version", 5},
                      {"tracks", Json::array({{{"id", "V1"}, {"kind", "video"}, {"name", "V1"}},
                                              {{"id", "V2"}, {"kind", "video"}, {"name", "V2"}}})},
                      {"clips", Json::array({clip("a", "V1", 0, 5), clip("b", "V1", 5, 3), clip("c", "V2", 0, 4)})}};
    auto tl = timeline_from_json(raw);
    REQUIRE(tl);
    return *tl;
}

EditSession session() { return EditSession(sample(), std::make_unique<SequentialIdSource>()); }

const TimelineClip& find(const Timeline& tl, std::string_view id) {
    for (const auto& c : tl.clips) {
        if (c->id == id) {
            return *c;
        }
    }
    FAIL("no está el clip " << id);
    throw;
}

} // namespace

TEST_CASE("una operación comparte con la anterior los clips que no cambian", "[session]") {
    const Timeline before = sample();
    SequentialIdSource ids;
    const auto res = apply_command(before, ops::MoveClip{"c", 10.0, std::nullopt}, ids);
    REQUIRE(res);
    // a y b no cambian: son el MISMO objeto en las dos timelines.
    CHECK(res->timeline.clips[0].same_object(before.clips[0]));
    CHECK(res->timeline.clips[1].same_object(before.clips[1]));
    // c cambió: copia nueva, y la timeline de entrada sigue intacta.
    CHECK_FALSE(res->timeline.clips[2].same_object(before.clips[2]));
    CHECK(before.clips[2]->start == 0.0);
    CHECK(res->timeline.clips[2]->start == 10.0);
}

TEST_CASE("deshacer y rehacer intercambian snapshots", "[session]") {
    EditSession s = session();
    const TimelineSnapshot original = s.timeline();
    REQUIRE(s.apply("move_clip", {{"clip_id", "a"}, {"start", 20}}));
    REQUIRE(s.apply("remove_clip", {{"clip_id", "c"}}));
    CHECK(s.timeline()->clips.size() == 2);

    CHECK(s.undo());
    CHECK(s.timeline()->clips.size() == 3);
    CHECK(find(*s.timeline(), "a").start == 20.0);
    CHECK(s.undo());
    CHECK(s.timeline() == original); // literalmente el snapshot de antes
    CHECK_FALSE(s.undo());

    CHECK(s.redo());
    CHECK(find(*s.timeline(), "a").start == 20.0);
    // Una edición nueva invalida el rehacer.
    REQUIRE(s.apply("rename_track", {{"track_id", "V1"}, {"name", "Principal"}}));
    CHECK_FALSE(s.can_redo());
}

TEST_CASE("los snapshots publicados no cambian aunque se siga editando", "[session]") {
    EditSession s = session();
    const TimelineSnapshot held = s.timeline(); // lo que tendría el hilo de render
    const Json frozen = to_json(*held);
    REQUIRE(s.apply("move_clip", {{"clip_id", "b"}, {"start", 30}}));
    REQUIRE(s.apply("split_clip", {{"clip_id", "a"}, {"at_time", 2}}));
    CHECK(to_json(*held) == frozen);
}

TEST_CASE("un grupo es un solo paso de deshacer", "[session]") {
    EditSession s = session();
    const TimelineSnapshot original = s.timeline();
    {
        EditSession::Group drag(s);
        for (int i = 1; i <= 10; ++i) { // arrastrar: muchas posiciones intermedias
            REQUIRE(s.apply("move_clip", {{"clip_id", "c"}, {"start", i}}));
        }
        drag.commit();
    }
    CHECK(find(*s.timeline(), "c").start == 10.0);
    CHECK(s.undo());
    CHECK(s.timeline() == original);
    CHECK_FALSE(s.can_undo());
}

TEST_CASE("un grupo sin commit vuelve atrás", "[session]") {
    EditSession s = session();
    const TimelineSnapshot original = s.timeline();
    {
        EditSession::Group g(s);
        REQUIRE(s.apply("remove_clip", {{"clip_id", "a"}}));
    }
    CHECK(s.timeline() == original);
    CHECK_FALSE(s.can_undo());
}

TEST_CASE("los observadores reciben cada cambio con su revisión", "[session]") {
    EditSession s = session();
    std::vector<Change> seen;
    const auto id = s.subscribe([&](const TimelineSnapshot&, const Change& c) { seen.push_back(c); });
    REQUIRE(s.apply("move_clip", {{"clip_id", "a"}, {"start", 50}}));
    CHECK(s.undo());
    s.unsubscribe(id);
    REQUIRE(s.apply("remove_clip", {{"clip_id", "a"}}));

    REQUIRE(seen.size() == 2);
    CHECK(seen[0].cause == "move_clip");
    CHECK(seen[0].changed == std::vector<std::string>{"a"});
    CHECK(seen[0].revision == 1);
    CHECK(seen[1].cause == "undo");
    CHECK(seen[1].revision == 2);
    CHECK(s.revision() == 3);
}

TEST_CASE("una operación fallida no cambia nada", "[session]") {
    EditSession s = session();
    const TimelineSnapshot original = s.timeline();
    CHECK_FALSE(s.apply("move_clip", {{"clip_id", "zz"}, {"start", 1}}));
    CHECK_FALSE(s.apply("volar", Json::object()));
    CHECK_FALSE(s.apply("move_clip", {{"clip_id", "a"}, {"desconocido", 1}}));
    CHECK(s.timeline() == original);
    CHECK(s.revision() == 0);
    CHECK_FALSE(s.can_undo());
}

TEST_CASE("checkpoints con nombre", "[session]") {
    EditSession s = session();
    s.checkpoint("antes");
    REQUIRE(s.apply("remove_clip", {{"clip_id", "a"}}));
    REQUIRE(s.apply("remove_clip", {{"clip_id", "b"}}));
    REQUIRE(s.restore_checkpoint("antes"));
    CHECK(s.timeline()->clips.size() == 3);
    CHECK(s.undo()); // restaurar también se deshace
    CHECK(s.timeline()->clips.size() == 1);
    CHECK_FALSE(s.restore_checkpoint("no-existe"));
    CHECK(s.checkpoints() == std::vector<std::string>{"antes"});
}

TEST_CASE("historial limitado", "[session]") {
    EditSession s(sample(), std::make_unique<SequentialIdSource>(), 3);
    for (int i = 0; i < 5; ++i) {
        REQUIRE(s.apply("move_clip", {{"clip_id", "c"}, {"start", 10 + i}}));
    }
    int undos = 0;
    while (s.undo()) {
        ++undos;
    }
    CHECK(undos == 3);
}
