// genclip-studio: el editor de escritorio (F5).
//
//   genclip-studio [--root <carpeta>] [--open <proyecto>] [--grab <png>]
//
// <carpeta> tiene data/projects.json y clips/, como backend/ de video-yt (así se abren tus
// proyectos de siempre). Sin --root, la carpeta de datos de la app. --grab guarda una captura de
// la ventana al arrancar y sale (revisión visual sin pantalla: QT_QPA_PLATFORM=offscreen).

#include "app.hpp"

#include <QCommandLineParser>
#include <QDir>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

#include "gc/core/io.hpp"

#include "editor_model.hpp"
#include "home_model.hpp"

int gc::studio::run(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName("GenClipPro");

    QCommandLineParser args;
    args.addHelpOption();
    args.addOption({"root", "Carpeta con data/projects.json y clips/.", "carpeta"});
    args.addOption({"grab", "Guarda una captura de la ventana y sale.", "png"});
    args.addOption({"open", "Abre ese proyecto en el editor.", "id"});
    args.process(app);
    const QString root = args.isSet("root")
                             ? args.value("root")
                             : QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(root);

    const auto base = gc::core::path_from_utf8(QDir(root).absolutePath().toStdString());
    gc::core::ProjectsStore store(base / "data" / "projects.json", base / "clips");
    gc::core::RandomIdSource ids;
    gc::studio::HomeModel home(store, ids);
    gc::studio::EditorModel editor(store);

    QQmlApplicationEngine engine;
    engine.setInitialProperties(
        {{"home", QVariant::fromValue(&home)}, {"editor", QVariant::fromValue(&editor)}});
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); },
        Qt::QueuedConnection);
    if (args.isSet("open")) {
        editor.openProject(args.value("open"));
    }
    engine.load(QUrl("qrc:/qt/qml/GenClip/Studio/Main.qml"));
    if (args.isSet("grab") && !engine.rootObjects().isEmpty()) {
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().front());
        QTimer::singleShot(500, &app, [window, file = args.value("grab")] {
            QCoreApplication::exit(window && window->grabWindow().save(file) ? 0 : 1);
        });
    }
    return QGuiApplication::exec();
}
