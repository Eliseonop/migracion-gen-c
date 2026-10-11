// Paridad de las CABECERAS DE IMAGEN con video-yt (images.py: probe_gif y _ext_from_magic).
//
// Fixtures (tools/fixtures/export_fixtures.py):
//   tests/fixtures/media/synthetic.json   media_cases.py (en el repo): GIF a mano y de Pillow,
//                                         y todos los truncados de dos de ellos
//   tests/fixtures/local/media/real.json  tus imágenes: la firma de todas y los GIF enteros
//
// Exacta: la duración y los fps son sumas y divisiones de reales redondeadas con round().

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <vector>

#include "gc/core/image_probe.hpp"
#include "gc/core/io.hpp"
#include "json_diff.hpp"

using gc::Json;
using namespace gc::core;

namespace {

const std::filesystem::path kFixtures = path_from_utf8(GC_FIXTURES_DIR);

std::vector<unsigned char> from_hex(const std::string& hex) {
    std::vector<unsigned char> out;
    out.reserve(hex.size() / 2);
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
        out.push_back(static_cast<unsigned char>(std::stoi(hex.substr(i, 2), nullptr, 16)));
    }
    return out;
}

Json gif_json(const GifInfo& g) {
    return Json{{"animated", g.animated}, {"frames", g.frames},
                {"duration", g.duration}, {"fps", g.fps},
                {"width", g.width ? Json(*g.width) : Json(nullptr)},
                {"height", g.height ? Json(*g.height) : Json(nullptr)},
                {"loop", g.loop},         {"has_alpha", g.has_alpha}};
}

std::size_t check_gifs(const Json& file) {
    std::size_t n = 0;
    for (const Json& c : file.at("gifs")) {
        INFO(c.at("name").get<std::string>());
        const auto data = from_hex(c.at("hex").get<std::string>());
        const auto diff = gc::test::json_diff(c.at("gif"), gif_json(probe_gif(data)));
        INFO("probe_gif: " << diff.value_or(""));
        CHECK_FALSE(diff);
        CHECK(image_ext_from_magic(data) == c.at("ext").get<std::string>());
        ++n;
    }
    return n;
}

} // namespace

TEST_CASE("paridad de las cabeceras de imagen: casos sintéticos", "[parity][media]") {
    const auto path = kFixtures / "media" / "synthetic.json";
    REQUIRE(std::filesystem::exists(path)); // faltan: ejecuta tools/fixtures/export_fixtures.py
    auto file = read_json_file(path);
    REQUIRE(file);
    CHECK(check_gifs(*file) > 250);
    for (const Json& m : file->at("magic")) {
        const auto data = from_hex(m.at("hex").get<std::string>());
        INFO("_ext_from_magic(" << m.at("hex").get<std::string>() << ")");
        CHECK(image_ext_from_magic(data) == m.at("ext").get<std::string>());
    }
    CHECK(file->at("magic").size() > 15);
}

TEST_CASE("paridad de las cabeceras de imagen: tus imágenes", "[parity][media][local]") {
    const auto path = kFixtures / "local" / "media" / "real.json";
    if (!std::filesystem::exists(path)) {
        SKIP("Sin fixtures locales: export_fixtures.py --real las genera desde tus carpetas");
    }
    auto file = read_json_file(path);
    REQUIRE(file);
    CHECK(check_gifs(*file) > 0);
}
