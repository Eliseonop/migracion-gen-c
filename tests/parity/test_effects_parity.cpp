// Paridad de los efectos de clip (clip_effects.hpp) con effects_ffmpeg de clip_fx.py: de cada
// `effects`, el C++ tiene que leer lo mismo que FFmpeg lee de la cadena que escribe el export
// (los números redondeados al escribirlos, con los límites de cada filtro).
//
// Fixture (tools/fixtures/export_fixtures.py → synthetic_effect_chains):
//   tests/fixtures/render/effects.json   media_cases.EFFECT_CHAINS: los `effects` y la cadena de
//                                        FFmpeg para un cuadro de 360×640.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "gc/core/clip_effects.hpp"
#include "gc/core/io.hpp"

using gc::Json;
using namespace gc::core;

namespace {

const std::filesystem::path kFixtures = path_from_utf8(GC_FIXTURES_DIR);

std::vector<std::string> split(std::string_view s, char sep) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start <= s.size()) {
        const std::size_t end = std::min(s.find(sep, start), s.size());
        out.emplace_back(s.substr(start, end - start));
        start = end + 1;
    }
    return out;
}

// El valor de «clave=valor» en las opciones de un filtro («a=1:b=2»).
double option(const std::string& opts, std::string_view key) {
    for (const auto& kv : split(opts, ':')) {
        if (kv.starts_with(std::string(key) + "=")) {
            return std::stod(kv.substr(key.size() + 1));
        }
    }
    FAIL("falta la opción " << key << " en " << opts);
    return 0.0;
}

// Lo que FFmpeg lee de la cadena (con los límites de vf_eq.c para brillo, contraste y saturación).
ClipEffects read_chain(const std::string& chain) {
    ClipEffects fx;
    if (chain.empty()) {
        return fx;
    }
    const auto filters = split(chain, ',');
    for (std::size_t i = 0; i < filters.size(); ++i) {
        const std::string& f = filters[i];
        const std::size_t eq = f.find('=');
        const std::string name = f.substr(0, eq);
        const std::string opts = eq == std::string::npos ? "" : f.substr(eq + 1);
        if (name == "gblur") {
            fx.blur = option(opts, "sigma");
        } else if (name == "unsharp") {
            fx.sharpen = std::stod(split(opts, ':').at(2));
        } else if (f == "hue=s=0") {
            fx.grayscale = true;
        } else if (f == "colorchannelmixer=.393:.769:.189:0:.349:.686:.168:0:.272:.534:.131") {
            fx.sepia = true;
        } else if (f == "eq=contrast=1.35:saturation=0.65") {
            REQUIRE(i + 1 < filters.size());
            CHECK(filters[i + 1] == "hue=h=-8");
            fx.vhs = true;
            ++i;
        } else if (name == "noise") {
            fx.grain = static_cast<int>(option(opts, "alls"));
            CHECK(opts.ends_with(":allf=t"));
        } else if (name == "scale") {
            // scale=iw/N:ih/N:flags=neighbor y scale=W:H:flags=neighbor
            const auto parts = split(opts, ':');
            REQUIRE(parts.at(0).starts_with("iw/"));
            fx.pixelate = std::stoi(parts.at(0).substr(3));
            REQUIRE(i + 1 < filters.size());
            CHECK(filters[i + 1] == "scale=360:640:flags=neighbor");
            ++i;
        } else if (name == "eq") {
            EqParams p;
            for (const auto& kv : split(opts, ':')) {
                const auto k = kv.substr(0, kv.find('='));
                const double v = std::stod(kv.substr(kv.find('=') + 1));
                if (k == "brightness") {
                    p.brightness = std::clamp(v, -1.0, 1.0);
                } else if (k == "contrast") {
                    p.contrast = std::clamp(v, -1000.0, 1000.0);
                } else if (k == "saturation") {
                    p.saturation = std::clamp(v, 0.0, 3.0);
                } else {
                    FAIL("opción de eq desconocida: " << kv);
                }
            }
            fx.eq = p;
        } else if (name == "colorchannelmixer") {
            constexpr std::array<std::array<std::string_view, 3>, 3> kNames{
                {{"rr", "rg", "rb"}, {"gr", "gg", "gb"}, {"br", "bg", "bb"}}};
            ColorMatrix m{};
            for (std::size_t r = 0; r < 3; ++r) {
                for (std::size_t c = 0; c < 3; ++c) {
                    m[r][c] = option(opts, kNames[r][c]);
                }
            }
            fx.adjust = m;
        } else {
            FAIL("filtro desconocido en la cadena: " << f);
        }
    }
    return fx;
}

} // namespace

TEST_CASE("efectos de clip: lo mismo que lee FFmpeg de la cadena del export", "[parity][effects]") {
    const auto path = kFixtures / "render" / "effects.json";
    REQUIRE(std::filesystem::exists(path)); // faltan: ejecuta tools/fixtures/export_fixtures.py --solo render
    auto file = read_json_file(path);
    REQUIRE(file);
    std::size_t cases = 0;
    std::size_t active = 0;
    for (const Json& c : *file) {
        const auto chain = c.at("chain").get<std::string>();
        INFO(c.at("effects").dump() << " → «" << chain << "»");
        const ClipEffects want = read_chain(chain);
        const ClipEffects got = clip_effects(c.at("effects"));
        CHECK(got.blur == want.blur);
        CHECK(got.sharpen == want.sharpen);
        CHECK(got.grayscale == want.grayscale);
        CHECK(got.sepia == want.sepia);
        CHECK(got.vhs == want.vhs);
        CHECK(got.grain == want.grain);
        CHECK(got.pixelate == want.pixelate);
        REQUIRE(got.eq.has_value() == want.eq.has_value());
        if (want.eq) {
            CHECK(got.eq->brightness == want.eq->brightness);
            CHECK(got.eq->contrast == want.eq->contrast);
            CHECK(got.eq->saturation == want.eq->saturation);
        }
        REQUIRE(got.adjust.has_value() == want.adjust.has_value());
        if (want.adjust) {
            CHECK(*got.adjust == *want.adjust);
        }
        CHECK(got.any() == want.any());
        active += got.any() ? 1 : 0;
        ++cases;
    }
    CHECK(cases >= 50);
    CHECK(active >= 35);
}
