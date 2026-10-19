// El ViewModel del inicio (apps/studio/home_model.hpp): lo que Home.jsx hace por su cuenta antes
// de llamar a la API.

#include <QCoreApplication>
#include <filesystem>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "gc/core/io.hpp"

#include "home_model.hpp"

using namespace gc;

namespace {

struct Fixture {
    std::filesystem::path dir = std::filesystem::temp_directory_path() /
                                ("gc_home_model_" + std::to_string(QCoreApplication::applicationPid()));
    core::SequentialIdSource ids;
    core::ProjectsStore store{dir / "data" / "projects.json", dir / "clips",
                              [] { return std::string("2026-10-19T10:00:00+00:00"); }};

    Fixture() { std::filesystem::remove_all(dir); }
    ~Fixture() { std::filesystem::remove_all(dir); }
};

QStringList names(const QVariantList& list) {
    QStringList out;
    for (const QVariant& v : list) {
        out << v.toMap()["name"].toString();
    }
    return out;
}

} // namespace

TEST_CASE("inicio: la raíz enseña los proyectos sueltos y cada carpeta los suyos", "[studio]") {
    Fixture f;
    studio::HomeModel home(f.store, f.ids);
    CHECK(home.empty());

    home.createGroup("  Shorts  ");
    const QString gid = home.groups().front().toMap()["id"].toString();
    home.createProject("Suelto", "");
    const QString pid = home.createProject("Dentro", gid);
    CHECK_FALSE(pid.isEmpty());
    CHECK(home.groupId() == gid); // crear entra en la carpeta del proyecto
    CHECK(names(home.projects()) == QStringList{"Dentro"});
    CHECK(home.groupName() == "Shorts");

    home.openGroup("");
    CHECK(names(home.projects()) == QStringList{"Suelto"});
    CHECK(home.rootCount() == 1);
    CHECK(home.groups().front().toMap()["count"] == 1);
    CHECK(home.projects().front().toMap()["date"] == "2026-10-19");
}

TEST_CASE("inicio: nombres vacíos o iguales no llaman al almacén", "[studio]") {
    Fixture f;
    studio::HomeModel home(f.store, f.ids);
    CHECK(home.createProject("   ", "").isEmpty());
    CHECK(home.empty());

    const QString pid = home.createProject("Uno", "");
    home.renameProject(pid, "  ");
    home.renameProject(pid, " Dos ");
    CHECK(names(home.projects()) == QStringList{"Dos"});
    CHECK(home.error().isEmpty());
}

TEST_CASE("inicio: mover, duplicar y borrar; borrar la carpeta devuelve sus proyectos a la raíz",
          "[studio]") {
    Fixture f;
    studio::HomeModel home(f.store, f.ids);
    home.createGroup("A");
    const QString gid = home.groups().front().toMap()["id"].toString();
    const QString pid = home.createProject("P", "");
    home.openGroup("");

    home.moveProject(pid, gid);
    CHECK(home.projects().isEmpty());
    home.openGroup(gid);
    home.duplicateProject(pid);
    CHECK(names(home.projects()) == QStringList{"P (copia)", "P"});

    home.deleteGroup(gid);
    CHECK(home.groupId().isEmpty()); // la carpeta abierta ya no existe
    CHECK(home.rootCount() == 2);
    home.deleteProject(pid);
    CHECK(names(home.projects()) == QStringList{"P (copia)"});
}

TEST_CASE("inicio: el error del almacén queda en `error`", "[studio]") {
    Fixture f;
    studio::HomeModel home(f.store, f.ids);
    const QString pid = home.createProject("P", "");
    home.moveProject(pid, "no-existe");
    CHECK(home.error() == "La carpeta no existe.");
    home.reload();
    CHECK(home.error().isEmpty());
}
