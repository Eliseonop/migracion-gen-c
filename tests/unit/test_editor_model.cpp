// El ViewModel del editor (apps/studio/editor_model.hpp): lo que hace cada gesto de la timeline y
// que el resultado se guarda en projects.json.

#include <QAbstractItemModel>
#include <QCoreApplication>
#include <QImage>
#include <QPointF>
#include <QUrl>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "gc/core/io.hpp"
#include "gc/core/library.hpp"
#include "gc/core/fx_catalog.hpp"
#include "gc/core/projects_store.hpp"
#include "gc/core/serialize.hpp"
#include "gc/core/timeline_ui.hpp"

#include "editor_model.hpp"

#ifdef GC_STUDIO_PREVIEW
#include <utility>

#include "gc/media/probe.hpp"
#endif

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
    ~Fixture() {
        try { // el remove_all de MSVC puede lanzar bad_alloc al copiar rutas
            std::filesystem::remove_all(dir);
        } catch (...) { // NOLINT(bugprone-empty-catch): la carpeta temporal se queda, el destructor no lanza
        }
    }

    [[nodiscard]] core::Timeline saved() const {
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
    const auto curve = [&] { return m.selectedClip()["curve"].toMap(); };
    CHECK(curve()["kind"] == "linear"); // la del keyframe al que se llega (el de 3 s)
    CHECK_FALSE(curve()["first"].toBool());
    m.setCurve("bezier");
    CHECK(curve()["kind"] == "bezier");
    m.setCurve("bezier", {0.1234, 1.5, 0.9, 1});
    CHECK(curve()["bezier"] == QVariantList{0.123, 1.5, 0.9, 1.0});
    CHECK(studio::EditorModel::curvePoints("hold", {}).size() == 3);
    const QVariantList pts = studio::EditorModel::curvePoints("bezier", {0.1, 1.5, 0.9, 1}, 4);
    CHECK(pts.size() == 5);
    CHECK(pts[2].toPointF().y() > 1); // el rebote se pasa
    m.setPlayhead(0.5);
    CHECK(curve()["first"].toBool());
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

TEST_CASE("editor: importar vídeos, imágenes y audios del disco al material del proyecto", "[studio]") {
#ifndef GC_STUDIO_PREVIEW
    SKIP("Sin gc_media (FFmpeg) no se importan JPG ni vídeos");
#endif
    Fixture f;
    const auto media_dir = core::path_from_utf8(GC_FIXTURES_DIR) / "media" / "files";
    const auto in = f.dir / "fuera";
    std::filesystem::create_directories(in);
    std::filesystem::copy_file(media_dir / "v_2s.mp4", in / "Mi clip.mp4");
    std::filesystem::copy_file(media_dir / "a.wav", in / "voz.WAV");
    std::filesystem::copy_file(core::path_from_utf8(GC_FIXTURES_DIR) / "media" / "import" / "jpeg_420.jpg", in / "foto.jpg");
    std::filesystem::copy_file(media_dir / "anim.gif", in / "baile.gif");
    { std::ofstream(in / "nota.txt") << "x"; }
    { std::ofstream(in / "rota.png") << "no es una imagen"; }
    { std::ofstream(in / "vacío.mp4"); }
    const auto url = [&](const char* name) {
        return QUrl::fromLocalFile(QString::fromStdString(core::path_to_utf8(in / core::path_from_utf8(name))));
    };
    studio::EditorModel m(f.store);
    CHECK(m.importFiles({url("Mi clip.mp4")}) == 0); // sin proyecto abierto
    REQUIRE(m.openProject("p1"));
    const auto project_names = [&] {
        QStringList out;
        for (const QVariant& v : m.materials()) {
            if (v.toMap()["source"] == "project") {
                out << v.toMap()["kind"].toString() + ":" + v.toMap()["name"].toString();
            }
        }
        return out;
    };
    REQUIRE(project_names().isEmpty());
    CHECK(m.importFiles({url("Mi clip.mp4"), url("foto.jpg"), url("voz.WAV"), url("nota.txt"), url("vacío.mp4"),
                         url("baile.gif"), url("rota.png")}) == 4);
    CHECK(project_names() == QStringList{"video:Mi clip", "image:foto", "image:baile", "audio:voz"});
    CHECK(m.materials().front().toMap()["source"] == "project"); // delante, como al abrir el proyecto
    CHECK(m.notice()["type"] == "error");
    const QString msg = m.notice()["message"].toString();
    CHECK_FALSE(msg.contains("nota.txt")); // importMedia: lo que no es vídeo, imagen ni audio no cuenta
    CHECK(msg.contains("vacío.mp4: Archivo vacío."));
    CHECK(msg.contains("rota.png: No se pudo leer la imagen. Usa PNG, JPG, WebP o GIF."));
    CHECK_FALSE(msg.contains("Mi clip"));
    CHECK_FALSE(msg.contains("foto"));

    // En projects.json, con el archivo copiado (el original sigue donde estaba).
    auto p = f.store.get_project("p1");
    REQUIRE(p);
    REQUIRE(*p);
    REQUIRE((*p)->clips.size() == 1);
    REQUIRE((*p)->audios.size() == 1);
    REQUIRE((*p)->images.size() == 2);
    // La imagen, como PNG de trabajo; el GIF animado, tal cual y con sus metadatos (upload_images).
    const core::ImageInfo& photo = (*p)->images[0];
    const core::ImageInfo& gif = (*p)->images[1];
    CHECK(photo.label == "foto");
    CHECK(photo.origin == "upload");
    CHECK(photo.filename.ends_with(".png"));
    CHECK(gif.filename.ends_with(".gif"));
    CHECK(gif.animated == true);
    CHECK(gif.frames == 10);
    CHECK(gif.width == 64);
    const auto photo_file = core::resolve_project_media(f.store.clips_dir(), **p, "image", photo.filename);
    REQUIRE(photo_file);
    {
        std::ifstream png(*photo_file, std::ios::binary);
        std::string magic(8, '\0');
        png.read(magic.data(), 8);
        CHECK(magic == std::string("\x89PNG\r\n\x1a\n"));
    }
    const core::ClipInfo& clip = (*p)->clips.front();
    CHECK(clip.label == "Mi clip");
    CHECK(clip.index == 100000);
    CHECK(clip.origin == "import");
    CHECK((*p)->audios.front().label == "voz");
    CHECK(std::filesystem::exists(in / "Mi clip.mp4"));
    const auto copied = core::resolve_project_media(f.store.clips_dir(), **p, "video", clip.filename);
    REQUIRE(copied);
    CHECK(std::filesystem::file_size(*copied) == std::filesystem::file_size(in / "Mi clip.mp4"));
#ifdef GC_STUDIO_PREVIEW
    CHECK(clip.end == media::probe_duration(in / "Mi clip.mp4"));
    CHECK((*p)->audios.front().duration == media::probe_duration(in / "voz.WAV"));
    CHECK(clip.end > 1.5);
    CHECK(photo.width == 160); // probe_size del PNG convertido
    CHECK(photo.height == 96);
#endif

    // Uno solo: el aviso de React con su nombre; el material se rehace sin duplicar.
    CHECK(m.importFiles({url("voz.WAV")}) == 1);
    CHECK(m.notice()["type"] == "success");
    CHECK(m.notice()["message"] == "«voz.WAV» está en el material.");
    CHECK(project_names() == QStringList{"video:Mi clip", "image:foto", "image:baile", "audio:voz", "audio:voz"});
    CHECK(m.importFiles({url("Mi clip.mp4"), url("voz.WAV")}) == 2);
    CHECK(m.notice()["message"] == "2 archivos añadidos al material.");

    // Nada que importar: el aviso del botón o el de soltar en el panel.
    CHECK(m.importFiles({url("nota.txt")}) == 0);
    CHECK(m.notice()["type"] == "error");
    CHECK(m.notice()["message"] == "Suelta un vídeo, una imagen o un audio.");
    CHECK(m.importFiles({url("nota.txt")}, true) == 0);
    CHECK(m.notice()["message"] == "Formato no compatible. Suelta un vídeo, una imagen o un audio.");

    // dragMediaKind: vídeo antes que imagen y que audio; «other» si no hay nada de eso.
    CHECK(m.dropFileKind({}) == "file");
    CHECK(m.dropFileKind({url("nota.txt")}) == "other");
    CHECK(m.dropFileKind({url("nota.txt"), url("voz.WAV")}) == "audio");
    CHECK(m.dropFileKind({url("voz.WAV"), url("foto.jpg")}) == "image");
    CHECK(m.dropFileKind({url("foto.jpg"), url("Mi clip.mp4")}) == "video");
    CHECK(m.dropFileKind({url("clip.MPEG")}) == "video"); // mpe?g, sin distinguir mayúsculas
    CHECK(m.dropFileKind({url("x.ogv")}) == "video");     // por su tipo MIME (video/ogg), como el navegador
}

namespace {

// Una copia: f.saved() devuelve una timeline temporal.
core::TimelineClip saved_clip(const core::Timeline& tl, const QString& id) {
    for (const auto& c : tl.clips) {
        if (c->id == id.toStdString()) {
            return *c;
        }
    }
    FAIL("no está " << id.toStdString());
    throw std::logic_error("inalcanzable");
}

} // namespace

TEST_CASE("editor: lo soltado queda como objeto libre, a la altura del cuadro (applyFreeLayout)", "[studio]") {
    Fixture f;
    const auto media_dir = core::path_from_utf8(GC_FIXTURES_DIR) / "media" / "files";
    std::filesystem::create_directories(f.dir / "fuera");
    std::filesystem::copy_file(media_dir / "still.png", f.dir / "fuera" / "foto.png"); // 64 × 48
    std::filesystem::copy_file(media_dir / "v_rot90.mp4", f.dir / "fuera" / "giro.mp4"); // 64 × 48 girado: 48 × 64
    const auto before = core::read_json_file(f.dir / "data" / "projects.json");
    REQUIRE(before);
    studio::EditorModel m(f.store);
    REQUIRE(m.openProject("p1"));
    CHECK(core::read_json_file(f.dir / "data" / "projects.json").value() == *before); // nada que convertir: no guarda
    const auto url = [&](const char* name) {
        return QUrl::fromLocalFile(QString::fromStdString(core::path_to_utf8(f.dir / "fuera" / name)));
    };
    REQUIRE(m.importFiles({url("foto.png"), url("giro.mp4")}) == 2);
    QString image_key;
    QString video_key;
    for (const QVariant& v : m.materials()) {
        const QVariantMap item = v.toMap();
        if (item["source"] == "project" && item["kind"] == "image") {
            image_key = item["key"].toString();
        } else if (item["source"] == "project" && item["kind"] == "video") {
            video_key = item["key"].toString();
        }
    }
    REQUIRE_FALSE(image_key.isEmpty());
    REQUIRE_FALSE(video_key.isEmpty());
    double y = -1;
    for (const QVariant& t : m.tracks()) {
        if (t.toMap()["id"] == "V1") {
            y = t.toMap()["y"].toDouble() + (t.toMap()["height"].toDouble() / 2);
        }
    }
    REQUIRE(y >= 0);
    const double out_h = static_cast<double>(f.saved().height);

    // La imagen: centrada y a la altura del cuadro (escala alto / 48), en el mismo paso que el soltar.
    m.dropMaterial(image_key, 12 * m.pps(), y, false);
    REQUIRE(m.selection().size() == 1);
    const QString image = m.selection().front();
    {
        const core::TimelineClip c = saved_clip(f.saved(), image);
        CHECK(c.layout == "overlay");
        CHECK(c.frame == "free");
        REQUIRE(c.transform);
        CHECK(c.transform->value("scale", 0.0) == core::js_fixed(out_h / 48, 5));
        REQUIRE(c.reframe);
        CHECK(c.reframe->crop_w == 1.0);
    }
    // heightScale: «Escala 100 %» es la altura del cuadro.
    CHECK(m.selectedClip()["transform"].toMap()["scalePct"].toDouble() == 100);
    m.setScalePct(50);
    CHECK(saved_clip(f.saved(), image).transform->value("scale", 0.0) == core::js_fixed(0.5 * out_h / 48, 5));
    m.resetTransform();
    CHECK(saved_clip(f.saved(), image).transform->value("scale", 0.0) == out_h / 48);
    m.undo();
    m.undo();
    m.undo(); // soltar y convertir era un solo paso
    CHECK(std::ranges::none_of(f.saved().clips, [&](const auto& c) { return c->id == image.toStdString(); }));

#ifdef GC_STUDIO_PREVIEW
    // El vídeo girado: su tamaño es el que dibuja el render (con el autogiro), 48 × 64.
    m.dropMaterial(video_key, 12 * m.pps(), y, false);
    REQUIRE(m.selection().size() == 1);
    const core::TimelineClip v = saved_clip(f.saved(), m.selection().front());
    CHECK(v.layout == "overlay");
    CHECK(v.frame == "free");
    REQUIRE(v.transform);
    CHECK(v.transform->value("scale", 0.0) == core::js_fixed(out_h / 64, 5));
    CHECK(m.selectedClip()["transform"].toMap()["scalePct"].toDouble() == 100);
#endif
}

TEST_CASE("editor: un clip antiguo («fill») entra como objeto libre al abrir, sin paso de deshacer", "[studio]") {
    Fixture f;
    // p1 con una imagen en su carpeta y un clip que la usa como en el «Fijar vídeo» de antes.
    auto data = core::read_json_file(f.dir / "data" / "projects.json");
    REQUIRE(data);
    Json& p1 = (*data)["projects"][0];
    p1["images"] = Json::array({{{"id", "i1"}, {"filename", "foto.png"}, {"url", "/i"}}});
    p1["timeline"]["clips"].push_back({{"id", "img"},        {"track_id", "V2"},    {"kind", "image"},
                                       {"asset_kind", "images"}, {"asset_id", "i1"}, {"filename", "foto.png"},
                                       {"start", 0.0},       {"in_point", 0.0},     {"out_point", 3.0},
                                       {"layout", "fill"},   {"frame", "full"}});
    REQUIRE(core::write_json_file(f.dir / "data" / "projects.json", *data));
    const auto project = f.store.get_project("p1");
    REQUIRE(project);
    REQUIRE(*project);
    const auto file = core::resolve_project_media(f.store.clips_dir(), **project, "image", "foto.png");
    REQUIRE(file);
    std::filesystem::create_directories(file->parent_path());
    std::filesystem::copy_file(core::path_from_utf8(GC_FIXTURES_DIR) / "media" / "files" / "still.png", *file);

    studio::EditorModel m(f.store);
    REQUIRE(m.openProject("p1"));
    CHECK_FALSE(m.canUndo());            // la conversión no es un paso
    const core::Timeline tl = f.saved(); // y ya está guardada, como tras el autoguardado de video-yt
    const core::TimelineClip c = saved_clip(tl, "img");
    CHECK(c.layout == "overlay");
    CHECK(c.frame == "free");
    REQUIRE(c.transform);
    CHECK(c.transform->value("scale", 0.0) == core::js_fixed(static_cast<double>(tl.height) / 48, 5));
    // Los de sin medio (a.mp4 no está) se quedan como estaban.
    CHECK(saved_clip(tl, "a").layout == "fill");
}

#ifdef GC_STUDIO_PREVIEW
TEST_CASE("editor: un SFX o un audio de colección soltado dura lo que su archivo", "[studio]") {
    Fixture f;
    const auto media_dir = core::path_from_utf8(GC_FIXTURES_DIR) / "media" / "files";
    const auto sfx_file = f.dir / "sfx" / "Ritmos" / "bateria.m4a";
    const auto col_file = f.dir / "material" / "Viajes" / "ola.wav";
    std::filesystem::create_directories(sfx_file.parent_path());
    std::filesystem::create_directories(col_file.parent_path());
    std::filesystem::copy_file(media_dir / "drums120.m4a", sfx_file);
    std::filesystem::copy_file(media_dir / "a.wav", col_file);
    core::SettingsFile settings(f.dir / "data" / "settings.json");
    REQUIRE(settings.save({{"sfx_folder", core::path_to_utf8(f.dir / "sfx")},
                           {"material_root", core::path_to_utf8(f.dir / "material")}}));
    studio::EditorModel m(f.store, &settings);
    REQUIRE(m.openProject("p1"));
    QString sfx;
    QString col;
    for (const QVariant& v : m.materials()) {
        const QVariantMap item = v.toMap();
        if (item["source"] == "sfx") {
            sfx = item["key"].toString();
        } else if (item["source"] == "collection") {
            col = item["key"].toString();
        }
    }
    REQUIRE_FALSE(sfx.isEmpty());
    REQUIRE_FALSE(col.isEmpty());
    double y = -1;
    for (const QVariant& t : m.tracks()) {
        if (t.toMap()["id"] == "A1") {
            y = t.toMap()["y"].toDouble() + (t.toMap()["height"].toDouble() / 2);
        }
    }
    REQUIRE(y >= 0);
    // Como el <audio preload="metadata"> de EdLibrary: la duración del archivo, no 1 s.
    double x = 30 * m.pps();
    for (const auto& [key, file] : {std::pair{sfx, sfx_file}, std::pair{col, col_file}}) {
        const double expected = media::probe_duration(file);
        INFO(key.toStdString());
        REQUIRE(expected > 0.1);
        REQUIRE(expected != 1.0); // la de por defecto
        CHECK(m.previewMaterialDrop(key, x, y, false)["duration"].toDouble() == expected);
        m.dropMaterial(key, x, y, false);
        REQUIRE(m.selection().size() == 1);
        const QString id = m.selection().front();
        for (const QVariant& v : m.clips()) {
            if (v.toMap()["clipId"] == id) {
                CHECK(v.toMap()["duration"].toDouble() == expected);
            }
        }
        x += 40 * m.pps();
    }
    // Una búsqueda nueva rehace el material: la duración ya sondeada se conserva.
    m.searchSfx("bateria", "");
    CHECK(m.previewMaterialDrop(sfx, x, y, false)["duration"].toDouble() == media::probe_duration(sfx_file));
}
#endif

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

TEST_CASE("editor: el rombo del color de un texto", "[studio]") {
    Fixture f;
    studio::EditorModel m(f.store);
    REQUIRE(m.openProject("p1"));
    m.addText();
    CHECK(m.selectedClip()["styleKf"].toMap()["color"] == "off");
    m.setPlayhead(1);
    m.toggleStyleKeyframe("color");
    CHECK(m.selectedClip()["styleKf"].toMap()["color"] == "on");
    m.setPlayhead(3);
    CHECK(m.selectedClip()["styleKf"].toMap()["color"] == "empty");
    m.changeStyle({{"color", "#ff0000"}}); // animado: el cambio va al keyframe del cabezal
    m.setPlayhead(1);
    CHECK(m.selectedClip()["style"].toMap()["color"] == "#ffffff");
    m.toggleStyleKeyframe("color");
    m.setPlayhead(3);
    CHECK(m.selectedClip()["styleKf"].toMap()["color"] == "on");
}

TEST_CASE("editor: recortar un clip superpuesto", "[studio]") {
    Fixture f;
    const auto file = f.dir / "data" / "projects.json";
    Json data = *core::read_json_file(file);
    Json& clips = data["projects"][0]["timeline"]["clips"];
    // Ya un objeto libre: si no, al abrir se convertiría (centrado, a la altura del cuadro y sin giro).
    clips[1].update({{"kind", "image"}, {"asset_kind", "images"}, {"filename", "foto.png"}, {"layout", "overlay"},
                     {"frame", "free"}, {"transform", {{"rotation", 5}}}});
    clips.push_back(clip("v", "V2", "video", 0, 4));
    clips.back()["layout"] = "overlay";
    REQUIRE(core::write_json_file(file, data));
    const auto project = f.store.get_project("p1");
    const auto image = core::project_base(f.store.clips_dir(), **project) / "image" / "foto.png";
    std::filesystem::create_directories(image.parent_path());
    REQUIRE(QImage(400, 200, QImage::Format_RGB32).save(QString::fromStdString(image.string())));
    studio::EditorModel m(f.store);
    REQUIRE(m.openProject("p1"));
    m.select("a");
    CHECK(m.openCrop().isEmpty()); // un clip a pantalla completa no se recorta
    m.select("v");
    CHECK(m.openCrop().isEmpty()); // un vídeo sin su archivo: sin su tamaño no se recorta
    CHECK(m.error().contains("vídeo"));
    m.select("b");
    const QVariantMap start = m.openCrop();
    CHECK(start["clipId"] == "b");
    CHECK(std::abs(m.playhead() - 6.02) < 1e-9); // el cabezal entra en el clip
    auto* crop = qobject_cast<studio::CropEditor*>(m.cropEditor());
    REQUIRE(crop);
    CHECK(crop->rotation() == 5);
    CHECK(crop->srcAspect() == 2); // el de la imagen
    crop->pickRatio("1:1");
    crop->setRotation(-20);
    const double wf = crop->wf();
    m.confirmCrop();
    const auto saved_b = [&] {
        const core::Timeline tl = f.saved();
        return **std::ranges::find(tl.clips, std::string("b"), [](const auto& c) { return c->id; });
    };
    const core::TimelineClip b = saved_b();
    REQUIRE(b.reframe);
    CHECK(std::abs(*b.reframe->crop_w - wf) < 1e-4);
    CHECK(b.reframe->keyframes.size() == 1);
    CHECK(b.transform->value("rotation", Json(0)) == -20);
    m.undo();
    CHECK_FALSE(saved_b().reframe);
}

TEST_CASE("editor: el recuadro del modal Recortar", "[studio]") {
    studio::CropEditor crop;
    crop.load({{"cx", 0.5}, {"cy", 0.5}, {"wf", 0.5}, {"hf", 0.5}});
    CHECK_FALSE(crop.press(0.05, 0.05, 400, 300));
    REQUIRE(crop.press(0.5, 0.5, 400, 300)); // dentro: mueve
    crop.drag(0.6, 0.45);
    CHECK(std::abs(crop.cx() - 0.6) < 1e-9);
    CHECK(std::abs(crop.cy() - 0.45) < 1e-9);
    crop.release();
    crop.load({{"cx", 0.5}, {"cy", 0.5}, {"wf", 0.5}, {"hf", 0.5}});
    REQUIRE(crop.press(0.75, 0.75, 400, 300)); // esquina: el borde opuesto queda anclado
    crop.drag(0.85, 0.85);
    CHECK(std::abs(crop.wf() - 0.6) < 1e-9);
    CHECK(std::abs(crop.cx() - 0.55) < 1e-9);
    CHECK(crop.cursorAt(0.25, 0.25, 400, 300) == Qt::SizeFDiagCursor);
    crop.reset();
    CHECK(crop.wf() == 1);
    CHECK(crop.ratio() == "free");
}

TEST_CASE("editor: gestionar las carpetas de material", "[studio]") {
    Fixture f;
    core::SettingsFile settings(f.dir / "data" / "settings.json");
    studio::EditorModel m(f.store, &settings);
    REQUIRE(m.openProject("p1"));
    const auto root = f.dir / "material";
    std::filesystem::create_directories(root / "Fondos");
    std::ofstream(root / "Fondos" / "cielo.png") << "png";
    std::ofstream(f.dir / "nube.jpg") << "jpg";
    const auto col = [&](int i) { return m.collections()[i].toMap(); };
    CHECK_FALSE(m.setCollectionsRoot(QString::fromStdString((f.dir / "no").string())));
    CHECK(m.error() == "Carpeta no válida.");
    REQUIRE(m.setCollectionsRoot(QUrl::fromLocalFile(QString::fromStdString(root.string())).toString()));
    CHECK(m.collectionsRoot() == QString::fromStdString(root.string()));
    REQUIRE(m.collections().size() == 1);
    CHECK(col(0)["enabled"].toBool());
    m.setCollectionPrefs("Fondos", {{"enabled", false}, {"favorite", true}});
    CHECK_FALSE(col(0)["enabled"].toBool());
    CHECK(col(0)["favorite"].toBool());
    CHECK(m.createCollection("Nueva") == "Nueva");
    REQUIRE(m.collections().size() == 2);
    CHECK(m.addCollectionFiles("Nueva", {QUrl::fromLocalFile(QString::fromStdString((f.dir / "nube.jpg").string()))}) == 1);
    CHECK(std::filesystem::exists(root / "Nueva" / "nube.jpg"));
    CHECK(col(1)["count"] == 1); // las favoritas primero
}

TEST_CASE("editor: dar de alta y editar SFX", "[studio]") {
    Fixture f;
    const auto lib = f.dir / "sfx";
    std::filesystem::create_directories(lib / "01_IMPACTOS");
    std::ofstream(lib / "01_IMPACTOS" / "golpe.mp3") << "x";
    REQUIRE(core::write_json_file(lib / "sfx_library.json",
                                  {{"categorias", {{"01_IMPACTOS", {{"etiqueta", "Impactos"}, {"cantidad", 1}}}}},
                                   {"sonidos", Json::array({{{"sonido", "Golpe"}, {"archivo", "golpe.mp3"},
                                                             {"carpeta", "01_IMPACTOS"}}})}}));
    std::ofstream(f.dir / "boom.wav") << "RIFF";
    std::ofstream(f.dir / "nota.txt") << "x";
    core::SettingsFile settings(f.dir / "data" / "settings.json");
    REQUIRE(settings.save({{"sfx_folder", core::path_to_utf8(lib)}}));
    studio::EditorModel m(f.store, &settings);
    REQUIRE(m.openProject("p1"));
    const auto url = [&](const char* name) { return QUrl::fromLocalFile(QString::fromStdString((f.dir / name).string())); };
    const QVariantList drafts = m.sfxDrafts({url("boom.wav"), url("nota.txt")}, "01_IMPACTOS");
    REQUIRE(drafts.size() == 1); // solo audios
    QVariantMap row = drafts[0].toMap();
    CHECK(row["name"] == "boom");
    CHECK(row["categoryId"] == "01_IMPACTOS");
    row["categoryId"] = "";
    row["categoryLabel"] = "";
    CHECK(m.saveSfx(row) == "Pon una categoría.");
    const QVariantMap cat = m.createSfxCategory("Explosiones");
    CHECK(cat["id"] == "02_EXPLOSIONES");
    CHECK(m.createSfxCategory(" explosiones ")["id"] == "02_EXPLOSIONES"); // ya está
    row["categoryId"] = cat["id"];
    row["name"] = " Boom ";
    CHECK(m.saveSfx(row).isEmpty());
    CHECK(std::filesystem::exists(lib / "02_EXPLOSIONES" / "boom.wav"));
    QVariantMap edit = m.sfxDraft("sfx:02_EXPLOSIONES/boom.wav");
    CHECK(edit["name"] == "Boom");
    edit["categoryId"] = "01_IMPACTOS";
    edit["uso"] = "golpes";
    CHECK(m.saveSfx(edit).isEmpty());
    CHECK(std::filesystem::exists(lib / "01_IMPACTOS" / "boom.wav"));
    CHECK(m.sfxDraft("sfx:01_IMPACTOS/boom.wav")["uso"] == "golpes");
}

TEST_CASE("editor: buscar en la biblioteca de efectos", "[studio]") {
    auto catalog = core::FxCatalog::load(core::path_from_utf8(GC_RESOURCES_DIR) / "fx" / "catalog.json");
    REQUIRE(catalog);
    core::set_fx_catalog(std::make_shared<const core::FxCatalog>(std::move(*catalog)));
    Fixture f;
    studio::EditorModel m(f.store);
    const auto ids = [&](const char* q, const char* cat) {
        QStringList out;
        for (const QVariant& e : m.searchVfx(q, cat)) {
            out << e.toMap()["id"].toString();
        }
        return out;
    };
    CHECK(ids("", "").size() == m.vfxCatalog().size());
    CHECK(ids("ABERRACION", "").contains("glitch_rgb")); // sin tildes ni mayúsculas, también en las etiquetas
    CHECK(ids("glitch  rgb", "").contains("glitch_rgb"));
    CHECK(ids("glitch", "retro").isEmpty());
    CHECK(ids("", "retro").contains("vhs"));
    CHECK(ids("nada que ver", "").isEmpty());
}

TEST_CASE("editor: título y descripción de un material del proyecto y de la biblioteca", "[studio]") {
    Fixture f;
    auto data = core::read_json_file(f.dir / "data" / "projects.json");
    REQUIRE(data);
    Json& p1 = (*data)["projects"][0];
    p1["clips"] = Json::array({{{"index", 3}, {"filename", "c.mp4"}, {"url", "/c"}, {"start", 10.0}, {"end", 72.5},
                                {"label", "Viejo"}, {"description_ai", "Un tren en la nieve"}}});
    p1["images"] = Json::array({{{"id", "i1"}, {"filename", "f.png"}, {"url", "/i"}, {"width", 640}, {"height", 480}}});
    REQUIRE(core::write_json_file(f.dir / "data" / "projects.json", *data));
    REQUIRE(core::write_json_file(f.dir / "data" / "library" / "library.json",
                                  {{"version", 1},
                                   {"items", Json::array({{{"id", "L1"}, {"resource_type", "image"},
                                                           {"filename", "l.png"}, {"label", "Guardada"}}})}}));
    studio::EditorModel m(f.store);
    REQUIRE(m.openProject("p1"));

    QVariantMap info = m.materialInfo("clips:3");
    CHECK(info["kind"] == "clips");
    CHECK_FALSE(info["library"].toBool());
    CHECK(info["label"] == "Viejo");
    CHECK(info["descriptionAi"] == "Un tren en la nieve");
    CHECK(info["duration"].toDouble() == 62.5); // end − start
    CHECK(m.materialInfo("images:i1")["width"].toInt() == 640);
    CHECK(m.materialInfo("shape:star").isEmpty());

    CHECK(m.saveMaterialInfo("clips:3", "  Tren  ", " Plano general ").isEmpty());
    const auto project = f.store.get_project("p1");
    REQUIRE(project);
    REQUIRE(*project);
    CHECK((**project).clips[0].label == "Tren");
    CHECK((**project).clips[0].description == "Plano general");
    CHECK(m.materialInfo("clips:3")["label"] == "Tren"); // el material se recarga

    info = m.materialInfo("lib:images:L1");
    CHECK(info["library"].toBool());
    CHECK(info["label"] == "Guardada");
    CHECK(m.saveMaterialInfo("lib:images:L1", "Logo", "Fondo transparente").isEmpty());
    const Json lib = core::LibraryFile(f.dir / "data").load();
    CHECK(lib["items"][0]["label"] == "Logo");
    CHECK(lib["items"][0]["description"] == "Fondo transparente");
    const QVariantList materials = m.materials();
    const auto row = std::ranges::find_if(materials, [](const QVariant& v) { return v.toMap()["key"] == "lib:images:L1"; });
    REQUIRE(row != materials.end());
    CHECK(row->toMap()["name"] == "Logo");
}
