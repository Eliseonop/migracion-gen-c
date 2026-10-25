// Paridad del MATTE DE «ELIMINAR FONDO» leído de la caché con video-yt: build_clip_bg_mask y
// ensure_derived de bg/service.py (con el nivel 2 vacío, la primera vez) y matte_lut de clip_bg.py.
//
// Fixture (tools/fixtures/export_fixtures.py --solo matte; casos en bg_cases.py):
//   tests/fixtures/media/bg_matte.json   las cachés sintéticas (qué fotogramas crudos hay y su
//                                         meta.json), lo que da cada clip y de qué crudo sale cada
//                                         fotograma derivado; y la LUT de umbral, suavidad e invertir
//
// El test rehace las cachés con archivos vacíos: a build_clip_bg_mask solo le importa qué fotogramas
// existen y lo que dice el meta.json.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <optional>
#include <string>

#include "gc/core/bg.hpp"
#include "gc/core/bg_matte.hpp"
#include "gc/core/io.hpp"
#include "gc/core/serialize.hpp"

using gc::Json;
using namespace gc::core;
namespace fs = std::filesystem;

namespace {

const fs::path kFixtures = path_from_utf8(GC_FIXTURES_DIR);

Json load_fixture() {
    auto fx = read_json_file(kFixtures / "media" / "bg_matte.json");
    REQUIRE(fx); // faltan: export_fixtures.py --solo matte
    return std::move(*fx);
}

} // namespace

TEST_CASE("paridad del matte de eliminar fondo: lo que el export pide a la caché", "[parity][bg]") {
    const Json fx = load_fixture();
    const fs::path root = fs::temp_directory_path() / "gc_bg_matte";
    fs::remove_all(root);
    for (const auto& [key, m] : fx.at("mattes").items()) {
        const fs::path dir = root / "matte" / path_from_utf8(key);
        fs::create_directories(dir);
        for (const Json& idx : m.at("files")) {
            std::ofstream(dir / matte_frame_name(idx.get<std::int64_t>())).put('\0');
        }
        if (m.at("meta").is_string()) {
            std::ofstream(dir / "meta.json", std::ios::binary) << m.at("meta").get<std::string>();
        }
    }
    std::size_t specs = 0;
    for (const Json& c : fx.at("clips")) {
        INFO(c.at("name").get<std::string>());
        auto clip = clip_from_json(c.at("clip"));
        REQUIRE(clip);
        const auto got = clip_matte_spec(*clip, root);
        if (c.contains("error")) { // Python lanza (y el export sigue sin el matte)
            CHECK_FALSE(got);
            continue;
        }
        REQUIRE(got);
        const Json& want = c.at("spec");
        if (want.is_null()) {
            CHECK_FALSE(got->has_value());
            continue;
        }
        REQUIRE(got->has_value());
        const MatteSpec& s = **got;
        CHECK(s.folder == root / "matte" / path_from_utf8(clip->bg_removal->at("auto").at("base_key").get<std::string>()));
        CHECK(s.start_number == want.at("start_number").get<std::int64_t>());
        CHECK(s.frames == want.at("frames").get<std::int64_t>());
        CHECK(s.mask_fps == want.at("mask_fps").get<std::int64_t>());
        CHECK(s.loop == want.at("loop").get<bool>());
        CHECK(s.duration == want.at("duration").get<double>());
        CHECK(s.width == want.at("width").get<std::int64_t>());
        CHECK(s.height == want.at("height").get<std::int64_t>());
        CHECK(s.lut_only == c.at("lut_only").get<bool>());
        if (s.lut_only) {
            Json sources = Json::array();
            for (const auto& src : s.sources) {
                sources.push_back(src ? Json(*src) : Json());
            }
            CHECK(sources == c.at("sources"));
        }
        ++specs;
    }
    fs::remove_all(root);
    CHECK(specs >= 15);
}

TEST_CASE("paridad del matte de eliminar fondo: la LUT de umbral, suavidad e invertir", "[parity][bg]") {
    const Json fx = load_fixture();
    for (const Json& c : fx.at("lut")) {
        INFO(c.at("auto").dump());
        const Json bg = normalize_bg(Json{{"auto", c.at("auto")}});
        const auto lut = matte_lut(bg.at("auto"));
        Json got = Json::array();
        for (const std::uint8_t v : lut) {
            got.push_back(v);
        }
        CHECK(got == c.at("lut"));
    }
    CHECK(fx.at("lut").size() >= 100);
}

TEST_CASE("matte_frame_index redondea como el filtro fps y nunca da negativo", "[bg]") {
    CHECK(matte_frame_index(0.0, 15) == 0);
    CHECK(matte_frame_index(1.0 / 30.0, 15) == 1); // 0,5 → 1
    CHECK(matte_frame_index(2.0, 15) == 30);
    CHECK(matte_frame_index(-3.0, 15) == 0);
    CHECK(matte_frame_index(std::numeric_limits<double>::quiet_NaN(), 15) == 0);
    CHECK(matte_frame_index(1.0, 0) == 15);  // int(mask_fps or 15)
    CHECK(matte_frame_index(1.0, -4) == 1);  // max(MASK_FPS_MIN, …)
    CHECK(matte_frame_name(0) == "000001.png");
    CHECK(matte_frame_name(68) == "000069.png");
}
