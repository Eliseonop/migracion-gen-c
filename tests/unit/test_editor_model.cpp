// El ViewModel del editor (apps/studio/editor_model.hpp): lo que hace cada gesto de la timeline y
// que el resultado se guarda en projects.json.

#include <filesystem>

#include <catch2/catch_test_macros.hpp>

#include "gc/core/io.hpp"
#include "gc/core/serialize.hpp"

#include "editor_model.hpp"

using namespace gc;

namespace {

Json clip(const char* id, const char* track, const char* kind, double start, double out) {
    return {{"id", id},         {"track_id", track},
            {"kind", kind},     {"asset_kind", kind == std::string("audio") ? "audios" : "clips"},
            {"asset_id", id},   {"filename", "a.mp4"},
            {"start", start},   {"in_point", 0.0},
            {"out_point", out}, {"source_duration", 20.0}};
}

struct Fixture {
    std::filesystem::path dir = std::filesystem::temp_directory_path() / "gc_editor_model";
    core::ProjectsStore store{dir / "data" / "projects.json", dir / "clips"};

    Fixture() {
        std::filesystem::remove_all(dir);
        const Json tracks = Json::array({{{"id", "V1"}, {"kind", "video"}, {"name", "V1"}},
                                         {{"id", "V2"}, {"kind", "video"}, {"name", "V2"}},
                                         {{"id", "A1"}, {"kind", "audio"}, {"name", "A1"}}});
        const Json tl = {
            {"schema_version", 5},
            {"tracks", tracks},
            {"clips", Json::array({clip("a", "V1", "video", 0, 4), clip("b", "V1", "video", 6, 3),
                                   clip("m", "A1", "audio", 0, 10)})}};
        const Json data = {
            {"projects",
             Json::array({{{"id", "p1"},
                           {"name", "Uno"},
                           {"created_at", "2026-10-19T10:00:00+00:00"},
                           {"timeline", tl}},
                          {{"id", "p2"}, {"name", "Vacío"}, {"created_at", "2026-10-19T10:00:00+00:00"}}})}};
        REQUIRE(core::write_json_file(dir / "data" / "projects.json", data));
    }
    ~Fixture() { std::filesystem::remove_all(dir); }

    [[nodiscard]] core::Timeline saved() {
        auto p = store.get_project("p1");
        REQUIRE(p);
        REQUIRE(*p);
        REQUIRE((*p)->timeline);
        return *(*p)->timeline;
    }
};

double start_of(const core::Timeline& tl, std::string_view id) {
    for (const auto& c : tl.clips) {
        if (c->id == id) {
            return c->start;
        }
    }
    FAIL("no está " << id);
    return 0;
}

QVariantMap clip_view(const studio::EditorModel& m, const QString& id) {
    for (const QVariant& v : m.clips()) {
        if (v.toMap()["id"] == id) {
            return v.toMap();
        }
    }
    return {};
}

} // namespace

TEST_CASE("editor: abre un proyecto; sin timeline, con las pistas de React", "[studio]") {
    Fixture f;
    studio::EditorModel m(f.store);
    REQUIRE(m.openProject("p2"));
    QStringList ids;
    for (const QVariant& t : m.tracks()) {
        ids << t.toMap()["id"].toString();
    }
    CHECK(ids == QStringList{"V2", "V1", "A1", "A2"}); // la pila al revés y luego el audio
    REQUIRE(m.openProject("p1"));
    CHECK(m.clips().size() == 3);
    CHECK(m.duration() == 10);
    CHECK_FALSE(m.openProject("nada"));
    CHECK(m.error() == "Proyecto no encontrado.");
}

TEST_CASE("editor: arrastrar se pega al vecino, cambia de pista compatible y se guarda", "[studio]") {
    Fixture f;
    studio::EditorModel m(f.store);
    REQUIRE(m.openProject("p1"));
    CHECK(m.previewMove("b", 2, "V1")["start"] == 4.0);    // encima de «a»: al hueco libre más cercano
    CHECK(m.previewMove("b", 2, "A1")["trackId"] == "V1"); // el audio no admite vídeo
    m.moveClip("b", 1, "V2");
    CHECK(clip_view(m, "b")["trackId"] == "V2");
    CHECK(start_of(f.saved(), "b") == 1);
    m.undo();
    CHECK(start_of(f.saved(), "b") == 6);
    m.redo();
    CHECK(start_of(f.saved(), "b") == 1);
}

TEST_CASE("editor: recortar no invade al vecino ni pasa del material", "[studio]") {
    Fixture f;
    studio::EditorModel m(f.store);
    REQUIRE(m.openProject("p1"));
    CHECK(m.previewTrim("a", false, 5)["duration"] == 6.0); // hasta «b» (en 6)
    CHECK(m.previewTrim("b", true, -10)["start"] == 6.0);   // ya empieza en el 0 del material
    m.trimClip("b", false, 100);
    CHECK(clip_view(m, "b")["duration"] == 20.0); // hasta el final del material
    m.trimClip("a", true, 1);
    const core::Timeline tl = f.saved();
    CHECK(start_of(tl, "a") == 1);
    CHECK(m.canUndo());
}

TEST_CASE("editor: selección, duplicar y borrar (un paso de deshacer cada uno)", "[studio]") {
    Fixture f;
    studio::EditorModel m(f.store);
    REQUIRE(m.openProject("p1"));
    m.select("a");
    m.select("b", true);
    CHECK(m.selection() == QStringList{"a", "b"});
    m.select("m", true);
    CHECK(m.selection() == QStringList{"m"}); // Ctrl con otro tipo empieza de nuevo
    m.select("a");
    m.select("b", false, true);
    CHECK(m.selection() == QStringList{"a", "b"}); // Shift: rango en la pista
    m.duplicateSelection();
    CHECK(m.clips().size() == 5);
    m.deleteSelection();
    CHECK(m.clips().size() == 3);
    CHECK(m.selection().isEmpty());
    m.undo();
    CHECK(m.clips().size() == 5);
}

TEST_CASE("editor: cortar en el cabezal solo corta la selección, que pasa a la mitad izquierda", "[studio]") {
    Fixture f;
    studio::EditorModel m(f.store);
    REQUIRE(m.openProject("p1"));
    m.setPlayhead(2);
    m.splitAtPlayhead();
    CHECK(m.clips().size() == 3); // sin selección no corta nada
    m.select("a");
    m.select("m", false, true);
    m.splitAtPlayhead();
    CHECK(m.clips().size() == 5);
    REQUIRE(m.selection().size() == 2);
    for (const QString& id : m.selection()) {
        CHECK(clip_view(m, id)["start"] == 0.0);
        CHECK(clip_view(m, id)["duration"] == 2.0);
    }
    m.undo();
    CHECK(m.clips().size() == 3);
}
