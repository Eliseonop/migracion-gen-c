// genclip-studio: el editor de escritorio (F5).
//
//   genclip-studio [--root <carpeta>] [--open <proyecto> [--select <clip>] [--at <s>]] [--settings] [--grab <png>]
//   genclip-studio --version
//   genclip-studio --import-keys <carpeta de video-yt>   (trae sus API keys a los ajustes de aquí y sale)
//
// --version y --import-keys lo dicen en la consola si la salida está redirigida (`| Out-Host`) y, si no
// (es una app de ventana: en Windows no escribe en la consola que la lanza), en un cuadro.
//
// <carpeta> tiene data/projects.json y clips/, como backend/ de video-yt (así se abren tus
// proyectos de siempre). Sin --root, la elegida en Ajustes › Carpeta de los proyectos (studio.json, en
// la configuración de la app) o, si no hay, la de datos de la app. --at pone el cabezal y --settings
// arranca en Ajustes. --grab guarda una captura de la ventana al arrancar (con un proyecto abierto,
// cuando la vista previa ya tiene su fotograma) y sale: revisión visual sin pantalla con
// QT_QPA_PLATFORM=offscreen.

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
#include <cstdio>
#include <cstdlib>

#include "gc/core/fx_catalog.hpp"
#include "gc/core/io.hpp"
#include "gc/core/version.hpp"

#include "editor_model.hpp"
#include "frame_preview.hpp"
#include "home_model.hpp"
#include "projects_folder.hpp"
#include "settings_model.hpp"
#include "thumbnails.hpp"
#include "updater.hpp"

#ifdef GC_STUDIO_PRELOAD_CONTROLS_IMPL
// Las genera Qt en Qt6QuickControls2Impl y Qt6QuickControls2BasicStyleImpl (ver el CMakeLists).
void qml_register_types_QtQuick_Controls_impl();       // NOLINT(readability-identifier-naming)
void qml_register_types_QtQuick_Controls_Basic_impl(); // NOLINT(readability-identifier-naming)
#endif

namespace {

// Lo que dice una opción que sale sin abrir la ventana (--import-keys), como --version: en la salida si está
// redirigida o hay consola y, si no, en un cuadro (QCommandLineParser, desde Qt 6.9).
[[noreturn]] void say_and_exit(const QString& text, int code) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
    QCommandLineParser::showMessageAndExit(
        code == 0 ? QCommandLineParser::MessageType::Information : QCommandLineParser::MessageType::Error,
        text + '\n', code);
#else
    std::fputs(qPrintable(text + '\n'), code == 0 ? stdout : stderr);
    std::exit(code);
#endif
}

} // namespace

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
    // La de CMakeLists.txt: la enseñan el título, el inicio y Ajustes (Qt.application.version) y --version.
    QGuiApplication::setApplicationVersion(
        QString::fromUtf8(gc::kVersion.data(), static_cast<qsizetype>(gc::kVersion.size())));

    QFile catalog(":/fx/catalog.json");
    if (catalog.open(QIODevice::ReadOnly)) {
        gc::core::set_fx_catalog(std::make_shared<const gc::core::FxCatalog>(
            gc::Json::parse(catalog.readAll().toStdString(), nullptr, false)));
    }

    QCommandLineParser args;
    args.addHelpOption();
    args.addVersionOption();
    args.addOption({"root", "Carpeta con data/projects.json y clips/.", "carpeta"});
    args.addOption({"grab", "Guarda una captura de la ventana y sale.", "png"});
    args.addOption({"open", "Abre ese proyecto en el editor.", "id"});
    args.addOption({"select", "Con --open, selecciona ese clip.", "clip"});
    args.addOption({"at", "Con --open, pone el cabezal en ese instante.", "segundos"});
    args.addOption({"clip", "Con --open, abre ese vídeo del material (clips:<index>) en el Clip Editor.", "clave"});
    args.addOption({"mark", "Con --clip, marca ese tramo (Z y X).", "inicio,fin"});
    args.addOption({"settings", "Arranca en Ajustes (con --grab, para verlos)."});
    args.addOption({"import-keys", "Trae las API keys de video-yt (su settings.json y sus .env) a los ajustes de aquí y sale.",
                    "carpeta"});
    args.process(app);
    // La carpeta de los proyectos: la de --root, la elegida en Ajustes o la de datos de la app.
    const auto default_root = gc::core::path_from_utf8(
        QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).absolutePath().toStdString());
    const auto folder_config =
        gc::core::path_from_utf8(QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation).toStdString()) /
        "studio.json";
    const auto start = gc::studio::ProjectsFolder::start(args.value("root"), folder_config, default_root);
    QDir().mkpath(QString::fromStdString(gc::core::path_to_utf8(start.root)));

    const auto& base = start.root;
    gc::core::SettingsFile settings_file(base / "data" / "settings.json");
    if (args.isSet("import-keys")) {
        // Antes de abrir nada más (sale sin cerrar lo abierto). Dice solo los proveedores, nunca las claves.
        const auto arrived = gc::studio::import_keys_from(
            settings_file, gc::core::path_from_utf8(QDir(args.value("import-keys")).absolutePath().toStdString()));
        if (!arrived) {
            say_and_exit(QString::fromStdString(arrived.error().message), 1);
        }
        say_and_exit(gc::studio::imported_keys_message(*arrived) + " En " +
                         QDir::toNativeSeparators(
                             QString::fromStdString(gc::core::path_to_utf8(base / "data" / "settings.json"))) +
                         ".",
                     0);
    }
    // Las miniaturas del inicio y los proxies de la vista previa, en la caché de la app (no en la
    // carpeta del proyecto).
    const auto cache =
        gc::core::path_from_utf8(QStandardPaths::writableLocation(QStandardPaths::CacheLocation).toStdString());
    gc::core::ProjectsStore store(base / "data" / "projects.json", base / "clips");
    gc::core::RandomIdSource ids;
    gc::studio::Thumbnails thumbnails;
    thumbnails.setCacheDir(cache / "thumbnails");
    gc::studio::HomeModel home(store, ids, &thumbnails);
    gc::studio::EditorModel editor(store, &settings_file);
    editor.setProxyDir(cache / "proxies");
    editor.setThumbnails(&thumbnails);
    editor.liveRecorder().setConfig(folder_config); // el yt-dlp elegido en «Directo» también va en studio.json
    gc::studio::SettingsModel settings(settings_file);
    gc::studio::ProjectsFolder folder(store, settings_file, folder_config, default_root);
    folder.setModels(&home, &settings, &editor);
    folder.setStart(start);
    gc::studio::Updater updater; // Ajustes › Versión (la de producción busca al arrancar)

    gc::studio::register_qml_types();
    QQmlApplicationEngine engine;
    engine.setInitialProperties({{"home", QVariant::fromValue(&home)},
                                 {"editor", QVariant::fromValue(&editor)},
                                 {"settings", QVariant::fromValue(&settings)},
                                 {"folder", QVariant::fromValue(&folder)},
                                 {"updater", QVariant::fromValue(&updater)}});
    QObject::connect(
        &engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(1); },
        Qt::QueuedConnection);
    if (args.isSet("open")) {
        editor.openProject(args.value("open"));
        if (args.isSet("clip") && editor.openClipEditor(args.value("clip")) && args.isSet("mark")) {
            const QStringList mark = args.value("mark").split(',');
            for (int i = 0; i < 2 && i < mark.size(); ++i) {
                editor.setPlayhead(mark[i].toDouble());
                editor.markClip(i == 0 ? "in" : "out");
            }
        }
        if (args.isSet("select")) {
            editor.select(args.value("select"));
        }
        if (args.isSet("at")) {
            editor.setPlayhead(args.value("at").toDouble());
        }
    }
    engine.load(QUrl("qrc:/qt/qml/GenClip/Studio/Main.qml"));
    if (args.isSet("settings") && !engine.rootObjects().isEmpty()) {
        engine.rootObjects().front()->setProperty("settingsOpen", true);
    }
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
