// El ViewModel del editor (apps/studio/editor_model.hpp): lo que hace cada gesto de la timeline y
// que el resultado se guarda en projects.json.

#include <QAbstractItemModel>
#include <QCoreApplication>
#include <filesystem>
#include <string>

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
    std::filesystem::path dir = std::filesystem::temp_directory_path() /
                                ("gc_editor_model_" + std::to_string(QCoreApplication::applicationPid()));
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
        if (v.toMap()["clipId"] == id) {
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

// Filas de la fixture en pantalla (bajo la regla de 22 px): V2 22–74, V1 74–126 y A1 126–178.
TEST_CASE("editor: arrastrar como en React (hueco, pista vecina, pista nueva y grupo) y se guarda",
          "[studio]") {
    Fixture f;
    studio::EditorModel m(f.store);
    REQUIRE(m.openProject("p1"));
    CHECK(m.previewDrag("b", -1.5, 100)["start"] == 4.5);
    const QVariantMap up = m.previewDrag("b", -4, 100); // encima de «a» y lejos del hueco: sube a V2
    CHECK(up["trackId"] == "V2");
    CHECK(up["start"] == 2.0);
    const QVariantMap fresh = m.previewDrag("b", 0, 150); // un vídeo sobre el audio: pista nueva
    CHECK(fresh["newTrack"] == true);
    CHECK(fresh["lineY"] == 126.0);

    m.dropDrag("b", -4, 100);
    CHECK(clip_view(m, "b")["trackId"] == "V2");
    CHECK(start_of(f.saved(), "b") == 2);
    m.undo();
    CHECK(start_of(f.saved(), "b") == 6);

    m.dropDrag("b", 0, 150);
    CHECK(m.tracks().size() == 4);
    const QString made = clip_view(m, "b")["trackId"].toString();
    CHECK(made.startsWith("V3-"));
    m.dropDrag("b", 0, 100); // vuelve a V1: la pista nueva se queda vacía y desaparece
    CHECK(m.tracks().size() == 3);

    m.select("a");
    m.select("b", true);
    const QVariantMap group = m.previewDrag("a", 10, 100);
    CHECK(group["group"] == true);
    CHECK(group["delta"] == 10.0);
    m.dropDrag("a", 10, 100);
    CHECK(start_of(f.saved(), "a") == 10);
    CHECK(start_of(f.saved(), "b") == 16);
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

TEST_CASE("editor: mover un clip solo avisa de su fila (QML no recrea los delegados)", "[studio]") {
    Fixture f;
    studio::EditorModel m(f.store);
    REQUIRE(m.openProject("p1"));
    auto* rows = qobject_cast<QAbstractItemModel*>(m.clipModel());
    REQUIRE(rows);
    int resets = 0;
    int changed = 0;
    QObject::connect(rows, &QAbstractItemModel::modelReset, [&] { ++resets; });
    QObject::connect(rows, &QAbstractItemModel::dataChanged, [&] { ++changed; });
    m.dropDrag("b", -1.5, 100);
    m.dropDrag("b", 0, 48); // a otra pista: el orden de las filas es el del array, no cambia
    CHECK(resets == 0);
    CHECK(changed == 2);
    m.select("b");
    m.deleteSelection();
    CHECK(resets == 1);
}

TEST_CASE("editor: cabecera de pista (ver, bloquear, nombre, juntar y orden)", "[studio]") {
    Fixture f;
    studio::EditorModel m(f.store);
    REQUIRE(m.openProject("p1"));
    const auto track_view = [&](const QString& id) {
        for (const QVariant& t : m.tracks()) {
            if (t.toMap()["id"] == id) {
                return t.toMap();
            }
        }
        return QVariantMap{};
    };
    m.toggleTrack("V1", "hidden");
    CHECK(track_view("V1")["hidden"] == true);
    m.renameTrack("V1", "  Principal ");
    CHECK(track_view("V1")["name"] == "Pri");
    m.compactTrack("V1"); // a (0–4) y b (6–9): b pasa a 4
    CHECK(clip_view(m, "b")["start"] == 4.0);
    m.toggleTrack("V1", "locked");
    m.compactTrack("V1");
    CHECK(track_view("V1")["locked"] == true);
    m.reorderTrack("V1", "V2", false); // V1 encima de V2 en pantalla
    QStringList order;
    for (const QVariant& t : m.tracks()) {
        order << t.toMap()["id"].toString();
    }
    CHECK(order == QStringList{"V1", "V2", "A1"});
    m.reorderTrack("A1", "V2", false); // el audio no entra en la pila
    CHECK(m.tracks().last().toMap()["id"] == "A1");
}

TEST_CASE("editor: inspector (opacidad y volumen sin animar, fusión, transiciones y nota)", "[studio]") {
    Fixture f;
    studio::EditorModel m(f.store);
    REQUIRE(m.openProject("p1"));
    m.select("a");
    m.select("b", true);
    m.setPose({{"opacity", 0.4}});
    CHECK(m.selectedClip()["poseOpacity"] == 0.4);
    CHECK_FALSE(m.selectedClip()["animated"].toBool()); // mover ≠ animar: sin keyframes
    m.setBlend("screen");
    m.setTransition("fade", "pop");
    const core::Timeline tl = f.saved();
    for (const auto& c : tl.clips) {
        if (c->id == "a" || c->id == "b") {
            CHECK(c->opacity == 0.4);
            CHECK(c->blend_mode == "screen");
            CHECK(c->appear == "fade");
            CHECK(c->exit == "pop");
        }
    }
    m.setBlend("normal");
    CHECK(m.selectedClip()["blend"] == "normal");
    m.select("m");
    m.setPose({{"volume", 1.6}});
    CHECK(m.selectedClip()["volumeAt"] == 1.6);
    m.setNote("  el momento clave ");
    CHECK(m.selectedClip()["note"] == "el momento clave");
    m.undo();
    CHECK(m.selectedClip()["note"] == "");
}

TEST_CASE("editor: copiar, cortar y pegar en el cabezal (un paso de deshacer)", "[studio]") {
    Fixture f;
    studio::EditorModel m(f.store);
    REQUIRE(m.openProject("p1"));
    CHECK_FALSE(m.paste());
    m.select("a");
    m.select("b", true); // a en 0 y b en 6: separación de 6 s
    REQUIRE(m.copySelection());
    m.setPlayhead(20);
    REQUIRE(m.paste());
    REQUIRE(m.selection().size() == 2);
    const QVariantMap first = clip_view(m, m.selection()[0]);
    const QVariantMap second = clip_view(m, m.selection()[1]);
    CHECK(first["start"] == 20.0);
    CHECK(second["start"] == 26.0);
    CHECK(first["trackId"] == "V1");
    m.undo();
    CHECK(m.clips().size() == 3);
    m.select("m");
    m.cutSelection();
    CHECK(m.clips().size() == 2);
    m.setPlayhead(1);
    REQUIRE(m.paste()); // la música vuelve, ahora en 1 s
    CHECK(clip_view(m, m.selection()[0])["start"] == 1.0);
}

TEST_CASE("editor: rombo de keyframe, fundidos y silenciar", "[studio]") {
    Fixture f;
    studio::EditorModel m(f.store);
    REQUIRE(m.openProject("p1"));
    m.select("a");
    m.setPlayhead(1);
    CHECK(m.selectedClip()["kf"] == "off");
    m.toggleKeyframe();
    CHECK(m.selectedClip()["kf"] == "on");
    m.setPlayhead(3);
    CHECK(m.selectedClip()["kf"] == "empty");
    m.setPose({{"opacity", 0.2}}); // ya animado: cae en un keyframe nuevo
    CHECK(m.selectedClip()["kf"] == "on");
    m.setPlayhead(1);
    CHECK(m.selectedClip()["poseOpacity"] == 1.0);
    m.toggleKeyframe(); // quita el de 1 s
    CHECK(m.selectedClip()["kf"] == "empty");

    m.select("m");
    m.fadeVolume("in");
    m.setPlayhead(0);
    CHECK(m.selectedClip()["volumeAt"] == 0.0);
    m.setPlayhead(0.5);
    CHECK(m.selectedClip()["volumeAt"] == 1.0);
    m.toggleMuted();
    CHECK(m.selectedClip()["muted"] == true);
}
