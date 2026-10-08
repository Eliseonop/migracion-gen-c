// Piezas sueltas del almacén de proyectos (la paridad completa está en test_projects_parity.cpp).

#include <catch2/catch_test_macros.hpp>

#include <regex>
#include <string>

#include "gc/core/io.hpp"
#include "gc/core/projects_store.hpp"

using namespace gc::core;

TEST_CASE("utc_now_iso es el _now() de projects.py", "[projects]") {
    // datetime.now(timezone.utc).isoformat(timespec="seconds"), p. ej. 2026-10-08T12:00:00+00:00
    const std::string now = utc_now_iso();
    CHECK(std::regex_match(now, std::regex(R"(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}\+00:00)")));
}

TEST_CASE("la carpeta de un proyecto: la propia o la de por defecto", "[projects]") {
    const auto clips = path_from_utf8("clips");
    Project p;
    p.id = "abcd1234";
    CHECK(project_base(clips, p) == clips / "abcd1234");
    p.folder = "";
    CHECK(project_base(clips, p) == clips / "abcd1234"); // una carpeta vacía es la de por defecto
    p.folder = "E:/vídeos/año";
    CHECK(project_base(clips, p) == path_from_utf8("E:/vídeos/año"));
}
