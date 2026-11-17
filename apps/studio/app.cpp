// genclip-studio: el editor de escritorio (F5).
//
//   genclip-studio [--root <carpeta>] [--open <proyecto> [--select <clip>] [--at <s>]] [--grab <png>]
//
// <carpeta> tiene data/projects.json y clips/, como backend/ de video-yt (así se abren tus
// proyectos de siempre). Sin --root, la carpeta de datos de la app. --at pone el cabezal. --grab
// guarda una captura de la ventana al arrancar (con un proyecto abierto, cuando la vista previa ya
// tiene su fotograma) y sale: revisión visual sin pantalla con QT_QPA_PLATFORM=offscreen.

#include "app.hpp"

#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

#include "gc/core/fx_catalog.hpp"
#include "gc/core/io.hpp"

#include "editor_model.hpp"
#include "frame_preview.hpp"
#include "home_model.hpp"
#include "settings_model.hpp"
#include "thumbnails.hpp"

#ifdef GC_STUDIO_PRELOAD_CONTROLS_IMPL
// Las genera Qt en Qt6QuickControls2Impl y Qt6QuickControls2BasicStyleImpl (ver el CMakeLists).
void qml_register_types_QtQuick_Controls_impl();       // NOLINT(readability-identifier-naming)
void qml_register_types_QtQuick_Controls_Basic_impl(); // NOLINT(readability-identifier-naming)
#endif

void gc::studio::preload_qml_modules() {
#ifdef GC_STUDIO_PRELOAD_CONTROLS_IMPL
    qml_register_types_QtQuick_Controls_impl();
    qml_register_types_QtQuick_Controls_Basic_impl();
#endif
}

int gc::studio::run(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    preload_qml_modules();
    QGuiApplication::setApplicationName("GenClipPro");

    QFile catalog(":/fx/catalog.json");
    if (catalog.open(QIODevice::ReadOnly)) {
        gc::core::set_fx_catalog(std::make_shared<const gc::core::FxCatalog>(
            gc::Json::parse(catalog.readAll().toStdString(), nullptr, false)));
    }

    QCommandLineParser args;
    args.addHelpOption();
    args.addOption({"root", "Carpeta con data/projects.json y clips/.", "carpeta"});
    args.addOption({"grab", "Guarda una captura de la ventana y sale.", "png"});
    args.addOption({"open", "Abre ese proyecto en el editor.", "id"});
    args.addOption({"select", "Con --open, selecciona ese clip.", "clip"});
    args.addOption({"at", "Con --open, pone el cabezal en ese instante.", "segundos"});
    args.process(app);
    const QString root = args.isSet("root")
                             ? args.value("root")
                             : QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(root);

    const auto base = gc::core::path_from_utf8(QDir(root).absolutePath().toStdString());
    // Las miniaturas del inicio y los proxies de la vista previa, en la caché de la app (no en la
    // carpeta del proyecto).
    const auto cache =
        gc::core::path_from_utf8(QStandardPaths::writableLocation(QStandardPaths::CacheLocation).toStdString());
    gc::core::ProjectsStore store(base / "data" / "projects.json", base / "clips");
    gc::core::RandomIdSource ids;
    gc::studio::Thumbnails thumbnails;
    thumbnails.setCacheDir(cache / "thumbnails");
    gc::studio::HomeModel home(store, ids, &thumbnails);
    gc::core::SettingsFile settings_file(base / "data" / "settings.json");
    gc::studio::EditorModel editor(store, &settings_file);
    editor.setProxyDir(cache / "proxies");
    gc::studio::SettingsModel settings(settings_file);

    gc::studio::register_qml_types();
    QQmlApplicationEngine engine;
    engine.setInitialProperties(
        {{"home", QVariant::fromValue(&home)}, {"editor", QVariant::fromValue(&editor)},
                                 {"settings", QVariant::fromValue(&settings)}});
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); },
        Qt::QueuedConnection);
    if (args.isSet("open")) {
        editor.openProject(args.value("open"));
        if (args.isSet("select")) {
            editor.select(args.value("select"));
        }
        if (args.isSet("at")) {
            editor.setPlayhead(args.value("at").toDouble());
        }
    }
    engine.load(QUrl("qrc:/qt/qml/GenClip/Studio/Main.qml"));
    QTimer poll;
    if (args.isSet("grab") && !engine.rootObjects().isEmpty()) {
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().front());
        // Medio segundo para que se pinte y, con un proyecto abierto, hasta que la vista previa tenga
        // su fotograma (o diga por qué no), con un tope.
        QObject::connect(&poll, &QTimer::timeout, &app, [&editor, window, file = args.value("grab"), waited = 0] mutable {
            waited += 100;
            const PreviewSource& preview = editor.previewSource();
            const bool ready = !editor.isOpen() || !preview.available() || preview.hasFrame() || !preview.error().isEmpty();
            if (waited >= 500 && (ready || waited >= 60000)) {
                QCoreApplication::exit(window && window->grabWindow().save(file) ? 0 : 1);
            }
        });
        poll.start(100);
    }
    return QGuiApplication::exec();
}
