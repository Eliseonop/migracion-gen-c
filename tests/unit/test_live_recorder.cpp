// Con qué yt-dlp busca los directos «Directo» (apps/studio/live_recorder.hpp): el orden de findYtdlp y el que se elige
// en el panel, que se recuerda en studio.json (apps/studio/studio_config.hpp).

#include <QCoreApplication>
#include <QDir>
#include <QProcessEnvironment>
#include <QUrl>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "gc/core/io.hpp"

#include "live_recorder.hpp"
#include "studio_config.hpp"

using namespace gc;
namespace fs = std::filesystem;

namespace {

struct Fixture {
    fs::path dir = fs::temp_directory_path() / ("gc_live_tools_" + std::to_string(QCoreApplication::applicationPid()));

    Fixture() {
        fs::remove_all(dir);
        fs::create_directories(dir);
    }
    ~Fixture() {
        try { // el remove_all de MSVC puede lanzar bad_alloc al copiar rutas
            fs::remove_all(dir);
        } catch (...) { // NOLINT(bugprone-empty-catch): la carpeta temporal se queda, el destructor no lanza
        }
    }
};

// Un «programa» vacío: basta con que exista (y, fuera de Windows, que se pueda ejecutar).
fs::path tool(const fs::path& path) {
    fs::create_directories(path.parent_path());
    std::ofstream(path).put('\n');
    fs::permissions(path, fs::perms::owner_exec | fs::perms::group_exec | fs::perms::others_exec,
                    fs::perm_options::add);
    return path;
}

QString text(const fs::path& p) { return QString::fromStdString(core::path_to_utf8(p)); }
QString native(const fs::path& p) { return QDir::toNativeSeparators(text(p)); }

} // namespace

TEST_CASE("Directo: yt-dlp se busca en la variable, el elegido, el PATH, winget y la venv de la carpeta",
          "[studio][live]") {
    Fixture f;
    const fs::path videoyt = f.dir / "video-yt";
    const fs::path local = f.dir / "local"; // LOCALAPPDATA
    const fs::path bin = f.dir / "bin";     // la carpeta del PATH
    fs::create_directories(videoyt / "backend");
    fs::create_directories(bin);
    // La carpeta de los proyectos es el backend/ de video-yt, como con --root.
    studio::LiveRecorder::YtdlpPlaces places{{}, std::nullopt, {native(bin)}, local, videoyt / "backend"};
    CHECK(studio::LiveRecorder::findYtdlp(places).isEmpty());

    // La venv de video-yt (la del padre de la carpeta).
    const fs::path venv = tool(videoyt / ".venv" / "Scripts" / "yt-dlp.exe");
    CHECK(studio::LiveRecorder::findYtdlp(places) == native(venv));

    // winget (instalado con el studio abierto), antes que la venv.
    const fs::path winget = tool(local / "Microsoft" / "WinGet" / "Packages" /
                                 "yt-dlp.yt-dlp_Microsoft.Winget.Source_8wekyb3d8bbwe" / "yt-dlp.exe");
    CHECK(studio::LiveRecorder::findYtdlp(places) == native(winget));

    // El PATH, antes que winget.
#ifdef _WIN32
    const fs::path on_path = tool(bin / "yt-dlp.exe");
#else
    const fs::path on_path = tool(bin / "yt-dlp");
#endif
    CHECK(studio::LiveRecorder::findYtdlp(places) == native(on_path));

    // El elegido, antes que el PATH; si ya no está, el siguiente.
    places.chosen = tool(f.dir / "elegido" / "yt-dlp.exe");
    CHECK(studio::LiveRecorder::findYtdlp(places) == native(*places.chosen));
    fs::remove(*places.chosen);
    CHECK(studio::LiveRecorder::findYtdlp(places) == native(on_path));

    // GENCLIP_YTDLP manda, tal cual.
    places.env = "C:/a/mano/yt-dlp.exe";
    CHECK(studio::LiveRecorder::findYtdlp(places) == "C:/a/mano/yt-dlp.exe");
}

TEST_CASE("Directo: el yt-dlp elegido se usa, se recuerda en studio.json y vale en el arranque siguiente",
          "[studio][live]") {
    Fixture f;
    const fs::path config = f.dir / "config" / "studio.json";
    REQUIRE(studio::studio_config::set(config, "root", "D:/proyectos")); // lo demás de studio.json se queda
    const QProcessEnvironment none; // sin GENCLIP_YTDLP, PATH ni LOCALAPPDATA: ningún yt-dlp

    studio::LiveRecorder live;
    live.setEnvironment(none);
    live.setConfig(config);
    CHECK(live.ytdlpPath().isEmpty());

    // Lo que no es un archivo: el aviso, y no cambia nada.
    CHECK_FALSE(live.chooseYtdlp(QUrl::fromLocalFile(text(f.dir / "no-esta.exe")).toString()));
    CHECK(live.status() == "error");
    CHECK(live.error().startsWith("No encuentro el programa"));
    CHECK_FALSE(live.chooseYtdlp("  "));
    CHECK(live.error() == "Elige el programa yt-dlp.");
    CHECK_FALSE(studio::studio_config::get(config, "ytdlp"));

    // El selector da una URL file:///.
    const fs::path chosen = tool(f.dir / "venv" / "Scripts" / "yt-dlp.exe");
    REQUIRE(live.chooseYtdlp(QUrl::fromLocalFile(text(chosen)).toString()));
    CHECK(live.ytdlpPath() == native(chosen));
    CHECK(live.status().isEmpty());
    CHECK(live.error().isEmpty());
    CHECK(live.message() == "Listo: los directos se buscan con «" + native(chosen) + "».");
    REQUIRE(studio::studio_config::get(config, "ytdlp"));
    CHECK(fs::equivalent(core::path_from_utf8(*studio::studio_config::get(config, "ytdlp")), chosen));
    CHECK(studio::studio_config::get(config, "root") == "D:/proyectos");

    // El arranque siguiente, con el mismo studio.json.
    studio::LiveRecorder again;
    again.setEnvironment(none);
    again.setConfig(config);
    CHECK(again.ytdlpPath() == native(chosen));

    // Si ya no está, «Grabar» lo dice y cómo tenerlo.
    fs::remove(chosen);
    again.setProject("p1", f.dir / "live");
    again.start("https://www.tiktok.com/@prueba/live");
    CHECK(again.status() == "error");
    CHECK(again.error().startsWith("No encuentro yt-dlp"));
    CHECK(again.error().contains("winget install yt-dlp"));
    CHECK(again.ytdlpPath().isEmpty());
}
