// El ViewModel del editor (apps/studio/editor_model.hpp): lo que hace cada gesto de la timeline y
// que el resultado se guarda en projects.json.

#include <QAbstractItemModel>
#include <QCoreApplication>
#include <filesystem>
#include <fstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "gc/core/io.hpp"
#include "gc/core/fx_catalog.hpp"
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

TEST_CASE("editor: transformación y velocidad desde el inspector", "[studio]") {
    Fixture f;
    studio::EditorModel m(f.store);
    REQUIRE(m.openProject("p1"));
    m.select("a");
    const auto tf = [&] { return m.selectedClip()["transform"].toMap(); };
    CHECK(tf()["zoomScale"].toBool()); // llena el cuadro: X/Y y escala son el encuadre sobre la fuente
    CHECK_FALSE(tf()["capcut"].toBool());
    m.setPosition(30, 70);
    CHECK(tf()["x"] == 30);
    CHECK(tf()["y"] == 70);
    m.setScalePct(200);
    CHECK(tf()["scalePct"] == 200);
    m.setRotation(45);
    CHECK(tf()["degrees"] == 45);
    m.resetTransform();
    CHECK(tf()["x"] == 50);
    CHECK(tf()["scalePct"] == 100);
    CHECK(tf()["degrees"] == 0);

    m.toggleFlip("h");
    CHECK(m.selectedClip()["flipH"] == true);
    m.toggleFlip("h");
    CHECK(m.selectedClip()["flipH"] == false);
    m.setSpeed(2);
    CHECK(m.selectedClip()["duration"] == 2);
    m.setSpeed(50);
    CHECK(m.selectedClip()["speed"] == 10);
    m.toggleReverse();
    m.toggleKeepPitch();
    const core::Timeline tl = f.saved();
    for (const auto& c : tl.clips) {
        if (c->id == "a") {
            CHECK(c->speed == 10);
            CHECK(c->reverse);
            CHECK_FALSE(c->keep_pitch);
        }
    }
    m.select("m");
    CHECK_FALSE(m.selectedClip()["transform"].toMap()["show"].toBool()); // el audio no tiene transformación
}

TEST_CASE("editor: añadir un texto crea su pista y se edita desde el inspector", "[studio]") {
    Fixture f;
    studio::EditorModel m(f.store);
    REQUIRE(m.openProject("p1"));
    m.setPlayhead(2);
    m.addText();
    const QVariantMap made = m.selectedClip();
    REQUIRE(made["kind"] == "text");
    CHECK(made["start"] == 2.0);
    CHECK(made["duration"] == 8.0); // del cabezal al final de la timeline (10 s)
    CHECK(made["text"] == "Texto predeterminado");
    CHECK(made["style"].toMap()["y"] == 0.86); // la caja de los subtítulos
    const QString tid = made["trackId"].toString();
    CHECK(tid.startsWith("T1-"));

    m.changeText("Hola mundo");
    m.changeStyle({{"color", "#ff0000"}, {"size", 0.05}});
    core::Timeline tl = f.saved();
    const auto it = std::ranges::find_if(tl.clips, [](const auto& c) { return c->kind == "text"; });
    REQUIRE(it != tl.clips.end());
    CHECK((*it)->text == "Hola mundo");
    CHECK((*it)->name == "Hola mundo");
    CHECK((*it)->style->at("color") == "#ff0000");
    CHECK_FALSE((*it)->keyframes); // sin animar: solo el estilo fijo

    m.setPlayhead(5); // con el color animado, el cambio cae en un keyframe del cabezal
    core::Timeline animated = f.saved();
    for (auto& c : animated.clips) {
        if (c->kind == "text") {
            c.mut().keyframes = Json{{"enabled", true}, {"items", Json::array({{{"id", "k1"}, {"t", 0}, {"props", {{"color", "#00ff00"}}}}})}};
        }
    }
    REQUIRE(f.store.save_timeline("p1", core::to_json(animated)));
    REQUIRE(m.openProject("p1"));
    m.select(made["id"].toString());
    m.setPlayhead(5);
    m.changeStyle({{"color", "#0000ff"}});
    CHECK(m.selectedClip()["style"].toMap()["color"] == "#0000ff");
    tl = f.saved();
    for (const auto& c : tl.clips) {
        if (c->kind == "text") {
            CHECK(c->keyframes->at("items").size() == 2);
        }
    }
    m.setPlayhead(2);
    CHECK(m.selectedClip()["style"].toMap()["color"] == "#00ff00");

    m.setPlayhead(10);
    m.addText(); // con un texto seleccionado, en su misma pista (si se solapara, settle lo subiría a otra)
    CHECK(m.selectedClip()["trackId"] == tid);
}

TEST_CASE("editor: figuras y capa de ajuste en el cabezal", "[studio]") {
    Fixture f;
    studio::EditorModel m(f.store);
    REQUIRE(m.openProject("p1"));
    m.setPlayhead(1);
    m.addShape("star", "Estrella");
    QVariantMap made = m.selectedClip();
    CHECK(made["kind"] == "shape");
    CHECK(made["label"] == "Estrella");
    CHECK(made["duration"] == 5.0);
    CHECK(made["trackId"] != "V1"); // en V1 hay un clip a esa altura: settle la sube
    m.addAdjustment(); // la pista de vídeo de arriba está ocupada: una nueva encima
    made = m.selectedClip();
    CHECK(made["kind"] == "adjustment");
    CHECK(made["start"] == 1.0);
    m.undo();
    m.select(m.clips().back().toMap()["clipId"].toString());
    m.changeShape({{"fill", "#00ff00"}, {"sides", 7}});
    CHECK(m.selectedClip()["shape"].toMap()["fill"] == "#00ff00");
    CHECK(m.selectedClip()["shape"].toMap()["sides"] == 7);
    m.undo();
    m.undo();
    CHECK(m.clips().size() == 3);
}

TEST_CASE("editor: la velocidad de un audio estira sus subtítulos enlazados", "[studio]") {
    Fixture f;
    core::Timeline tl = f.saved();
    tl.tracks.push_back({});
    tl.tracks.back().id = "T1";
    tl.tracks.back().kind = "text";
    tl.tracks.back().name = "T1";
    for (auto& t : tl.tracks) {
        if (t.id == "A1") {
            t.linked_track_id = "T1";
        }
    }
    auto sub = core::clip_from_json({{"id", "s"}, {"track_id", "T1"}, {"kind", "text"}, {"asset_kind", "text"},
                                     {"asset_id", "s"}, {"filename", ""}, {"start", 4.0}, {"in_point", 0.0}, {"out_point", 2.0},
                                     {"text", "hola"}});
    REQUIRE(sub);
    tl.clips.emplace_back(std::move(*sub));
    REQUIRE(f.store.save_timeline("p1", core::to_json(tl)));
    studio::EditorModel m(f.store);
    REQUIRE(m.openProject("p1"));
    m.select("m");
    m.setSpeed(2);
    m.select("s");
    CHECK(m.selectedClip()["start"] == 2.0); // el audio empieza en 0: todo a la mitad
    CHECK(m.selectedClip()["duration"] == 1.0);
}

TEST_CASE("editor: temas de subtítulos y estilos preestablecidos de un texto", "[studio]") {
    Fixture f;
    studio::EditorModel m(f.store);
    REQUIRE(m.openProject("p1"));
    CHECK(studio::EditorModel::textThemes().size() > 20);
    m.addText();
    m.changeStyle({{"x", 0.3}});
    m.applyTextTheme("neon");
    QVariantMap st = m.selectedClip()["style"].toMap();
    CHECK(m.selectedClip()["themeId"] == "neon");
    CHECK(st["font"] == "Segoe UI Black");
    CHECK(st["x"] == 0.3); // el tema conserva la caja
    m.applyTextTheme("neon"); // otro clic en el mismo lo quita
    CHECK(m.selectedClip()["themeId"] == "");
    CHECK(m.selectedClip()["style"].toMap()["preset"] == "outline");
    m.applyTextLook("black-yellow");
    CHECK(m.selectedClip()["lookId"] == "black-yellow");
    m.applyTextLook("");
    CHECK(m.selectedClip()["lookId"] == "none");
    m.changeStyle({{"curve_on", true}, {"curve", 0.5}}, true);
    m.changeStyle({{"shadow_blur", 0.1}});
    CHECK(m.selectedClip()["style"].toMap()["curve_on"] == true);
    CHECK(m.selectedClip()["style"].toMap()["shadow_blur"] == 0.1);
    m.setPose({{"rot_x", 30}}); // texto 3D
    CHECK(m.selectedClip()["transform"].toMap()["rotX"] == 30);
}

TEST_CASE("editor: filtros de color apilados y ajustes de color", "[studio]") {
    Fixture f;
    studio::EditorModel m(f.store);
    REQUIRE(m.openProject("p1"));
    CHECK(studio::EditorModel::colorFilters().size() == 15);
    m.select("a");
    m.toggleFilter("sepia");
    m.toggleFilter("cool");
    m.setFilterAmount(1, 0.4);
    m.moveFilter(1, -1);
    QVariantList fs = m.selectedClip()["filters"].toList();
    REQUIRE(fs.size() == 2);
    CHECK(fs[0].toMap()["id"] == "cool");
    CHECK(fs[0].toMap()["amount"] == 0.4);
    m.toggleFilter("cool");
    CHECK(m.selectedClip()["filters"].toList().size() == 1);
    m.setEffect("exposure", 0.3);
    m.setEffect("hue", -45);
    CHECK(m.selectedClip()["effects"].toMap()["exposure"] == 0.3);
    CHECK(m.selectedClip()["effects"].toMap()["hue"] == -45);
    m.setEffect("vhs", true);
    m.setEffect("blur", 2);
    CHECK(m.selectedClip()["effects"].toMap()["vhs"] == true);
    CHECK(m.selectedClip()["effects"].toMap()["exposure"] == 0.3); // se suman a los que había
    const core::Timeline tl = f.saved();
    for (const auto& c : tl.clips) {
        if (c->id == "a") {
            CHECK(c->filters->size() == 1);
            CHECK(c->look == "none");
        }
    }
}

TEST_CASE("editor: efectos de sonido, fijos o en el keyframe del cabezal", "[studio]") {
    Fixture f;
    studio::EditorModel m(f.store);
    REQUIRE(m.openProject("p1"));
    m.select("m");
    m.setAudioFx("reverb", 0.6);
    CHECK(m.selectedClip()["audioFx"].toMap()["reverb"] == 0.6);
    CHECK(f.saved().clips.back()->audio_fx->at("reverb") == 0.6);
    m.setPlayhead(2);
    m.toggleKeyframe(); // con keyframes, el cambio cae en el del cabezal
    m.setPlayhead(5);
    m.setAudioFx("reverb", 1.5);
    CHECK(m.selectedClip()["audioFx"].toMap()["reverb"] == 1.0);
    m.setPlayhead(2);
    CHECK(m.selectedClip()["audioFx"].toMap()["reverb"] == 0.6);
}

TEST_CASE("editor: los beats del clip como fracción de su ancho", "[studio]") {
    Fixture f;
    core::Timeline tl = f.saved();
    for (auto& c : tl.clips) {
        if (c->id == "m") {
            c.mut().beats = Json{{"times", Json::array({1.0, 2.5, 5.0, 12.0})}, {"bpm", 120}, {"every", 1}};
        }
    }
    REQUIRE(f.store.save_timeline("p1", core::to_json(tl)));
    studio::EditorModel m(f.store);
    REQUIRE(m.openProject("p1"));
    const QVariantList beats = clip_view(m, "m")["beats"].toList();
    REQUIRE(beats.size() == 3); // el de 12 s cae fuera del recorte (0–10)
    CHECK(beats[1].toDouble() == 0.25);
}

TEST_CASE("editor: copiar y pegar atributos entre clips", "[studio]") {
    Fixture f;
    studio::EditorModel m(f.store);
    REQUIRE(m.openProject("p1"));
    CHECK_FALSE(m.copyAttributes()); // sin selección
    m.select("a");
    m.setPose({{"opacity", 0.3}});
    m.setBlend("screen");
    m.toggleFlip("h");
    REQUIRE(m.copyAttributes());
    m.select("b");
    QStringList ids;
    for (const QVariant& g : m.pasteableGroups()) {
        ids << g.toMap()["id"].toString();
    }
    CHECK(ids.contains("blend"));
    CHECK_FALSE(ids.contains("style")); // solo entre textos o figuras
    m.pasteAttributes({"blend"});
    CHECK(m.selectedClip()["blend"] == "screen");
    CHECK(m.selectedClip()["poseOpacity"] == 0.3);
    CHECK(m.selectedClip()["flipH"] == false); // «Voltear» no se eligió
    m.undo();
    CHECK(m.selectedClip()["blend"] == "normal");
}

TEST_CASE("editor: copiar, pegar y duplicar el keyframe del cabezal", "[studio]") {
    Fixture f;
    studio::EditorModel m(f.store);
    REQUIRE(m.openProject("p1"));
    m.select("a");
    CHECK_FALSE(m.copyKeyframe()); // sin keyframes
    m.setPlayhead(1);
    m.toggleKeyframe();
    m.setPose({{"opacity", 0.25}});
    REQUIRE(m.copyKeyframe());
    m.setPlayhead(3);
    REQUIRE(m.pasteKeyframe());
    CHECK(m.selectedClip()["kf"] == "on");
    CHECK(m.selectedClip()["poseOpacity"] == 0.25);
    m.setPlayhead(2);
    CHECK(m.selectedClip()["curve"] == "linear"); // la del keyframe al que se llega (el de 3 s)
    m.setCurve("bezier");
    CHECK(m.selectedClip()["curve"] == "bezier");
    m.setPlayhead(3);
    REQUIRE(m.duplicateKeyframe()); // del cabezal (3 s) a 4 s
    m.setPlayhead(4);
    CHECK(m.selectedClip()["kf"] == "on");
    CHECK(m.selectedClip()["poseOpacity"] == 0.25);
}

TEST_CASE("editor: animaciones de clip del catálogo por ranura", "[studio]") {
    auto catalog = core::FxCatalog::load(core::path_from_utf8(GC_RESOURCES_DIR) / "fx" / "catalog.json");
    REQUIRE(catalog);
    core::set_fx_catalog(std::make_shared<const core::FxCatalog>(std::move(*catalog)));
    Fixture f;
    studio::EditorModel m(f.store);
    REQUIRE(m.openProject("p1"));
    REQUIRE(studio::EditorModel::animationSlots().size() == 4);
    m.select("a");
    m.setAnimation("in", "fade_in");
    CHECK(m.selectedClip()["animations"].toMap()["in"] == "fade_in");
    const core::Timeline tl = f.saved();
    CHECK(tl.clips.front()->animations->at("in") == Json{{"id", "fade_in"}, {"dur", 0.5}});
    m.setAnimationDur("in", 9);
    CHECK(m.selectedClip()["animations"].toMap()["inDur"] == 4.0); // hasta 4 s
    m.setAnimation("in", "zoom_in");
    CHECK(m.selectedClip()["animations"].toMap()["inDur"] == 4.0); // otra animación conserva la duración
    m.setAnimation("in", "");
    CHECK(m.selectedClip()["animations"].toMap().isEmpty());
    CHECK_FALSE(f.saved().clips.front()->animations);

    m.addVfx("glitch_rgb"); // la pila de efectos animados
    m.addVfx("nada");
    QVariantList vfx = m.selectedClip()["vfx"].toList();
    REQUIRE(vfx.size() == 1);
    CHECK(vfx[0].toMap()["name"] == "Glitch RGB");
    m.setVfxParam(0, "amount", 0.25);
    m.patchVfx(0, {{"enabled", false}, {"from", 1.0}});
    vfx = m.selectedClip()["vfx"].toList();
    CHECK(vfx[0].toMap()["params"].toMap()["amount"] == 0.25);
    CHECK(vfx[0].toMap()["enabled"] == false);
    m.removeVfx(0);
    CHECK_FALSE(f.saved().clips.front()->vfx);
}

TEST_CASE("editor: máscara del clip (añadir, cambiar de tipo, geometría y quitar)", "[studio]") {
    Fixture f;
    studio::EditorModel m(f.store);
    REQUIRE(m.openProject("p1"));
    m.select("a");
    m.addMask("circle");
    QVariantMap mask = m.selectedClip()["mask"].toMap();
    CHECK(mask["type"] == "circle");
    m.commitMask({{"mx", 0.3}, {"mfeather", 0.1}});
    m.changeMask({{"invert", true}});
    mask = m.selectedClip()["mask"].toMap();
    CHECK(mask["x"] == 0.3);
    CHECK(mask["feather"] == 0.1);
    CHECK(mask["invert"] == true);
    m.changeMask({{"type", "rectangle"}}); // otro tipo: su tamaño, conservando posición
    mask = m.selectedClip()["mask"].toMap();
    CHECK(mask["type"] == "rectangle");
    CHECK(mask["x"] == 0.3);
    m.duplicateMask();
    CHECK(m.selectedClip()["maskCount"] == 2);
    m.removeMask();
    m.removeMask();
    CHECK(m.selectedClip()["mask"].toMap().isEmpty());
    CHECK(f.saved().clips.front()->masks.empty());
}

TEST_CASE("editor: el estilo general de una pista de texto", "[studio]") {
    Fixture f;
    studio::EditorModel m(f.store);
    REQUIRE(m.openProject("p1"));
    m.addText();
    const QString tid = m.selectedClip()["trackId"].toString();
    m.setPlayhead(10);
    m.addText(); // dos textos en la pista
    m.selectTrack(tid);
    CHECK(m.selectedClip().isEmpty());
    CHECK(m.selectedTrack()["kind"] == "text");
    m.changeStyle({{"color", "#00ff00"}});
    m.applyTextTheme("fire");
    CHECK(m.selectedTrack()["themeId"] == "fire");
    int texts = 0;
    for (const auto& c : f.saved().clips) {
        if (c->kind == "text") {
            ++texts;
            CHECK(c->style->at("theme") == "fire");
        }
    }
    CHECK(texts == 2);
    m.select("a"); // elegir un clip suelta la pista
    CHECK(m.selectedTrack().isEmpty());
}

TEST_CASE("editor: material de la biblioteca y de la carpeta de SFX", "[studio]") {
    Fixture f;
    REQUIRE(core::write_json_file(
        f.dir / "data" / "library" / "library.json",
        {{"version", 1},
         {"items", Json::array({{{"id", "lib_1"}, {"resource_type", "audio"}, {"filename", "tema.mp3"},
                                 {"label", "Tema"}, {"duration", 3.0}}})}}));
    std::filesystem::create_directories(f.dir / "sfx" / "Golpes");
    { std::ofstream(f.dir / "sfx" / "Golpes" / "pum.wav") << "RIFF"; }
    std::filesystem::create_directories(f.dir / "material" / "Viajes");
    { std::ofstream(f.dir / "material" / "Viajes" / "playa.mp4") << "x"; }
    core::SettingsFile settings(f.dir / "data" / "settings.json");
    REQUIRE(settings.save({{"sfx_folder", core::path_to_utf8(f.dir / "sfx")},
                           {"material_root", core::path_to_utf8(f.dir / "material")}}));
    studio::EditorModel m(f.store, &settings);
    REQUIRE(m.openProject("p1"));
    QStringList sources;
    for (const QVariant& v : m.materials()) {
        sources << v.toMap()["source"].toString() + ":" + v.toMap()["name"].toString();
    }
    CHECK(sources.contains("library:Tema"));
    CHECK(sources.contains("sfx:pum"));
    CHECK(sources.contains("collection:playa"));
    REQUIRE(m.collections().size() == 1);
    CHECK(m.collections().front().toMap()["name"] == "Viajes");
    CHECK_FALSE(m.sfxCategories().isEmpty());
    m.searchSfx("nada que coincida", "");
    CHECK(std::ranges::none_of(m.materials(), [](const QVariant& v) { return v.toMap()["source"] == "sfx"; }));
    m.searchSfx("pum", "");
    CHECK(std::ranges::any_of(m.materials(), [](const QVariant& v) { return v.toMap()["source"] == "sfx"; }));
}

TEST_CASE("editor: deshacer sobrevive a cerrar y abrir el proyecto", "[studio]") {
    Fixture f;
    {
        studio::EditorModel m(f.store);
        REQUIRE(m.openProject("p1"));
        m.moveClip("b", 7, "V1");
        CHECK(m.canUndo());
    }
    CHECK(std::filesystem::exists(f.dir / "data" / "history" / "p1.json"));
    studio::EditorModel again(f.store);
    REQUIRE(again.openProject("p1"));
    CHECK(start_of(f.saved(), "b") == 7);
    REQUIRE(again.canUndo());
    again.undo();
    CHECK(start_of(f.saved(), "b") == 6);
    CHECK(again.canRedo());
}

TEST_CASE("editor: fragmentar un subtítulo en cuadros", "[studio]") {
    Fixture f;
    studio::EditorModel m(f.store);
    REQUIRE(m.openProject("p1"));
    m.addText();
    m.changeText("uno dos tres cuatro cinco");
    m.changeStyle({{"max_words", 2}});
    const int before = static_cast<int>(m.clips().size());
    m.fragmentText();
    CHECK(static_cast<int>(m.clips().size()) == before + 2);
    CHECK(m.selectedClip()["text"] == "uno dos");
    m.undo();
    CHECK(static_cast<int>(m.clips().size()) == before);
}

TEST_CASE("editor: «Mis estilos» de texto en los ajustes", "[studio]") {
    Fixture f;
    core::SettingsFile settings(f.dir / "data" / "settings.json");
    studio::EditorModel m(f.store, &settings);
    REQUIRE(m.openProject("p1"));
    m.addText();
    m.applyTextTheme("neon");
    m.saveTextFavorite();
    REQUIRE(m.textFavorites().size() == 1);
    const QString id = m.textFavorites().front().toMap()["id"].toString();
    CHECK(m.textFavorites().front().toMap()["name"] == "neon · 61px");
    m.applyTextTheme("neon"); // lo quita
    m.applyTextFavorite(id);
    CHECK(m.selectedClip()["themeId"] == "neon");
    m.deleteTextFavorite(id);
    CHECK(m.textFavorites().isEmpty());
}
