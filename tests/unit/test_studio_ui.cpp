// genclip-studio entero sin pantalla: el QML real con los ViewModels, manejado con el ratón y el
// teclado de QTest. Comprueba que los gestos de la timeline llegan al modelo y a projects.json.

#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <filesystem>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "gc/core/io.hpp"

#include "editor_model.hpp"
#include "home_model.hpp"
#include "settings_model.hpp"

using namespace gc;

namespace {

// Una sola app para todo el proceso y sin destruir: hacerlo al salir, detrás de los estáticos de Qt,
// rompe.
QGuiApplication& app() {
    static int argc = 1;
    static char name[] = "gc_tests";
    static char* argv[] = {name, nullptr};
    qputenv("QT_QPA_PLATFORM", "offscreen");
    static auto* instance = new QGuiApplication(argc, argv);
    return *instance;
}

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
                         {"clips", Json::array({clip("a", "V1", 0, 4), clip("b", "V1", 6, 3)})}};
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
        home = std::make_unique<studio::HomeModel>(*store, ids);
        editor = std::make_unique<studio::EditorModel>(*store);
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
        std::filesystem::remove_all(dir);
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
    void click(const QString& name) {
        QQuickItem* i = item(name);
        REQUIRE(i);
        QTest::mouseClick(window, Qt::LeftButton, {}, i->mapToScene(QPointF(i->width() / 2, i->height() / 2)).toPoint());
        QCoreApplication::processEvents();
    }
    void drag(QPoint from, QPoint to, Qt::KeyboardModifiers mods = {}) {
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
    Studio s;
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
    Studio s;
    const double pps = s.editor->pps();
    const QPoint edge = s.at("a", 1.0) - QPoint(2, 0);
    s.drag(edge, edge + QPoint(static_cast<int>(5 * pps), 0)); // a: 4 s → hasta «b» (en 6)
    CHECK(s.view("a")["duration"].toDouble() == 6.0);
}

TEST_CASE("studio sin pantalla: arrastrar a la fila de encima cambia de pista; S corta y Supr borra",
          "[studio][ui]") {
    Studio s;
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
    Studio s;
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
    Studio s;
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

TEST_CASE("studio sin pantalla: arrastrar material del panel a la timeline crea el clip", "[studio][ui]") {
    Studio s;
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
    Studio s;
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
    Studio s;
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
    Studio s;
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
    Studio s;
    QQuickItem* lane = s.item("clip:b")->parentItem();
    const QPoint from = s.at("b", 0.5);
    const double grab = from.x() - s.at("b", 0.0).x();
    const QPoint edge(s.window->width() - 30, from.y());
    QTest::mousePress(s.window, Qt::LeftButton, {}, from);
    for (int i = 1; i <= 8; ++i) {
        QTest::mouseMove(s.window, from + (edge - from) * i / 8);
    }
    const double before = lane->mapFromScene(QPointF(edge)).x();
    QTest::qWait(300);
    CHECK(lane->mapFromScene(QPointF(edge)).x() > before + 100); // la vista ha corrido bajo el puntero
    QTest::mouseRelease(s.window, Qt::LeftButton, {}, edge);
    QCoreApplication::processEvents();
    const double reached = (before - grab) / s.editor->pps(); // donde caería sin desplazar
    CHECK(s.view("b")["start"].toDouble() > reached + 1);
}
