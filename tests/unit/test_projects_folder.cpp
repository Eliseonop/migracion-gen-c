// La carpeta de los proyectos (apps/studio/projects_folder.hpp): con cuál arranca el studio, cambiarla
// desde Ajustes (relee el inicio y los ajustes) y recordarla en studio.json.

#include <QCoreApplication>
#include <QDir>
#include <QUrl>
#include <filesystem>
#include <optional>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "gc/core/io.hpp"

#include "home_model.hpp"
#include "projects_folder.hpp"
#include "settings_model.hpp"

using namespace gc;
namespace fs = std::filesystem;

namespace {

struct Fixture {
    fs::path dir = fs::temp_directory_path() / ("gc_projects_folder_" + std::to_string(QCoreApplication::applicationPid()));
    fs::path config = dir / "config" / "studio.json";
    fs::path usual = dir / "siempre"; // la carpeta de siempre (la de datos de la app)
    core::SequentialIdSource ids;

    Fixture() {
        fs::remove_all(dir);
        fs::create_directories(usual);
    }
    ~Fixture() {
        try { // el remove_all de MSVC puede lanzar bad_alloc al copiar rutas
            fs::remove_all(dir);
        } catch (...) { // NOLINT(bugprone-empty-catch): la carpeta temporal se queda, el destructor no lanza
        }
    }

    // Una carpeta de datos con un proyecto que se llama `name`.
    void library(const fs::path& root, const std::string& name) {
        fs::create_directories(root);
        core::ProjectsStore store(root / "data" / "projects.json", root / "clips");
        REQUIRE(store.create_project(name, std::nullopt, ids));
    }
};

QString text(const fs::path& p) { return QString::fromStdString(core::path_to_utf8(p)); }

QStringList names(const QVariantList& list) {
    QStringList out;
    for (const QVariant& v : list) {
        out << v.toMap()["name"].toString();
    }
    return out;
}

} // namespace

TEST_CASE("carpeta de los proyectos: arranca con --root, si no con la guardada y, si ya no está, con la de siempre",
          "[studio]") {
    Fixture f;
    const fs::path other = f.dir / "otra";
    fs::create_directories(other);

    auto start = studio::ProjectsFolder::start({}, f.config, f.usual); // nada guardado
    CHECK(start.root == f.usual);
    CHECK_FALSE(start.from_cli);
    CHECK(start.notice.isEmpty());

    fs::create_directories(f.config.parent_path());
    REQUIRE(core::write_json_file(f.config, {{"root", core::path_to_utf8(other)}}));
    start = studio::ProjectsFolder::start({}, f.config, f.usual);
    CHECK(fs::equivalent(start.root, other));
    CHECK(start.notice.isEmpty());

    start = studio::ProjectsFolder::start(text(f.usual), f.config, f.usual); // --root manda
    CHECK(fs::equivalent(start.root, f.usual));
    CHECK(start.from_cli);

    fs::remove_all(other); // un disco que no está: la de siempre, con aviso, y se sigue recordando
    start = studio::ProjectsFolder::start({}, f.config, f.usual);
    CHECK(start.root == f.usual);
    CHECK(start.notice.contains("No encuentro"));
    CHECK(start.notice.contains(QDir::toNativeSeparators(text(other))));
    CHECK(studio::ProjectsFolder::saved(f.config));
}

TEST_CASE("carpeta de los proyectos: cambiarla relee el inicio y los ajustes, y se recuerda", "[studio]") {
    Fixture f;
    const fs::path other = f.dir / "otra";
    f.library(f.usual, "De siempre");
    f.library(other, "De la otra");
    core::ProjectsStore store(f.usual / "data" / "projects.json", f.usual / "clips");
    core::SettingsFile settings_file(f.usual / "data" / "settings.json");
    studio::HomeModel home(store, f.ids);
    studio::SettingsModel settings(settings_file);
    settings.setExport(60, "high");
    studio::ProjectsFolder folder(store, settings_file, f.config, f.usual);
    folder.setModels(&home, &settings, nullptr);
    CHECK(folder.isDefault());
    CHECK(folder.projectCount() == 1);
    CHECK(folder.blocker().isEmpty()); // sin editor, nada lo impide

    // El selector de carpetas da una URL file:///.
    REQUIRE(folder.choose(QUrl::fromLocalFile(text(other)).toString()));
    CHECK(fs::equivalent(folder.root(), other));
    CHECK(folder.path() == QDir::toNativeSeparators(text(folder.root())));
    CHECK_FALSE(folder.isDefault());
    CHECK(folder.message().startsWith("Listo"));
    CHECK(folder.error().isEmpty());
    CHECK(names(home.projects()) == QStringList{"De la otra"});
    CHECK(folder.projectCount() == 1);
    CHECK(settings.fps() == 30); // los ajustes son los de la otra: de fábrica
    REQUIRE(studio::ProjectsFolder::saved(f.config));
    CHECK(fs::equivalent(*studio::ProjectsFolder::saved(f.config), other));

    // Lo nuevo va a la otra; la de siempre no cambia.
    home.createProject("Nuevo", {});
    CHECK(folder.projectCount() == 2);
    CHECK(core::ProjectsStore(other / "data" / "projects.json", other / "clips").list_projects()->size() == 2);
    CHECK(core::ProjectsStore(f.usual / "data" / "projects.json", f.usual / "clips").list_projects()->size() == 1);

    // La misma otra vez: nada cambia.
    REQUIRE(folder.choose(text(other)));
    CHECK(folder.message() == "Ya estás usando esa carpeta.");

    // «Usar la de siempre» vuelve y olvida la guardada.
    REQUIRE(folder.useDefault());
    CHECK(folder.isDefault());
    CHECK(names(home.projects()) == QStringList{"De siempre"});
    CHECK(settings.fps() == 60);
    CHECK_FALSE(studio::ProjectsFolder::saved(f.config));
}

TEST_CASE("carpeta de los proyectos: la de video-yt entra por backend/ y una nueva empieza vacía", "[studio]") {
    Fixture f;
    const fs::path videoyt = f.dir / "video-yt";
    f.library(videoyt / "backend", "De video-yt");
    core::ProjectsStore store(f.usual / "data" / "projects.json", f.usual / "clips");
    core::SettingsFile settings_file(f.usual / "data" / "settings.json");
    studio::HomeModel home(store, f.ids);
    studio::ProjectsFolder folder(store, settings_file, f.config, f.usual);
    folder.setModels(&home, nullptr, nullptr);

    REQUIRE(folder.choose(QDir::toNativeSeparators(text(videoyt)))); // la de video-yt, no su backend/
    CHECK(fs::equivalent(folder.root(), videoyt / "backend"));
    CHECK(names(home.projects()) == QStringList{"De video-yt"});

    const fs::path fresh = f.dir / "nueva" / "proyectos"; // no existe: se crea
    REQUIRE(folder.choose(text(fresh)));
    CHECK(fs::is_directory(fresh / "data"));
    CHECK(home.projects().isEmpty());
    CHECK(folder.projectCount() == 0);
    home.createProject("Primero", {});
    CHECK(fs::exists(fresh / "data" / "projects.json"));

    CHECK_FALSE(folder.choose("  "));
    CHECK(folder.error() == "Elige una carpeta.");
    CHECK(folder.message().isEmpty());
    CHECK(fs::equivalent(folder.root(), fresh));
}
