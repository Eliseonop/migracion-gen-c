// Ajustes › Versión (apps/studio/updater.hpp): las versiones, las releases locales y la de GitHub, buscar (con una
// API de mentira) y preparar la actualización sin lanzarla.

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QHostAddress>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "gc/core/io.hpp"
#include "gc/core/version.hpp"

#include "qt_app.hpp"
#include "updater.hpp"

using namespace gc;
namespace fs = std::filesystem;
using studio::Updater;

namespace {

struct TempDir {
    fs::path dir = fs::temp_directory_path() / ("gc_updater_" + std::to_string(QCoreApplication::applicationPid()));
    TempDir() {
        fs::remove_all(dir);
        fs::create_directories(dir);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};

void write(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

QString text(const fs::path& p) { return QString::fromStdString(core::path_to_utf8(p)); }
// Como la da QUrl::toLocalFile: con «/».
QString qt_path(const fs::path& p) { return QDir::fromNativeSeparators(text(p)); }

Updater::Version current() {
    return *Updater::parseVersion(QString::fromUtf8(gc::kVersion.data(), static_cast<qsizetype>(gc::kVersion.size())));
}

// La versión siguiente a la de esta app, en texto («0.23.0» → «0.24.0»).
QString next_minor() {
    const auto v = current();
    return QString("%1.%2.0").arg(v.major).arg(v.minor + 1);
}

// Un servidor HTTP de mentira: en las rutas de `files`, ese archivo; en las demás, `body` con `status` (la API de
// GitHub).
struct FakeApi {
    QTcpServer server;
    QByteArray body;
    int status = 200;
    std::map<QByteArray, QByteArray> files; // el .zip y el .sha256 de la release de GitHub
    FakeApi() {
        REQUIRE(server.listen(QHostAddress::LocalHost));
        QObject::connect(&server, &QTcpServer::newConnection, [this] {
            QTcpSocket* s = server.nextPendingConnection();
            QObject::connect(s, &QTcpSocket::disconnected, s, &QObject::deleteLater);
            auto request = std::make_shared<QByteArray>();
            QObject::connect(s, &QTcpSocket::readyRead, s, [this, s, request] {
                *request += s->readAll();
                if (!request->contains("\r\n\r\n")) {
                    return;
                }
                const auto file = files.find(request->split(' ').value(1)); // GET <ruta> HTTP/1.1
                const QByteArray& out = file != files.end() ? file->second : body;
                const int code = file != files.end() ? 200 : status;
                s->write("HTTP/1.1 " + QByteArray::number(code) + " OK\r\nContent-Length: " +
                         QByteArray::number(out.size()) + "\r\nConnection: close\r\n\r\n" + out);
                s->disconnectFromHost();
            });
        });
    }
    [[nodiscard]] QUrl url(const QString& path = "/latest") const {
        return QUrl(QString("http://127.0.0.1:%1%2").arg(server.serverPort()).arg(path));
    }
};

} // namespace

TEST_CASE("actualizar: las versiones, las releases locales y la de GitHub", "[studio][updater]") {
    CHECK(Updater::parseVersion("0.23.0") == Updater::Version{0, 23, 0});
    CHECK(Updater::parseVersion("v1.2.10") == Updater::Version{1, 2, 10});
    CHECK_FALSE(Updater::parseVersion("1.2"));
    CHECK_FALSE(Updater::parseVersion("v1.2.3-beta"));
    CHECK(*Updater::parseVersion("0.10.0") > *Updater::parseVersion("0.9.9"));

    TempDir t;
    for (const char* name : {"GenClipPro-0.22.0-win64.zip", "GenClipPro-0.24.0-win64.zip", "GenClipPro-0.23.5-win64.zip",
                             "GenClipPro-9.9.9-linux.zip", "otro.zip"}) {
        write(t.dir / name, "zip");
    }
    write(t.dir / "GenClipPro-0.24.0-win64.zip.sha256", "abc");
    const auto best = Updater::newestLocal(t.dir, {0, 23, 0});
    REQUIRE(best);
    CHECK(best->text == "0.24.0");
    CHECK(best->source == "local");
    CHECK(best->zip.toLocalFile() == qt_path(t.dir / "GenClipPro-0.24.0-win64.zip"));
    CHECK(best->sha256.toLocalFile() == qt_path(t.dir / "GenClipPro-0.24.0-win64.zip.sha256"));
    CHECK_FALSE(Updater::newestLocal(t.dir, {0, 24, 0}));
    CHECK_FALSE(Updater::newestLocal(t.dir / "no-esta", {0, 1, 0}));

    const QByteArray json = R"({"tag_name": "v0.25.0", "draft": false, "prerelease": false, "assets": [
        {"name": "GenClipPro-0.25.0-win64.zip", "browser_download_url": "https://ejemplo/z.zip"},
        {"name": "GenClipPro-0.25.0-win64.zip.sha256", "browser_download_url": "https://ejemplo/z.sha256"},
        {"name": "notas.txt", "browser_download_url": "https://ejemplo/n.txt"}]})";
    const auto gh = Updater::fromGithub(json, {0, 23, 0});
    REQUIRE(gh);
    CHECK(gh->text == "0.25.0");
    CHECK(gh->source == "github");
    CHECK(gh->zip == QUrl("https://ejemplo/z.zip"));
    CHECK(gh->sha256 == QUrl("https://ejemplo/z.sha256"));
    CHECK_FALSE(Updater::fromGithub(json, {0, 25, 0}));                           // no es más nueva
    CHECK_FALSE(Updater::fromGithub(R"({"tag_name": "v0.30.0", "assets": []})", {0, 23, 0})); // sin el .zip
    CHECK_FALSE(Updater::fromGithub("no es JSON", {0, 23, 0}));

    // El script cambia la carpeta con tar.exe y vuelve a abrir la app; en ASCII (PowerShell 5.1 lo lee como ANSI).
    const QString script = Updater::updateScript();
    CHECK(script.contains("tar.exe"));
    CHECK(script.contains("genclip-studio.exe"));
    CHECK(script.toLatin1() == script.toUtf8());
}

TEST_CASE("actualizar: la de desarrollo no se actualiza; la de producción busca en este PC y en GitHub",
          "[studio][updater]") {
    test::qt_app(); // la red y el bucle de eventos
    TempDir t;
    FakeApi api;
    Updater up;
    up.setDryRun(true);
    up.setAppDir(t.dir / "app");
    up.setLocalReleases(t.dir / "releases");
    up.setApiUrl(api.url());
    CHECK_FALSE(up.installed()); // sin genclip-release.json
    CHECK(up.version() == QString::fromUtf8(gc::kVersion.data(), static_cast<qsizetype>(gc::kVersion.size())));

    // GitHub sin releases (404) y nada en este PC: al día.
    api.status = 404;
    api.body = R"({"message": "Not Found"})";
    up.check();
    REQUIRE(QTest::qWaitFor([&] { return up.status() != "checking"; }, 10000));
    CHECK(up.status() == "uptodate");

    // Una más nueva en GitHub.
    const QString newer = next_minor();
    api.status = 200;
    api.body = QString(R"({"tag_name": "v%1", "assets": [{"name": "GenClipPro-%1-win64.zip",
                          "browser_download_url": "https://ejemplo/z.zip"}]})")
                   .arg(newer)
                   .toUtf8();
    up.check();
    REQUIRE(QTest::qWaitFor([&] { return up.status() != "checking"; }, 10000));
    CHECK(up.status() == "available");
    CHECK(up.latest() == newer);
    CHECK(up.source() == "github");
    up.update(); // la de desarrollo no se toca
    CHECK(up.status() == "error");
    CHECK(up.error().startsWith("Esta es la de desarrollo"));

    // La de producción, con una release local igual de nueva: gana la de este PC, con su .sha256.
    write(t.dir / "app" / "genclip-release.json", R"({"version": "0.0.0"})");
    REQUIRE(up.installed());
    const fs::path zip = t.dir / "releases" / ("GenClipPro-" + newer.toStdString() + "-win64.zip");
    write(zip, "el zip de la release local");
    QFile z(text(zip));
    REQUIRE(z.open(QIODevice::ReadOnly));
    const QByteArray sha = QCryptographicHash::hash(z.readAll(), QCryptographicHash::Sha256).toHex();
    z.close();
    write(fs::path(zip).concat(".sha256"), "0000000000000000000000000000000000000000000000000000000000000000  x.zip\n");
    up.check();
    REQUIRE(QTest::qWaitFor([&] { return up.status() != "checking"; }, 10000));
    CHECK(up.status() == "available");
    CHECK(up.source() == "local");
    CHECK(up.message().contains("de una release de este PC"));

    // Un .sha256 que no coincide: no se instala.
    QSignalSpy restart(&up, &Updater::readyToRestart);
    up.update();
    CHECK(up.status() == "error");
    CHECK(up.error().contains("no coincide"));
    CHECK(restart.isEmpty());

    // Con el bueno: el script listo para cambiar la carpeta de la app (en los tests no se lanza).
    write(fs::path(zip).concat(".sha256"), sha.toStdString() + "  " + zip.filename().string() + "\n");
    up.check();
    REQUIRE(QTest::qWaitFor([&] { return up.status() != "checking"; }, 10000));
    up.update();
    CHECK(up.status() == "installing");
    REQUIRE(restart.size() == 1);
    const QString script = restart.front().at(0).toString();
    const QStringList args = restart.front().at(1).toStringList();
    CHECK(QFile::exists(script));
    CHECK(args.contains("-File"));
    CHECK(args.at(args.indexOf("-Zip") + 1) == QDir::toNativeSeparators(text(zip)));
    CHECK(args.at(args.indexOf("-Install") + 1) == QDir::toNativeSeparators(text(t.dir / "app")));
    CHECK(args.at(args.indexOf("-AppPid") + 1) == QString::number(QCoreApplication::applicationPid()));
}

TEST_CASE("actualizar: la de GitHub se baja con su .sha256, se comprueba y queda lista", "[studio][updater]") {
    test::qt_app();
    TempDir t;
    FakeApi api;
    Updater up;
    up.setDryRun(true);
    up.setAppDir(t.dir / "app");
    up.setLocalReleases(t.dir / "releases");
    up.setApiUrl(api.url());
    write(t.dir / "app" / "genclip-release.json", "{}");
    const QString newer = next_minor();
    const QByteArray zip(300000, 'z'); // llega en varios trozos
    api.files["/z.zip"] = zip;
    api.files["/z.zip.sha256"] = QCryptographicHash::hash(zip, QCryptographicHash::Sha256).toHex() + "  z.zip\n";
    api.body = QString(R"({"tag_name": "v%1", "assets": [
        {"name": "GenClipPro-%1-win64.zip", "browser_download_url": "%2"},
        {"name": "GenClipPro-%1-win64.zip.sha256", "browser_download_url": "%3"}]})")
                   .arg(newer, api.url("/z.zip").toString(), api.url("/z.zip.sha256").toString())
                   .toUtf8();
    up.check();
    REQUIRE(QTest::qWaitFor([&] { return up.status() != "checking"; }, 10000));
    REQUIRE(up.status() == "available");
    CHECK(up.source() == "github");

    QSignalSpy restart(&up, &Updater::readyToRestart);
    up.update();
    CHECK(up.status() == "downloading");
    REQUIRE(QTest::qWaitFor([&] { return up.status() != "downloading"; }, 10000));
    CHECK(up.error().isEmpty());
    CHECK(up.status() == "installing");
    CHECK(up.progress() > 0.99);
    REQUIRE(restart.size() == 1);
    const QStringList args = restart.front().at(1).toStringList();
    QFile got(args.at(args.indexOf("-Zip") + 1));
    CHECK(got.fileName().endsWith(QString("GenClipPro-%1-win64.zip").arg(newer)));
    REQUIRE(got.open(QIODevice::ReadOnly));
    CHECK(got.readAll() == zip);
    got.close();
    got.remove();
}

TEST_CASE("actualizar: sin un .sha256 que valga no se instala", "[studio][updater]") {
    test::qt_app();
    TempDir t;
    FakeApi api;
    Updater up;
    up.setDryRun(true);
    up.setAppDir(t.dir / "app");
    up.setLocalReleases(t.dir / "releases");
    up.setApiUrl(api.url());
    write(t.dir / "app" / "genclip-release.json", "{}");
    REQUIRE(up.installed());
    QSignalSpy restart(&up, &Updater::readyToRestart);
    const auto look = [&up] {
        up.check();
        REQUIRE(QTest::qWaitFor([&] { return up.status() != "checking"; }, 10000));
        REQUIRE(up.status() == "available");
    };

    // La de GitHub sin el .sha256 entre sus archivos: ni se baja.
    const QString newer = next_minor();
    api.body = QString(R"({"tag_name": "v%1", "assets": [{"name": "GenClipPro-%1-win64.zip",
                          "browser_download_url": "https://ejemplo/z.zip"}]})")
                   .arg(newer)
                   .toUtf8();
    look();
    CHECK(up.source() == "github");
    up.update();
    CHECK(up.status() == "error");
    CHECK(up.error() == QString("La %1 no trae su .sha256: no se instala.").arg(newer));

    // La de este PC sin .sha256 al lado y luego con uno que no es un hash.
    const fs::path zip = t.dir / "releases" / ("GenClipPro-" + newer.toStdString() + "-win64.zip");
    write(zip, "zip");
    look();
    CHECK(up.source() == "local");
    up.update();
    CHECK(up.error().contains("no trae su .sha256"));
    write(fs::path(zip).concat(".sha256"), "no es un hash\n");
    look();
    up.update();
    CHECK(up.error() == QString("El .sha256 de la %1 no es válido: no se instala.").arg(newer));

    // El de Windows PowerShell 5.1 (con BOM) sí vale.
    QFile z(text(zip));
    REQUIRE(z.open(QIODevice::ReadOnly));
    const QByteArray sha = QCryptographicHash::hash(z.readAll(), QCryptographicHash::Sha256).toHex();
    z.close();
    write(fs::path(zip).concat(".sha256"), "\xEF\xBB\xBF" + sha.toUpper().toStdString() + "  x.zip\r\n");
    CHECK(restart.isEmpty());
    look();
    up.update();
    CHECK(up.status() == "installing");
    CHECK(restart.size() == 1);
}
