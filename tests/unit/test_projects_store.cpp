// Piezas sueltas del almacén de proyectos (la paridad completa está en test_projects_parity.cpp).

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <regex>
#include <string>
#include <string_view>

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

TEST_CASE("resolve_project_media: dentro de la carpeta, como pathlib, y sin dispositivos", "[projects]") {
    namespace fs = std::filesystem;
    const fs::path tmp = fs::temp_directory_path() / "gc_resolve_media";
    fs::create_directories(tmp / "clips" / "p1" / "video");
    Project p;
    p.id = "p1";
    const auto ok = [&](std::string_view kind, std::string_view name) {
        return resolve_project_media(tmp / "clips", p, kind, name).has_value();
    };
    CHECK(ok("video", "a.mp4"));
    CHECK(ok("video", "sub/a.mp4"));
    CHECK(ok("video", "../audio/a.wav")); // sale de video/ pero no de la carpeta del proyecto
    CHECK(ok("video", "con.mp4"));        // con extensión ya no es un dispositivo
    CHECK_FALSE(ok("otro", "a.mp4"));
    CHECK_FALSE(ok("video", ".."));       // la propia carpeta del proyecto
    CHECK_FALSE(ok("video", "../../x.mp4"));
    CHECK_FALSE(ok("video", "/a.mp4"));   // raíz sin unidad: la de la base
    for (const char* device : {"NUL", "nul", "Con", "AUX.", "prn ", "COM1", "lpt9", "COM²"}) {
        INFO(device);
        CHECK_FALSE(ok("video", device));
    }
    std::error_code ec;
    fs::remove_all(tmp, ec);
}
