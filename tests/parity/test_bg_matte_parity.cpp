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

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "gc/core/bg.hpp"
#include "gc/core/bg_matte.hpp"
#include "gc/core/io.hpp"
#include "gc/core/serialize.hpp"

using gc::Json;
using namespace gc::core;
namespace fs = std::filesystem;

namespace {

const fs::path kFixtures = path_from_utf8(GC_FIXTURES_DIR);

// Base64 estándar (la fixture guarda los fotogramas en gris así).
std::vector<std::uint8_t> from_base64(std::string_view text) {
    std::vector<std::uint8_t> out;
    std::uint32_t acc = 0;
    int bits = 0;
    for (const char c : text) {
        int v = -1;
        if (c >= 'A' && c <= 'Z') {
            v = c - 'A';
        } else if (c >= 'a' && c <= 'z') {
            v = c - 'a' + 26;
        } else if (c >= '0' && c <= '9') {
            v = c - '0' + 52;
        } else if (c == '+') {
            v = 62;
        } else if (c == '/') {
            v = 63;
        }
        if (v < 0) {
            continue; // el relleno «=»
        }
        acc = (acc << 6U) | static_cast<std::uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<std::uint8_t>((acc >> static_cast<unsigned>(bits)) & 0xFFU));
        }
    }
    return out;
}

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

// derive_matte más allá de la LUT, frente al de clip_bg.py (cv2 y numpy en float32): el GaussianBlur,
// los resize y el pincel (cv2.polylines y cv2.circle con LINE_AA, cv_draw.cpp) son los de OpenCV.
// Bit a bit salvo, a veces, 1 nivel en pocos píxeles (la suma en float32 en otro orden: la
// contracción, 44 de 5184).
TEST_CASE("paridad del matte de eliminar fondo: pluma, expansión, opacidad y pincel", "[parity][bg]") {
    const Json fx = load_fixture();
    std::size_t cases = 0;
    for (const Json& c : fx.at("derive")) {
        const auto name = c.at("name").get<std::string>();
        INFO(name);
        const int w = c.at("width").get<int>();
        const int h = c.at("height").get<int>();
        const auto raw = from_base64(c.at("raw").get<std::string>());
        const auto want = from_base64(c.at("derived").get<std::string>());
        REQUIRE(raw.size() == static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
        REQUIRE(want.size() == raw.size());
        Json a = c.at("auto");
        a["enabled"] = true;
        a["base_key"] = "k";
        a["status"] = "ready";
        const Json bg = normalize_bg(Json{{"auto", a}});
        const auto got = derive_matte(raw, w, h, bg.at("auto"));
        REQUIRE(got.size() == want.size());
        int worst = 0;
        std::size_t off = 0;
        double se = 0.0;
        for (std::size_t i = 0; i < got.size(); ++i) {
            const int d = std::abs(int{got[i]} - int{want[i]});
            worst = std::max(worst, d);
            off += d > 0 ? 1 : 0;
            se += static_cast<double>(d) * d;
        }
        const double psnr = se == 0.0 ? 99.0 : 10.0 * std::log10(255.0 * 255.0 * static_cast<double>(got.size()) / se);
        UNSCOPED_INFO(name << ": " << off << " de " << got.size() << " píxeles distintos, como mucho " << worst
                           << "; " << psnr << " dB");
        CHECK(worst <= 1);
        CHECK(static_cast<double>(off) <= 0.01 * static_cast<double>(got.size()));
        ++cases;
    }
    CHECK(cases >= 20);
}

// derive_matte sobre tus mattes reales de la caché de video-yt (export_fixtures.py --solo matte
// --real): un fotograma de cada proveedor y tamaño (RVM 1280×720, SAM 910×512, U²-Net…) con pluma,
// expansión, opacidad y pincel. Mismo criterio que los sintéticos, y el tiempo de cada derivado.
TEST_CASE("paridad del matte de eliminar fondo: derive_matte con tus mattes", "[parity][bg][real]") {
    const fs::path dir = kFixtures / "local" / "bg";
    if (!fs::exists(dir / "derive.json")) {
        SKIP("sin tests/fixtures/local/bg/derive.json (export_fixtures.py --solo matte --real)");
    }
    auto fx = read_json_file(dir / "derive.json");
    REQUIRE(fx);
    const auto read_gray = [&](const std::string& name) {
        std::ifstream in(dir / "derive" / path_from_utf8(name), std::ios::binary);
        return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    };
    std::size_t cases = 0;
    for (const Json& c : fx->at("cases")) {
        const auto name = c.at("name").get<std::string>();
        INFO(name);
        const int w = c.at("width").get<int>();
        const int h = c.at("height").get<int>();
        const auto raw = read_gray(c.at("raw").get<std::string>());
        const auto want = read_gray(c.at("derived").get<std::string>());
        REQUIRE(raw.size() == static_cast<std::size_t>(w) * static_cast<std::size_t>(h));
        REQUIRE(want.size() == raw.size());
        const Json bg = normalize_bg(Json{{"auto", c.at("auto")}});
        const auto t0 = std::chrono::steady_clock::now();
        const auto got = derive_matte(raw, w, h, bg.at("auto"));
        const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        REQUIRE(got.size() == want.size());
        int worst = 0;
        std::size_t off = 0;
        for (std::size_t i = 0; i < got.size(); ++i) {
            const int d = std::abs(int{got[i]} - int{want[i]});
            worst = std::max(worst, d);
            off += d > 0 ? 1 : 0;
        }
        UNSCOPED_INFO(name << ": " << off << " de " << got.size() << " píxeles distintos, como mucho " << worst
                           << "; " << ms << " ms");
        CHECK(worst <= 1);
        CHECK(static_cast<double>(off) <= 0.01 * static_cast<double>(got.size()));
        ++cases;
    }
    CHECK(cases >= 20);
}
