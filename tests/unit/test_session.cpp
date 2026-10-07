// Sesión de edición: snapshots inmutables, estructura compartida, deshacer/rehacer,
// grupos (un paso de deshacer) y observadores.

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <memory>
#include <fstream>
#include <random>

#include "gc/core/edit.hpp"
#include "gc/core/history_store.hpp"
#include "gc/core/io.hpp"
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

// --- Historial en disco y autoguardado ----------------------------------------------------

namespace {

// Carpeta temporal propia de cada test; se borra al terminar.
struct TempDir {
    std::filesystem::path path;
    TempDir() {
        std::random_device rd;
        path = std::filesystem::temp_directory_path() / ("gc_tests_" + std::to_string(rd()));
        std::filesystem::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

} // namespace

TEST_CASE("autoguardado: historial tras cada paso y timeline tras cada cambio", "[session][history]") {
    TempDir dir;
    const auto file = history_path(dir.path, "proyecto");
    EditSession s = session();
    std::vector<Json> saved;
    Autosave autosave(s, file, [&](const Timeline& tl) -> gc::Result<void> {
        saved.push_back(to_json(tl));
        return {};
    });

    REQUIRE(s.apply("move_clip", {{"clip_id", "c"}, {"start", 10}}));
    CHECK(saved.size() == 1);
    CHECK(saved.back() == to_json(*s.timeline()));
    CHECK(load_history(file).undo_stack().size() == 1);

    s.checkpoint("antes");                         // solo cambia el historial
    CHECK(saved.size() == 1);
    CHECK(load_history(file).checkpoints() == std::vector<std::string>{"antes"});

    {
        EditSession::Group g(s);                   // dos ops, un paso de deshacer
        REQUIRE(s.apply("move_clip", {{"clip_id", "c"}, {"start", 20}}));
        REQUIRE(s.apply("move_clip", {{"clip_id", "c"}, {"start", 30}}));
        g.commit();
    }
    CHECK(saved.size() == 3);
    CHECK(load_history(file).undo_stack().size() == 2);

    REQUIRE(s.undo());
    const History on_disk = load_history(file);
    CHECK(on_disk.undo_stack().size() == 1);
    CHECK(on_disk.redo_stack().size() == 1);
    CHECK(history_to_json(on_disk) == history_to_json(s.history()));
    CHECK_FALSE(autosave.last_error());
}

TEST_CASE("autoguardado: un fallo al guardar no para la edición y se recuerda", "[session][history]") {
    TempDir dir;
    EditSession s = session();
    bool fail_next = true;
    Autosave autosave(s, history_path(dir.path, "p"), [&](const Timeline&) -> gc::Result<void> {
        if (fail_next) {
            return gc::fail(gc::ErrorCode::Io, "disco lleno");
        }
        return {};
    });
    REQUIRE(s.apply("move_clip", {{"clip_id", "c"}, {"start", 10}}));
    REQUIRE(autosave.last_error());
    CHECK(autosave.last_error()->message == "disco lleno");
    fail_next = false;
    REQUIRE(s.apply("move_clip", {{"clip_id", "c"}, {"start", 11}}));
    CHECK_FALSE(autosave.last_error());
}

TEST_CASE("retomar una sesión con su historial guardado", "[session][history]") {
    TempDir dir;
    const auto file = history_path(dir.path, "p");
    Json first;
    {
        EditSession s = session();
        first = to_json(*s.timeline());
        Autosave autosave(s, file);
        REQUIRE(s.apply("move_clip", {{"clip_id", "c"}, {"start", 10}}));
        s.checkpoint("uno");
    }
    // Otra sesión, más tarde: la timeline guardada y su historial.
    EditSession again(sample(), load_history(file), std::make_unique<SequentialIdSource>());
    CHECK(again.can_undo());
    CHECK(again.checkpoints() == std::vector<std::string>{"uno"});
    REQUIRE(again.undo());
    CHECK(to_json(*again.timeline()) == first);
}

TEST_CASE("historial ilegible o ausente: vacío, sin error", "[session][history]") {
    TempDir dir;
    CHECK_FALSE(load_history(dir.path / "no-existe.json").can_undo());
    const auto bad = dir.path / "roto.json";
    {
        std::ofstream(bad) << "{ esto no es json";
    }
    CHECK_FALSE(load_history(bad).can_undo());
    CHECK(load_history(bad).limit() == History::kDefaultLimit);
}

