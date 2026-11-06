// genclip-studio entero sin pantalla: el QML real con los ViewModels, manejado con el ratón y el
// teclado de QTest. Comprueba que los gestos de la timeline llegan al modelo y a projects.json.

#include <QCoreApplication>
#include <QGuiApplication>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "gc/core/fx_catalog.hpp"
#include "gc/core/io.hpp"
#include "gc/core/timeline_view.hpp"

#include "editor_model.hpp"
#include "frame_preview.hpp"
#include "home_model.hpp"
#include "settings_model.hpp"
#include "thumbnails.hpp"

#ifdef GC_STUDIO_PREVIEW
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

#include "gc/export/frame.hpp"
#include "gc/export/project.hpp"
#include "gc/media/encode.hpp"
#include "gc/media/probe.hpp"
#endif

using namespace gc;

namespace {

// Una sola app para todo el proceso y sin destruir: hacerlo al salir, detrás de los estáticos de Qt,
// rompe.
QGuiApplication& app() {
    static int argc = 1;
    static char name[] = "gc_tests";
    static char* argv[] = {name, nullptr};
    // Una vez y antes de que haya hilos (el de la vista previa lee el entorno).
    static auto* instance = [] {
        qputenv("QT_QPA_PLATFORM", "offscreen");
#ifdef _WIN32
        // En Windows, offscreen no mira las fuentes del sistema sino <prefijo de Qt>/lib/fonts, que el
        // Qt de vcpkg no trae: sin fuentes, los textos no miden lo que deben y la maqueta se descoloca.
        if (qEnvironmentVariableIsEmpty("QT_QPA_FONTDIR")) {
            qputenv("QT_QPA_FONTDIR", qgetenv("WINDIR") + "/Fonts");
        }
#endif
        qputenv("GC_AUDIO_DEVICE", "null"); // el reproductor, sin sonar por los altavoces
        studio::register_qml_types();
        return new QGuiApplication(argc, argv);
    }();
    return *instance;
}

// El color de la imagen «b» de la fixture.
const QColor kImageColor(200, 30, 40);

Json clip(const char* id, const char* track, double start, double out) {
    return {{"id", id},         {"track_id", track},       {"kind", "video"}, {"asset_kind", "clips"},
            {"asset_id", id},   {"filename", "a.mp4"},     {"start", start},  {"in_point", 0.0},
            {"out_point", out}, {"source_duration", 20.0}, {"name", id}};
}

struct Studio {
    std::filesystem::path dir = std::filesystem::temp_directory_path() /
                                ("gc_studio_ui_" + std::to_string(QCoreApplication::applicationPid()));
    std::unique_ptr<core::ProjectsStore> store;
    core::SequentialIdSource ids;
    std::unique_ptr<studio::Thumbnails> thumbs; // antes que home, que lo usa
    std::unique_ptr<studio::HomeModel> home;
    std::unique_ptr<studio::EditorModel> editor;
    std::unique_ptr<core::SettingsFile> settings_file;
    std::unique_ptr<studio::SettingsModel> settings;
    std::unique_ptr<QQmlApplicationEngine> engine;
    QQuickWindow* window = nullptr;

    Studio() {
        app();
        std::filesystem::remove_all(dir);
        const Json tracks = Json::array({{{"id", "V1"}, {"kind", "video"}, {"name", "V1"}},
                                         {{"id", "V2"}, {"kind", "video"}, {"name", "V2"}},
                                         {{"id", "A1"}, {"kind", "audio"}, {"name", "A1"}}});
        const Json tl = {{"schema_version", 5},
                         {"tracks", tracks},
                         {"clips", Json::array({clip("a", "V1", 0, 4), [] {
                              Json b = clip("b", "V1", 6, 3);
                              b.update({{"kind", "image"}, {"asset_kind", "images"}, {"filename", "b.png"},
                                        {"layout", "overlay"}});
                              return b;
                          }()})}};
        REQUIRE(core::write_json_file(
            dir / "data" / "projects.json",
            {{"projects", Json::array({{{"id", "p1"},
                                        {"name", "Uno"},
                                        {"created_at", "2026-10-19T10:00:00+00:00"},
                                        {"timeline", tl},
                                        {"audios", Json::array({{{"id", "a1"},
                                                                 {"filename", "m.mp3"},
                                                                 {"url", "/u"},
                                                                 {"duration", 5.0},
                                                                 {"label", "Música"}}})},
                                        {"images", Json::array({{{"id", "i1"}, {"filename", "logo.png"},
                                                                 {"url", "/i"}, {"label", "Logo"}}})}}})}}));
        store = std::make_unique<core::ProjectsStore>(dir / "data" / "projects.json", dir / "clips");
        const auto image = core::project_base(store->clips_dir(), **store->get_project("p1")) / "image" / "b.png";
        std::filesystem::create_directories(image.parent_path());
        QImage solid(160, 90, QImage::Format_RGB32);
        solid.fill(kImageColor);
        REQUIRE(solid.save(QString::fromStdString(image.string())));
        thumbs = std::make_unique<studio::Thumbnails>();
        thumbs->setCacheDir(dir / "thumbs");
        home = std::make_unique<studio::HomeModel>(*store, ids, thumbs.get());
        editor = std::make_unique<studio::EditorModel>(*store);
        editor->setProxyDir(dir / "proxies");
        REQUIRE(editor->openProject("p1"));
        settings_file = std::make_unique<core::SettingsFile>(dir / "data" / "settings.json");
        settings = std::make_unique<studio::SettingsModel>(*settings_file);
        engine = std::make_unique<QQmlApplicationEngine>();
        engine->setInitialProperties({{"home", QVariant::fromValue(home.get())},
                                      {"editor", QVariant::fromValue(editor.get())},
                                      {"settings", QVariant::fromValue(settings.get())}});
        engine->load(QUrl("qrc:/qt/qml/GenClip/Studio/Main.qml"));
        REQUIRE(!engine->rootObjects().isEmpty());
        window = qobject_cast<QQuickWindow*>(engine->rootObjects().front());
        REQUIRE(window);
        window->show();
        REQUIRE(QTest::qWaitForWindowExposed(window));
    }
    ~Studio() {
        engine.reset();
        editor.reset(); // para el hilo de la vista previa, que puede estar leyendo la carpeta
        home.reset();
        thumbs.reset(); // y el de las miniaturas
        try { // aun con error_code, el remove_all de MSVC puede lanzar bad_alloc al copiar rutas
            std::filesystem::remove_all(dir);
        } catch (...) { // NOLINT(bugprone-empty-catch): la carpeta temporal se queda, el destructor no lanza
        }
    }

    // Busca en el árbol visual: los delegados de un Repeater cuelgan de su padre visual.
    [[nodiscard]] QQuickItem* item(const QString& name, QQuickItem* from = nullptr) const {
        for (QQuickItem* i : (from ? from : window->contentItem())->childItems()) {
            if (i->objectName() == name && i->isVisible()) {
                return i;
            }
            if (QQuickItem* hit = item(name, i)) {
                return hit;
            }
        }
        return nullptr;
    }
    // Punto de la ventana a `fx`·ancho y la mitad del alto del clip.
    [[nodiscard]] QPoint at(const QString& id, double fx) const {
        QQuickItem* c = item("clip:" + id);
        REQUIRE(c);
        return c->mapToScene(QPointF(c->width() * fx, c->height() / 2)).toPoint();
    }
    void click(const QString& name) const {
        QQuickItem* i = item(name);
        REQUIRE(i);
        QTest::mouseClick(window, Qt::LeftButton, {}, i->mapToScene(QPointF(i->width() / 2, i->height() / 2)).toPoint());
        QCoreApplication::processEvents();
    }
    void drag(QPoint from, QPoint to, Qt::KeyboardModifiers mods = {}) const {
        QTest::mousePress(window, Qt::LeftButton, mods, from);
        for (int i = 1; i <= 8; ++i) {
            QTest::mouseMove(window, from + (to - from) * i / 8);
        }
        QTest::mouseRelease(window, Qt::LeftButton, mods, to);
        QCoreApplication::processEvents(); // la timeline aplica el gesto con Qt.callLater
    }
    [[nodiscard]] QVariantMap view(const QString& id) const {
        for (const QVariant& v : editor->clips()) {
            if (v.toMap()["clipId"] == id) {
                return v.toMap();
            }
        }
        return {};
    }
};

} // namespace

TEST_CASE(
    "studio sin pantalla: arrastrar un clip con el ratón lo mueve, se pega al vecino y se deshace con Ctrl+Z",
    "[studio][ui]") {
    const Studio s;
    const double pps = s.editor->pps();
    const QPoint from = s.at("b", 0.5);
    s.drag(from, from - QPoint(static_cast<int>(1.5 * pps), 0)); // b: 6 → 4,5
    CHECK(s.view("b")["start"].toDouble() == 4.5);
    CHECK(s.editor->selection() == QStringList{"b"});

    const QPoint again = s.at("b", 0.5);
    s.drag(again, again - QPoint(static_cast<int>(0.4 * pps), 0)); // a 0,1 s de «a»: el imán lo pega en 4
    CHECK(s.view("b")["start"].toDouble() == 4.0);

    QTest::keyClick(s.window, Qt::Key_Z, Qt::ControlModifier);
    CHECK(s.view("b")["start"].toDouble() == 4.5);
}

TEST_CASE("studio sin pantalla: arrastrar el borde derecho recorta y no invade al vecino", "[studio][ui]") {
    const Studio s;
    const double pps = s.editor->pps();
    const QPoint edge = s.at("a", 1.0) - QPoint(2, 0);
    s.drag(edge, edge + QPoint(static_cast<int>(5 * pps), 0)); // a: 4 s → hasta «b» (en 6)
    CHECK(s.view("a")["duration"].toDouble() == 6.0);
}

TEST_CASE("studio sin pantalla: arrastrar a la fila de encima cambia de pista; S corta y Supr borra",
          "[studio][ui]") {
    const Studio s;
    QQuickItem* a = s.item("clip:a");
    REQUIRE(a);
    const QPoint from = s.at("b", 0.5);
    s.drag(from, from - QPoint(0, 52)); // V1 → V2 (la fila de encima)
    CHECK(s.view("b")["trackId"] == "V2");

    s.editor->setPlayhead(2);
    QTest::mouseClick(s.window, Qt::LeftButton, {}, s.at("a", 0.5));
    QCoreApplication::processEvents();
    QTest::keyClick(s.window, Qt::Key_S);
    CHECK(s.editor->clips().size() == 3);
    QTest::keyClick(s.window, Qt::Key_Delete);
    CHECK(s.editor->clips().size() == 2);
}

TEST_CASE("studio sin pantalla: atajos del editor (M, «,» y «.», flechas, V)", "[studio][ui]") {
    const Studio s;
    s.editor->setPlayhead(1);
    QTest::keyClick(s.window, Qt::Key_M);
    s.editor->setPlayhead(3);
    QTest::keyClick(s.window, Qt::Key_M);
    REQUIRE(s.editor->markers().size() == 2);
    QTest::keyClick(s.window, Qt::Key_Comma);
    CHECK(s.editor->playhead() == 1.0);
    QTest::keyClick(s.window, Qt::Key_Period);
    CHECK(s.editor->playhead() == 3.0);
    QTest::keyClick(s.window, Qt::Key_M); // otra vez en el mismo sitio: lo quita
    CHECK(s.editor->markers().size() == 1);
    QTest::keyClick(s.window, Qt::Key_Right, Qt::ShiftModifier);
    CHECK(s.editor->playhead() == 4.0);
    QTest::keyClick(s.window, Qt::Key_Left);
    CHECK(std::abs(s.editor->playhead() - 3.9) < 1e-9);

    QTest::mouseClick(s.window, Qt::LeftButton, {}, s.at("a", 0.5));
    QCoreApplication::processEvents();
    QTest::keyClick(s.window, Qt::Key_V);
    CHECK(s.view("a")["disabled"] == true);
    QTest::keyClick(s.window, Qt::Key_V);
    CHECK(s.view("a")["disabled"] == false);
}

TEST_CASE("studio sin pantalla: con la pista bloqueada sus clips no se arrastran", "[studio][ui]") {
    const Studio s;
    QQuickItem* lock = s.item("track:V1:locked");
    REQUIRE(lock);
    QTest::mouseClick(s.window, Qt::LeftButton, {},
                      lock->mapToScene(QPointF(lock->width() / 2, lock->height() / 2)).toPoint());
    QCoreApplication::processEvents();
    const QPoint from = s.at("b", 0.5);
    s.drag(from, from - QPoint(60, 0));
    CHECK(s.view("b")["start"].toDouble() == 6.0);
    CHECK(s.view("b")["locked"] == true);
}

TEST_CASE("studio sin pantalla: los clips de audio pintan sus barras (pseudoWaveform)", "[studio][ui]") {
    const Studio s;
    // Las barras salen del asset_id (no del audio), como en EdTimeline.jsx.
    const QVariantList bars = studio::EditorModel::waveform("a1", 20);
    const auto expected = core::pseudo_waveform(Json("a1"), Json(20));
    REQUIRE(bars.size() == 20);
    for (qsizetype i = 0; i < bars.size(); ++i) {
        CHECK(bars[i].toDouble() == expected[static_cast<std::size_t>(i)]);
    }
    // Un audio soltado en A1 enseña su lienzo con max(16, ancho/5) barras; un vídeo, no.
    double y = -1;
    for (const QVariant& t : s.editor->tracks()) {
        if (t.toMap()["id"] == "A1") {
            y = t.toMap()["y"].toDouble() + (t.toMap()["height"].toDouble() / 2);
        }
    }
    REQUIRE(y >= 0);
    s.editor->dropMaterial("audios:a1", s.editor->pps(), y, false);
    QCoreApplication::processEvents();
    QString audio;
    for (const QVariant& v : s.editor->clips()) {
        if (v.toMap()["kind"] == "audio") {
            audio = v.toMap()["clipId"].toString();
        }
    }
    REQUIRE_FALSE(audio.isEmpty());
    QQuickItem* wave = s.item("wave:" + audio);
    REQUIRE(wave);
    QQuickItem* block = s.item("clip:" + audio);
    REQUIRE(block);
    const auto count = std::max<qsizetype>(16, std::lround(block->width() / 5));
    CHECK(wave->property("heights").toList().size() == count);
    CHECK_FALSE(s.item("wave:a")); // «a» es un vídeo: sin barras
}

TEST_CASE("studio sin pantalla: arrastrar material del panel a la timeline crea el clip", "[studio][ui]") {
    const Studio s;
    QQuickItem* card = s.item("material:audios:a1");
    REQUIRE(card);
    QQuickItem* a = s.item("clip:a");
    REQUIRE(a);
    // A la fila del audio (debajo de V1), a la altura de 10 s.
    const double pps = s.editor->pps();
    const QPointF lane = a->mapToScene(QPointF(0, 0)) - QPointF(a->x(), a->y() - 3);
    const QPoint to = (lane + QPointF(10 * pps, 52 * 2 + 26)).toPoint();
    s.drag(card->mapToScene(QPointF(20, card->height() / 2)).toPoint(), to);
    QCoreApplication::processEvents();
    REQUIRE(s.editor->clips().size() == 3);
    const QVariantMap made = s.editor->selectedClip();
    CHECK(made["kind"] == "audio");
    CHECK(std::abs(made["start"].toDouble() - (to.x() - lane.x()) / pps) < 0.1);
}

TEST_CASE("studio sin pantalla: con Alt el material soltado encima de un clip lo reemplaza", "[studio][ui]") {
    const Studio s;
    QQuickItem* card = s.item("material:images:i1");
    REQUIRE(card);
    const QPoint to = s.at("b", 0.5);
    s.drag(card->mapToScene(QPointF(20, card->height() / 2)).toPoint(), to, Qt::AltModifier);
    QCoreApplication::processEvents();
    REQUIRE(s.editor->clips().size() == 2);
    CHECK(s.view("b").isEmpty());
    const QVariantMap made = s.editor->selectedClip();
    CHECK(made["kind"] == "image");
    CHECK(made["start"] == 6.0);
    CHECK(made["duration"] == 3.0); // toma la duración del clip al que reemplaza
}

TEST_CASE("studio sin pantalla: el rectángulo de selección elige los clips que corta", "[studio][ui]") {
    const Studio s;
    const QPoint a = s.at("a", 0.5);
    const QPoint b = s.at("b", 0.5);
    // Desde un hueco de V1 entre «a» y «b», arriba, hasta el centro de «b»: corta solo «b».
    const QPoint gap((s.at("a", 1.0).x() + s.at("b", 0.0).x()) / 2, a.y() - 20);
    s.drag(gap, b);
    CHECK(s.editor->selection() == QStringList{"b"});
    s.drag(gap, QPoint(a.x(), b.y())); // hacia atrás: «a»
    CHECK(s.editor->selection() == QStringList{"a"});
    QTest::mouseClick(s.window, Qt::LeftButton, {}, gap);
    CHECK(s.editor->selection().isEmpty());
}

TEST_CASE("studio sin pantalla: Ctrl+, abre los ajustes, cambia los fps y guarda una clave", "[studio][ui]") {
    const Studio s;
    CHECK_FALSE(s.item("set:fps:60"));
    QTest::keyClick(s.window, Qt::Key_Comma, Qt::ControlModifier);
    s.click("set:fps:60");
    CHECK(s.settings->fps() == 60);
    s.click("set:quality:high");
    CHECK(s.settings->quality() == "high");
    QTest::keyClick(s.window, Qt::Key_S); // el editor está tapado: sus atajos no cortan
    CHECK(s.editor->clips().size() == 2);

    s.click("set:add:gemini");
    QQuickItem* field = s.item("set:key:gemini");
    REQUIRE(field);
    CHECK(field->hasActiveFocus());
    for (const char ch : std::string("k123")) {
        QTest::keyClick(s.window, ch);
    }
    QTest::keyClick(s.window, Qt::Key_Return);
    CHECK(s.settings_file->api_key("gemini") == "k123");
    s.click("set:remove:gemini");
    CHECK(s.settings_file->api_key("gemini").empty());
}

TEST_CASE("studio sin pantalla: arrastrar hasta el borde desplaza la vista y el clip la sigue", "[studio][ui]") {
    const Studio s;
    QQuickItem* lane = s.item("clip:b")->parentItem();
    const QPoint from = s.at("b", 0.5);
    const double grab = from.x() - s.at("b", 0.0).x();
    const QPoint edge(s.window->width() - 30, from.y());
    QTest::mousePress(s.window, Qt::LeftButton, {}, from);
    for (int i = 1; i <= 8; ++i) {
        QTest::mouseMove(s.window, from + (edge - from) * i / 8);
    }
    const double before = lane->mapFromScene(QPointF(edge)).x();
    // La vista corre a golpes de un Timer de 16 ms y cada golpe rehace la vista previa del gesto: en
    // Debug (MSVC sin optimizar, ~50 ms el golpe) y con el PC ocupado, tarda más que en 300 ms fijos.
    CHECK(QTest::qWaitFor([&] { return lane->mapFromScene(QPointF(edge)).x() > before + 100; }, 5000)); // ha corrido bajo el puntero
    QTest::mouseRelease(s.window, Qt::LeftButton, {}, edge);
    QCoreApplication::processEvents();
    const double reached = (before - grab) / s.editor->pps(); // donde caería sin desplazar
    CHECK(s.view("b")["start"].toDouble() > reached + 1);
}

TEST_CASE("studio sin pantalla: el botón T añade un texto y el inspector lo cambia", "[studio][ui]") {
    const Studio s;
    s.click("editor:addText");
    REQUIRE(s.editor->selectedClip()["kind"] == "text");
    s.click("insp:bold");
    CHECK(s.editor->selectedClip()["style"].toMap()["bold"] == false);
    s.click("insp:align:left");
    CHECK(s.editor->selectedClip()["style"].toMap()["align"] == "left");
    s.click("insp:color:#ffe400");
    CHECK(s.editor->selectedClip()["style"].toMap()["color"] == "#ffe400");
    QQuickItem* glow = s.item("insp:wordfx:glow"); // más abajo en el inspector, fuera de la vista
    REQUIRE(glow);
    QMetaObject::invokeMethod(glow, "clicked");
    CHECK(s.editor->selectedClip()["style"].toMap()["word_fx"].toStringList() == QStringList{"glow"});
}

TEST_CASE("studio sin pantalla: los marcadores se arrastran y se quitan con el botón derecho", "[studio][ui]") {
    const Studio s;
    s.editor->setPlayhead(2);
    QTest::keyClick(s.window, Qt::Key_M);
    REQUIRE(s.editor->markers().size() == 1);
    const QString id = s.editor->markers().front().toMap()["id"].toString();
    QQuickItem* flag = s.item("marker:" + id);
    REQUIRE(flag);
    const QPoint from = flag->mapToScene(QPointF(flag->width() / 2, 5)).toPoint();
    s.drag(from, from + QPoint(static_cast<int>(3 * s.editor->pps()), 0));
    CHECK(std::abs(s.editor->markers().front().toMap()["t"].toDouble() - 5) < 0.1);
    s.editor->renameMarker(id, "  gancho ");
    CHECK(s.editor->markers().front().toMap()["label"] == "gancho");
    const QQuickItem* moved = s.item("marker:" + id);
    QTest::mouseClick(s.window, Qt::RightButton, {}, moved->mapToScene(QPointF(moved->width() / 2, 5)).toPoint());
    QCoreApplication::processEvents();
    CHECK(s.editor->markers().isEmpty());
}

TEST_CASE("studio sin pantalla: Ctrl+Alt+C y Ctrl+Alt+V pegan los atributos elegidos", "[studio][ui]") {
    const Studio s;
    s.editor->select("a");
    s.editor->setBlend("multiply");
    QTest::keyClick(s.window, Qt::Key_C, Qt::ControlModifier | Qt::AltModifier);
    s.editor->select("b");
    QTest::keyClick(s.window, Qt::Key_V, Qt::ControlModifier | Qt::AltModifier);
    QTest::qWait(50); // el diálogo se abre con una transición
    s.click("attr:paste");
    CHECK(s.editor->selectedClip()["blend"] == "multiply");
}

TEST_CASE("studio sin pantalla: un rombo de keyframe se arrastra y un clic lleva el cabezal a él", "[studio][ui]") {
    const Studio s;
    s.editor->select("a");
    s.editor->setPlayhead(1);
    s.editor->toggleKeyframe();
    QCoreApplication::processEvents();
    QQuickItem* dot = s.item("kf:a:0");
    REQUIRE(dot);
    const QPoint from = dot->mapToScene(QPointF(dot->width() / 2, dot->height() / 2)).toPoint();
    s.drag(from, from + QPoint(static_cast<int>(2 * s.editor->pps()), 0));
    CHECK(std::abs(s.editor->playhead() - 3) < 0.1); // el cabezal sigue al keyframe
    CHECK(s.editor->selectedClip()["kf"] == "on");
    s.editor->setPlayhead(0);
    const bool undo = s.editor->canUndo();
    const QQuickItem* moved = s.item("kf:a:0");
    QTest::mouseClick(s.window, Qt::LeftButton, {}, moved->mapToScene(QPointF(moved->width() / 2, moved->height() / 2)).toPoint());
    QCoreApplication::processEvents();
    CHECK(std::abs(s.editor->playhead() - 3) < 0.1);
    CHECK(s.editor->canUndo() == undo);
}

TEST_CASE("studio sin pantalla: las pestañas del inspector según el clip", "[studio][ui]") {
    const Studio s;
    s.editor->select("a");
    QCoreApplication::processEvents();
    CHECK(s.item("insp:x"));
    CHECK_FALSE(s.item("insp:fx:brightness"));
    s.click("insp:tab:color");
    CHECK(s.item("insp:fx:brightness"));
    CHECK_FALSE(s.item("insp:x"));
    s.click("insp:tab:anim");
    CHECK(s.item("insp:appear"));
    s.click("editor:addText"); // un texto no tiene Color ni Audio
    CHECK_FALSE(s.item("insp:tab:color"));
    CHECK_FALSE(s.item("insp:tab:audio"));
}

TEST_CASE("studio sin pantalla: el modal Recortar se abre desde la barra y confirma", "[studio][ui]") {
    const Studio s;
    s.editor->select("a");
    s.click("cropButton");
    QTest::qWait(50);
    CHECK_FALSE(s.item("cropConfirm")); // a pantalla completa no hay recorte
    s.editor->select("b");
    s.click("cropButton");
    QTest::qWait(50);
    QMetaObject::invokeMethod(s.editor->cropEditor(), "pickRatio", Q_ARG(QString, "1:1"));
    s.click("cropConfirm");
    QTest::qWait(50);
    CHECK_FALSE(s.item("cropConfirm"));
    CHECK(s.editor->canUndo());
}

TEST_CASE("studio sin pantalla: el tirador de la curva del keyframe la vuelve personalizada", "[studio][ui]") {
    const Studio s;
    s.editor->select("a");
    s.editor->setPlayhead(1);
    s.editor->toggleKeyframe();
    s.editor->setPlayhead(3);
    s.editor->toggleKeyframe();
    s.editor->setPlayhead(2);
    s.click("insp:tab:anim");
    QQuickItem* h = s.item("curveHandle:1");
    REQUIRE(h);
    const QPoint from = h->mapToScene(QPointF(h->width() / 2, h->height() / 2)).toPoint();
    s.drag(from, from + QPoint(-20, -30));
    const QVariantMap curve = s.editor->selectedClip()["curve"].toMap();
    CHECK(curve["kind"] == "bezier");
    CHECK(curve["bezier"].toList()[1].toDouble() > 0.1); // subió
}

TEST_CASE("studio sin pantalla: un efecto de la biblioteca va a la pestaña Efectos del clip", "[studio][ui]") {
    auto catalog = core::FxCatalog::load(core::path_from_utf8(GC_RESOURCES_DIR) / "fx" / "catalog.json");
    REQUIRE(catalog);
    core::set_fx_catalog(std::make_shared<const core::FxCatalog>(std::move(*catalog)));
    const Studio s;
    s.editor->select("a");
    s.click("material:source:fx");
    s.click("fx:glitch_rgb");
    s.click("insp:tab:fx");
    CHECK(s.item("insp:vfx:name:0"));
    CHECK(s.editor->selectedClip()["vfx"].toList().size() == 1);
}

TEST_CASE("studio sin pantalla: una figura del material se añade con un clic o arrastrándola", "[studio][ui]") {
    const Studio s;
    s.click("material:source:shapes");
    s.click("material:shape:star");
    CHECK(s.editor->selectedClip()["kind"] == "shape");
    const auto count = s.editor->clips().size();
    QQuickItem* card = s.item("material:shape:circle");
    REQUIRE(card);
    const QPoint to = s.at("b", 0.0) + QPoint(static_cast<int>(4 * s.editor->pps()), 0); // tras «b», en V1
    s.drag(card->mapToScene(QPointF(card->width() / 2, card->height() / 2)).toPoint(), to);
    QCoreApplication::processEvents();
    CHECK(s.editor->clips().size() == count + 1);
    CHECK(s.editor->selectedClip()["kind"] == "shape");
    CHECK(s.editor->selectedClip()["duration"] == 5.0);
}

TEST_CASE("studio sin pantalla: los textos del material se añaden, cambian de tema y se arrastran", "[studio][ui]") {
    const Studio s;
    s.click("material:source:text");
    s.click("material:text:neon");
    QVariantMap made = s.editor->selectedClip();
    CHECK(made["kind"] == "text");
    CHECK(made["text"] == "hola mundo");
    CHECK(made["themeId"] == "neon");
    s.click("material:text:fire"); // con un texto elegido, el tema se aplica
    CHECK(s.editor->selectedClip()["themeId"] == "fire");
    CHECK(s.editor->selectedClip()["clipId"] == made["clipId"]);
    const auto count = s.editor->clips().size();
    QQuickItem* tile = s.item("material:text:box");
    REQUIRE(tile);
    const QPoint to = s.at("a", 0.5);
    s.drag(tile->mapToScene(QPointF(tile->width() / 2, tile->height() / 2)).toPoint(), to);
    QCoreApplication::processEvents();
    CHECK(s.editor->clips().size() == count + 1);
    CHECK(s.editor->selectedClip()["kind"] == "text");
    CHECK(s.editor->selectedClip()["themeId"] == "box");
}

TEST_CASE("studio sin pantalla: el menú del clic derecho ordena capas y elimina", "[studio][ui]") {
    Studio s;
    s.editor->setPlayhead(1);
    s.editor->addText(); // el texto de la pista nueva no comparte pista con «a»
    s.editor->select("a");
    s.editor->duplicateSelection(); // la copia, en V1 detrás de «a»: dos capas en la pista
    const QString copy = s.editor->selection().front();
    const auto order = [&] {
        QStringList ids;
        for (const QVariant& c : s.editor->clips()) {
            if (c.toMap()["trackId"] == "V1") {
                ids << c.toMap()["clipId"].toString();
            }
        }
        return ids;
    };
    const QStringList before = order();
    QTest::mouseClick(s.window, Qt::RightButton, {}, s.at("a", 0.5));
    QTest::qWait(50);
    REQUIRE(s.item("ctx:front"));
    QMetaObject::invokeMethod(s.item("ctx:front"), "triggered");
    QCoreApplication::processEvents();
    CHECK(order().back() == "a");
    CHECK(order() != before);
    QTest::mouseClick(s.window, Qt::RightButton, {}, s.at("b", 0.5));
    QTest::qWait(50);
    QMetaObject::invokeMethod(s.item("ctx:delete"), "triggered");
    QCoreApplication::processEvents();
    CHECK(s.view("b").isEmpty());
    CHECK(!copy.isEmpty());
}

#ifdef GC_STUDIO_PREVIEW

namespace {

// Hasta que la vista previa publique el fotograma `tick` (o un error), con un tope: en Debug, componer
// un 1080 × 1920 tarda.
bool wait_tick(const studio::PreviewSource& p, qint64 tick, int since = -1) {
    return QTest::qWaitFor([&] { return p.revision() > since && (p.tick() == tick || !p.error().isEmpty()); }, 60000);
}

// Lo que da el export en `t` con lo que hay en projects.json (el editor lo guarda en cada cambio).
render::Image exported(const std::filesystem::path& root, double t, double scale, const exporter::ProxyMap& proxies = {}) {
    auto project = exporter::load_project(root, "p1");
    REQUIRE(project);
    exporter::FrameOptions options;
    options.omit_unsupported = true;
    options.scale = scale;
    options.proxies = proxies;
    auto renderer = exporter::FrameRenderer::open(*project, options);
    REQUIRE(renderer);
    auto frame = renderer->at(t);
    REQUIRE(frame);
    return std::move(frame->image);
}

bool same_pixels(const QImage& image, const render::Image& ref) {
    return image.width() == ref.width && image.height() == ref.height &&
           static_cast<std::size_t>(image.sizeInBytes()) == ref.pixels.size() &&
           std::memcmp(image.constBits(), ref.pixels.data(), ref.pixels.size()) == 0;
}

// ¿Algún píxel de `rect` casi del color `c`?
bool has_color(const QImage& image, const QRect& rect, const QColor& c) {
    const QRect r = rect.intersected(image.rect());
    for (int y = r.top(); y <= r.bottom(); ++y) {
        for (int x = r.left(); x <= r.right(); ++x) {
            const QColor p = image.pixelColor(x, y);
            if (std::abs(p.red() - c.red()) <= 24 && std::abs(p.green() - c.green()) <= 24 &&
                std::abs(p.blue() - c.blue()) <= 24) {
                return true;
            }
        }
    }
    return false;
}

bool has_color(const render::Image& image, const QColor& c) {
    return has_color(QImage(image.pixels.data(), image.width, image.height, static_cast<qsizetype>(image.width) * 4,
                            QImage::Format_RGBA8888),
                     QRect(0, 0, image.width, image.height), c);
}

} // namespace

TEST_CASE("studio sin pantalla: la vista previa pinta el fotograma del export en el cabezal", "[studio][ui][preview]") {
    const Studio s;
    studio::PreviewSource& p = s.editor->previewSource();
    REQUIRE(p.available());
    REQUIRE(wait_tick(p, 0));
    REQUIRE(p.error().isEmpty());
    // El recuadro de la ventana del test es pequeño: compone reducido (½ o ¼), lo justo para llenarlo.
    // El primer fotograma puede llegar a 1:1, antes de que el QML diga cuánto se ve.
    REQUIRE(QTest::qWaitFor([&] { return p.tick() == 0 && p.scale() < 1.0; }, 60000));
    CHECK(p.image().height() == static_cast<int>(std::lround(1280 * p.scale())));
    // En 0 s solo está «a», cuyo a.mp4 no existe: negro, con su aviso, como el export.
    CHECK(same_pixels(p.image(), exported(s.dir, 0.0, p.scale())));
    CHECK(p.warnings().join('\n').contains("a.mp4"));
    QQuickItem* item = s.item("preview");
    REQUIRE(item);
    const auto on_screen = [&] { return item->mapRectToScene(item->property("frameRect").toRectF()).toAlignedRect(); };
    REQUIRE(!on_screen().isEmpty());
    CHECK_FALSE(has_color(s.window->grabWindow(), on_screen(), kImageColor));

    // De 6 a 9 s se ve «b», la imagen roja: el fotograma sigue al cabezal y llega a la pantalla.
    int since = p.revision();
    s.editor->setPlayhead(7.0);
    REQUIRE(wait_tick(p, 210, since)); // 7 s · 30 fps
    REQUIRE(p.error().isEmpty());
    const render::Image at7 = exported(s.dir, 7.0, p.scale());
    REQUIRE(has_color(at7, kImageColor));
    CHECK(same_pixels(p.image(), at7));
    CHECK(has_color(s.window->grabWindow(), on_screen(), kImageColor));
    // El fotograma encaja en el recuadro negro sin deformarse.
    const QRectF fit = item->property("frameRect").toRectF();
    CHECK(std::abs(fit.width() / fit.height() - static_cast<double>(at7.width) / at7.height) < 0.01);

    // Cambia la timeline (multiplicar sobre negro: «b» deja de verse): se vuelve a componer en el
    // mismo instante, con la timeline de la sesión.
    since = p.revision();
    s.editor->select("b");
    s.editor->setBlend("multiply");
    REQUIRE(wait_tick(p, 210, since));
    REQUIRE(p.error().isEmpty());
    const render::Image multiplied = exported(s.dir, 7.0, p.scale());
    CHECK_FALSE(has_color(multiplied, kImageColor));
    CHECK(same_pixels(p.image(), multiplied));
    CHECK_FALSE(has_color(s.window->grabWindow(), on_screen(), kImageColor));

    // Pasado el final, el último fotograma (como el cabezal en la duración).
    since = p.revision();
    s.editor->setPlayhead(1000.0);
    REQUIRE(wait_tick(p, 269, since)); // 9 s · 30 − 1
    CHECK(p.error().isEmpty());

    // Al cerrar el proyecto, sin fotograma.
    since = p.revision();
    s.editor->closeProject();
    REQUIRE(QTest::qWaitFor([&] { return p.revision() > since; }, 60000));
    CHECK_FALSE(p.hasFrame());
}

TEST_CASE("studio sin pantalla: la vista previa de una timeline vacía lo dice", "[studio][ui][preview]") {
    const Studio s;
    studio::PreviewSource& p = s.editor->previewSource();
    REQUIRE(wait_tick(p, 0));
    const int since = p.revision();
    for (const char* id : {"a", "b"}) {
        s.editor->select(id);
        s.editor->deleteSelection();
    }
    REQUIRE(s.editor->clips().isEmpty());
    REQUIRE(QTest::qWaitFor([&] { return p.revision() > since; }, 60000));
    INFO("error «" << p.error().toStdString() << "», fotograma " << p.hasFrame() << ", tick " << p.tick());
    CHECK_FALSE(p.hasFrame());
    CHECK(p.error().contains("vacía"));
    QQuickItem* message = s.item("preview:message");
    REQUIRE(message);
    CHECK(message->property("text").toString() == p.error());
}

#endif

TEST_CASE("vista previa: la escala más pequeña que llena el recuadro", "[studio][preview]") {
    using studio::PreviewSource;
    CHECK(PreviewSource::scale_for(1080, 1920, 0, 0) == 1.0); // sin recuadro todavía
    CHECK(PreviewSource::scale_for(1080, 1920, 270, 480) == 0.25);
    CHECK(PreviewSource::scale_for(1080, 1920, 250, 481) == 0.5); // un píxel más alto que ¼
    CHECK(PreviewSource::scale_for(1080, 1920, 540, 960) == 0.5);
    CHECK(PreviewSource::scale_for(1080, 1920, 541, 300) == 1.0); // más ancho que ½
    CHECK(PreviewSource::scale_for(2160, 3840, 405, 720) == 0.25); // un 4K vertical en un portátil
    CHECK(PreviewSource::scale_for(0, 1920, 270, 480) == 1.0);
}

TEST_CASE("studio sin pantalla: reproducir lleva el cabezal con el reloj, se para y salta", "[studio][ui][player]") {
    const Studio s;
    studio::Player& player = s.editor->playback();
    s.editor->setPlayhead(1.0);
    QTest::keyClick(s.window, Qt::Key_Space); // reproduce
    REQUIRE(player.playing());
    QTest::qWait(400);
    const double ran = s.editor->playhead();
    CHECK(ran > 1.1); // en tiempo real, con margen para un PC cargado
    CHECK(ran < 3.0);

    s.click("editor:play"); // para
    CHECK_FALSE(player.playing());
    const double stopped = s.editor->playhead();
    QTest::qWait(150);
    CHECK(s.editor->playhead() == stopped);
    // Con el foco en el botón, Espacio cambia una vez (no el botón y el atajo a la vez).
    QTest::keyClick(s.window, Qt::Key_Space);
    CHECK(player.playing());
    QTest::keyClick(s.window, Qt::Key_Space);
    CHECK_FALSE(player.playing());

    // Mover el cabezal sonando: el reloj salta ahí y sigue.
    player.play();
    s.editor->setPlayhead(5.0);
    QTest::qWait(200);
    CHECK(s.editor->playhead() >= 5.0);
    CHECK(s.editor->playhead() < 6.5);

    // Al final se para en la duración, y otra vez desde el principio.
    s.editor->setPlayhead(s.editor->duration() - 0.2);
    REQUIRE(QTest::qWaitFor([&] { return !player.playing(); }, 10000));
    CHECK(s.editor->playhead() == s.editor->duration());
    player.play();
    CHECK(s.editor->playhead() < 0.5);
    player.pause();
}

TEST_CASE("studio sin pantalla: sonando, el inspector no se recalcula a cada fotograma", "[studio][ui][player]") {
    const Studio s;
    s.editor->select("b");
    int changes = 0;
    QObject::connect(s.editor.get(), &studio::EditorModel::selectionChanged, [&] { ++changes; });
    s.editor->playback().play();
    QTest::qWait(300);
    CHECK(changes == 0);
    s.editor->playback().pause();
    CHECK(changes == 1); // al parar, con la pose del cabezal
    s.editor->setPlayhead(1.0);
    CHECK(changes == 2); // parado, como siempre
}

#ifdef GC_STUDIO_PREVIEW
TEST_CASE("studio sin pantalla: la mezcla del export suena en el reproductor y se rehace al cambiar", "[studio][ui][player]") {
    const Studio s;
    studio::Player& player = s.editor->playback();
    CHECK_FALSE(player.audible());
    player.play();
    // La fixture no tiene audio con material: la mezcla es silencio, pero llega y suena.
    REQUIRE(QTest::qWaitFor([&] { return player.audible() || !player.audioError().isEmpty(); }, 60000));
    CHECK(player.audioError().isEmpty());
    CHECK(player.audible());
    // Un cambio de la timeline sonando: hasta la mezcla nueva no, y luego sí, sin parar.
    s.editor->select("b");
    s.editor->setBlend("multiply");
    CHECK_FALSE(player.audible());
    REQUIRE(QTest::qWaitFor([&] { return player.audible() || !player.audioError().isEmpty(); }, 60000));
    CHECK(player.audible());
    CHECK(player.playing());
    player.pause();
}
#endif

#ifdef GC_STUDIO_PREVIEW
TEST_CASE("studio sin pantalla: un vídeo grande se hace proxy y la vista previa lo lee de él", "[studio][ui][proxy]") {
    const Studio s;
    // «a» pasa a ser un vídeo de 2560 × 1440 (1 s), y se vuelve a abrir el proyecto.
    auto project = s.store->get_project("p1");
    REQUIRE(project);
    REQUIRE(*project);
    const auto video = core::project_base(s.store->clips_dir(), **project) / "video" / "a.mp4";
    std::filesystem::create_directories(video.parent_path());
    {
        media::EncodeSettings settings;
        settings.width = 2560;
        settings.height = 1440;
        settings.fps = 30;
        settings.audio = false;
        auto writer = media::Mp4Writer::open(video, settings);
        REQUIRE(writer);
        std::vector<std::uint8_t> rgba(static_cast<std::size_t>(2560) * 1440 * 4, 255);
        for (int k = 0; k < 30; ++k) {
            for (std::size_t i = 0; i < rgba.size(); i += 4) {
                rgba[i] = static_cast<std::uint8_t>(40 + (5 * k)); // el rojo cambia con el fotograma
                rgba[i + 1] = 90;
                rgba[i + 2] = 160;
            }
            REQUIRE(writer->video(rgba));
        }
        REQUIRE(writer->finish());
    }
    REQUIRE(s.editor->openProject("p1"));
    studio::ProxyMaker& maker = s.editor->proxyMaker();
    REQUIRE(QTest::qWaitFor([&] { return maker.ready() == 1 || maker.status().startsWith("No se pudo"); }, 180000));
    INFO(maker.status().toStdString());
    REQUIRE(maker.ready() == 1);
    CHECK(maker.status().isEmpty());
    const studio::PreviewProxy& proxy = maker.proxies().begin()->second;
    CHECK(std::filesystem::is_regular_file(proxy.file));
    CHECK(proxy.file.parent_path() == s.dir / "proxies"); // en la caché, no en la carpeta del proyecto
    CHECK(proxy.source_width == 2560);
    CHECK(proxy.source_height == 1440);

    // La vista previa vuelve a componer con el proxy: lo mismo que FrameRenderer con él.
    studio::PreviewSource& p = s.editor->previewSource();
    exporter::ProxyMap proxies;
    for (const auto& [source, px] : maker.proxies()) {
        proxies[source] = {px.file, px.source_width, px.source_height};
    }
    REQUIRE(QTest::qWaitFor([&] { return p.tick() == 0 && p.scale() < 1.0; }, 60000));
    const render::Image expected = exported(s.dir, 0.0, p.scale(), proxies);
    CHECK(QTest::qWaitFor([&] { return same_pixels(p.image(), expected); }, 60000));
    // Y el proxy se ve: el clip «a» deja de ser el negro de «sin material».
    CHECK(has_color(expected, QColor(40, 90, 160)));
}
#endif

#ifdef GC_STUDIO_PREVIEW
TEST_CASE("studio sin pantalla: exportar deja el MP4 en exports, con su progreso y sus avisos", "[studio][ui][export]") {
    const Studio s;
    studio::ExportJob& job = s.editor->exportJob();
    REQUIRE(job.available());
    s.click("editor:export");
    REQUIRE(job.running());
    CHECK(s.item("export:card"));
    CHECK(s.item("export:cancel"));
    REQUIRE(QTest::qWaitFor([&] { return !job.running(); }, 300000));
    INFO(job.message().toStdString());
    REQUIRE(job.error().isEmpty());
    CHECK(job.message() == "Vídeo final exportado.");
    CHECK(job.progress() == 1.0);
    // Como jobs._run_export: <carpeta del proyecto>/exports/final_<UTC>.mp4.
    const auto file = core::path_from_utf8(job.file().toStdString());
    REQUIRE(std::filesystem::is_regular_file(file));
    auto project = s.store->get_project("p1");
    REQUIRE(project);
    REQUIRE(*project);
    CHECK(file.parent_path() == core::project_base(s.store->clips_dir(), **project) / "exports");
    CHECK(core::path_to_utf8(file.filename()).starts_with("final_"));
    CHECK(std::abs(media::probe_duration(file) - 9.0) < 0.1); // «b» acaba a los 9 s
    CHECK(job.warnings().join('\n').contains("a.mp4"));       // «a» no tiene material: se omite
    CHECK(s.item("export:open"));
    CHECK(s.item("export:warnings"));
    s.click("export:close");
    CHECK(job.message().isEmpty());
    CHECK_FALSE(s.item("export:card"));
}

TEST_CASE("studio sin pantalla: un export cancelado no deja nada y una timeline vacía no se exporta", "[studio][ui][export]") {
    const Studio s;
    studio::ExportJob& job = s.editor->exportJob();
    REQUIRE(s.editor->exportVideo());
    job.cancel();
    REQUIRE(QTest::qWaitFor([&] { return !job.running(); }, 300000));
    CHECK(job.message() == "Export cancelado.");
    CHECK(job.file().isEmpty());
    auto project = s.store->get_project("p1");
    REQUIRE(project);
    REQUIRE(*project);
    const auto folder = core::project_base(s.store->clips_dir(), **project) / "exports";
    CHECK((!std::filesystem::exists(folder) || std::filesystem::is_empty(folder))); // sin el MP4 a medias

    for (const char* id : {"a", "b"}) {
        s.editor->select(id);
        s.editor->deleteSelection();
    }
    CHECK_FALSE(s.editor->exportVideo());
    CHECK_FALSE(job.running());
    CHECK(job.error() == "No hay nada en la timeline para exportar.");
}
#endif

#ifdef GC_STUDIO_PREVIEW
namespace {

// Un vídeo vertical de w × h a 30 fps en tres franjas horizontales: rojo, verde y azul.
void write_bands(const std::filesystem::path& path, int w, int h, int frames) {
    std::filesystem::create_directories(path.parent_path());
    media::EncodeSettings settings;
    settings.width = w;
    settings.height = h;
    settings.fps = 30;
    settings.audio = false;
    auto writer = media::Mp4Writer::open(path, settings);
    REQUIRE(writer);
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(w) * h * 4, 255);
    for (int y = 0; y < h; ++y) {
        const int band = 3 * y / h;
        for (int x = 0; x < w; ++x) {
            std::uint8_t* p = &rgba[((static_cast<std::size_t>(y) * w) + x) * 4];
            p[0] = band == 0 ? 255 : 0;
            p[1] = band == 1 ? 255 : 0;
            p[2] = band == 2 ? 255 : 0;
        }
    }
    for (int k = 0; k < frames; ++k) {
        REQUIRE(writer->video(rgba));
    }
    REQUIRE(writer->finish());
}

bool green(QColor c) {
    return c.green() > 180 && c.red() < 90 && c.blue() < 90;
}

} // namespace

TEST_CASE("miniaturas: el primer fotograma recortado al centro, en la caché, y caducan con el archivo",
          "[studio][thumbs]") {
    app();
    const auto dir = std::filesystem::temp_directory_path() /
                     ("gc_thumbs_" + std::to_string(QCoreApplication::applicationPid()));
    std::filesystem::remove_all(dir);
    const auto cache = dir / "thumbs";
    const auto video = dir / "v.mp4";
    write_bands(video, 180, 320, 3);
    CHECK_FALSE(studio::find_thumbnail(cache, video));

    // Como `object-fit: cover`: el vídeo de 9:16 cubre el recuadro de 440 × 248 y se queda la franja
    // del medio (encajado, se verían las tres; estirado, el rojo arriba).
    const auto made = studio::make_thumbnail(cache, video);
    REQUIRE(made);
    CHECK(made->parent_path() == cache);
    CHECK(made->extension() == ".jpg");
    CHECK(studio::find_thumbnail(cache, video) == *made);
    const QImage thumb(QString::fromStdString(core::path_to_utf8(*made)));
    REQUIRE(thumb.size() == QSize(studio::kThumbWidth, studio::kThumbHeight));
    for (const int y : {2, studio::kThumbHeight / 2, studio::kThumbHeight - 3}) {
        for (const int x : {2, studio::kThumbWidth / 2, studio::kThumbWidth - 3}) {
            INFO(x << ", " << y);
            CHECK(green(thumb.pixelColor(x, y)));
        }
    }
    const auto again = studio::make_thumbnail(cache, video); // ya hecha: la misma
    REQUIRE(again);
    CHECK(*again == *made);

    // Otro vídeo en el mismo archivo: la de antes ya no es la suya.
    write_bands(video, 320, 180, 5);
    CHECK_FALSE(studio::find_thumbnail(cache, video));
    const auto remade = studio::make_thumbnail(cache, video);
    REQUIRE(remade);
    CHECK(*remade != *made);

    // Lo que no se puede leer: un error y nada a medias en la caché.
    const auto broken = dir / "roto.mp4";
    std::ofstream(broken, std::ios::binary) << "no es un vídeo";
    CHECK_FALSE(studio::make_thumbnail(cache, broken));
    CHECK_FALSE(studio::make_thumbnail(cache, dir / "no_existe.mp4"));
    CHECK_FALSE(studio::make_thumbnail({}, video)); // sin caché
    for (const auto& entry : std::filesystem::directory_iterator(cache)) {
        CHECK(entry.path().extension() == ".jpg");
    }
    std::filesystem::remove_all(dir);
}

TEST_CASE("studio sin pantalla: la tarjeta del inicio enseña la miniatura de su primer clip",
          "[studio][ui][thumbs]") {
    const Studio s;
    // «p1» con un primer clip (un vídeo de su carpeta) y «p2» sin ninguno.
    auto project = s.store->get_project("p1");
    REQUIRE(project);
    REQUIRE(*project);
    write_bands(core::project_base(s.store->clips_dir(), **project) / "video" / "v.mp4", 180, 320, 3);
    core::SequentialIdSource ids;
    core::ClipInfo first;
    first.filename = "v.mp4";
    first.url = "/api/media/p1/video/v.mp4";
    first.end = 0.1;
    REQUIRE(s.store->add_clips("p1", {first}, ids));
    REQUIRE(s.store->create_project("Dos", std::nullopt, ids));
    s.editor->closeProject();
    s.home->reload();

    REQUIRE(QTest::qWaitFor([&] { return s.home->thumbs().contains("p1"); }, 60000));
    const auto file = core::path_from_utf8(s.home->thumbs().value("p1").toString().toStdString());
    CHECK(file.parent_path() == s.dir / "thumbs"); // en la caché, no en la carpeta del proyecto
    CHECK(s.home->thumbs().size() == 1);
    QQuickItem* image = s.item("project:thumb:p1");
    REQUIRE(image);
    CHECK(image->property("loaded").toBool());

    // Se pinta en el recuadro con las esquinas de arriba redondeadas: el centro es el vídeo y la
    // esquina, el fondo de la página.
    REQUIRE(QTest::qWaitFor([&] {
        const QImage shot = s.window->grabWindow();
        const QPoint center = image->mapToScene(QPointF(image->width() / 2, image->height() / 2)).toPoint();
        return green(shot.pixelColor(center));
    }, 5000));
    const QImage shot = s.window->grabWindow();
    CHECK_FALSE(green(shot.pixelColor(image->mapToScene(QPointF(1, 1)).toPoint())));
    CHECK(green(shot.pixelColor(image->mapToScene(QPointF(image->width() / 2, 2)).toPoint())));

    // Renombrar no recrea la tarjeta sin su miniatura.
    s.home->renameProject("p1", "Uno bis");
    CHECK(s.home->thumbs().contains("p1"));
}
#endif
