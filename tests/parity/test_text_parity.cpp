// Paridad del ESTILO DE TEXTO con video-yt: los cálculos puros de text_ass.py y las métricas
// de fuente de font_metrics.py.
//
// Fixtures (tools/fixtures/export_fixtures.py, casos en text_cases.py):
//   tests/fixtures/text/synthetic.json   estilos raros a propósito, May./min., palabra activa,
//                                        TTF mínimos (rotos o no) y carpetas de fuentes falsas
//   tests/fixtures/local/text/real.json  tus estilos de texto y las fuentes de C:/Windows/Fonts

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

#include "gc/core/font_metrics.hpp"
#include "gc/core/io.hpp"
#include "gc/core/text_metrics.hpp"
#include "gc/core/text_style.hpp"

using gc::Json;
using namespace gc::core;
namespace fs = std::filesystem;

namespace {

const fs::path kFixtures = path_from_utf8(GC_FIXTURES_DIR);

// Un real de la fixture: número, o "nan" / "inf" / "-inf" (JSON no los admite).
double num(const Json& j) {
    if (j.is_string()) {
        const auto& s = j.get_ref<const std::string&>();
        if (s == "nan") {
            return std::numeric_limits<double>::quiet_NaN();
        }
        return s == "-inf" ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::infinity();
    }
    return j.get<double>();
}

bool same(double a, const Json& expected) {
    const double b = num(expected);
    return (std::isnan(a) && std::isnan(b)) || a == b;
}

std::vector<unsigned char> unhex(const std::string& hex) {
    std::vector<unsigned char> out;
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
        out.push_back(static_cast<unsigned char>(std::stoi(hex.substr(i, 2), nullptr, 16)));
    }
    return out;
}

void write_bytes(const fs::path& p, const std::string& hex) {
    fs::create_directories(p.parent_path());
    const auto bytes = unhex(hex);
    std::ofstream(p, std::ios::binary).write(reinterpret_cast<const char*>(bytes.data()),
                                             static_cast<std::streamsize>(bytes.size()));
}

const Json* ok(const Json& c, const char* key) {
    const Json& o = c.at(key);
    return o.contains("ok") ? &o.at("ok") : nullptr;
}

// Compara los cálculos de un estilo; devuelve cuántas salidas daban error en Python.
int check_style(const Json& c) {
    const Json& st = c.at("style");
    INFO(st.dump());
    int errors = 0;
    if (const Json* e = ok(c, "fx")) {
        const WordFx fx = word_fx_set(st);
        Json names = Json::array();
        for (const auto& [on, name] : {std::pair{fx.glow, "glow"}, {fx.highlight, "highlight"}, {fx.pop, "pop"}}) {
            if (on) {
                names.push_back(name);
            }
        }
        CHECK(names == *e);
    }
    if (const Json* e = ok(c, "opacity")) {
        CHECK(same(style_opacity(st), *e));
    }
    if (const Json* e = ok(c, "word_opacity")) {
        CHECK(same(word_opacity(st, true), e->at(0)));
        CHECK(same(word_opacity(st, false), e->at(1)));
    }
    if (const Json* e = ok(c, "alpha")) {
        CHECK(alpha_hex(word_opacity(st, true)) == e->at(0).get<std::string>());
        CHECK(alpha_hex(word_opacity(st, false)) == e->at(1).get<std::string>());
    }
    if (const Json* e = ok(c, "stretch")) {
        const auto [sx, sy] = stretch_of(st);
        CHECK(same(sx, e->at(0)));
        CHECK(same(sy, e->at(1)));
    }
    if (const Json* e = ok(c, "box_color")) {
        const auto box = box_color(st);
        CHECK((box ? Json(*box) : Json()) == *e);
    }
    if (const Json* e = ok(c, "box_drawn")) {
        CHECK(box_drawn(st) == e->get<bool>());
    }
    if (const Json* e = ok(c, "xy")) {
        const auto big = text_xy(st, 1080, 1920);
        const auto small = text_xy(st, 360, 640);
        CHECK(Json::array({big.first, big.second}) == e->at(0));
        CHECK(Json::array({small.first, small.second}) == e->at(1));
    }
    const double font_px = text_font_px(st, 1920);
    if (const Json* e = ok(c, "font_px")) {
        CHECK(same(font_px, *e));
    }
    if (const Json* e = ok(c, "letter_spacing")) {
        CHECK(same(text_letter_spacing_px(st, font_px), *e));
    }
    if (const Json* e = ok(c, "line_height")) {
        CHECK(same(text_line_height(st), *e));
    }
    if (const Json* e = ok(c, "shadow")) {
        const auto sh = text_shadow(st, font_px);
        REQUIRE(sh.has_value() == !e->is_null());
        if (sh) {
            CHECK(sh->color == e->at("color").get<std::string>());
            CHECK(same(sh->opacity, e->at("opacity")));
            // cos / sin de la biblioteca de C: pueden diferir en el último bit
            CHECK(sh->dx == Catch::Approx(num(e->at("dx"))).margin(1e-12));
            CHECK(sh->dy == Catch::Approx(num(e->at("dy"))).margin(1e-12));
            CHECK(same(sh->sigma, e->at("sigma")));
            CHECK(same(sh->spread, e->contains("spread") ? e->at("spread") : Json(0.0)));
        }
    } else {
        ++errors;
        CHECK_FALSE(text_shadow(st, font_px)); // ángulo infinito: video-yt falla, aquí no hay sombra
    }
    if (const Json* e = ok(c, "curve")) {
        CHECK(same(curve_of(st), *e));
    }
    for (const char* key : {"fx", "opacity", "word_opacity", "alpha", "stretch", "box_color", "box_drawn", "xy",
                            "font_px", "letter_spacing", "line_height", "curve"}) {
        errors += ok(c, key) ? 0 : 1;
    }
    return errors;
}

void check_case(const Json& c) {
    INFO(c.at("text").dump() << " " << c.at("mode").dump());
    CHECK(apply_text_case(c.at("text").get<std::string>(), c.at("mode")) == c.at("out").get<std::string>());
}

Json load(const fs::path& path) {
    auto file = read_json_file(path);
    REQUIRE(file);
    return std::move(*file);
}

} // namespace

TEST_CASE("paridad del texto: estilo, opacidad, colores, sombra, curva y posición", "[parity][text]") {
    const auto path = kFixtures / "text" / "synthetic.json";
    REQUIRE(fs::exists(path)); // faltan: ejecuta tools/fixtures/export_fixtures.py
    const Json fx = load(path);
    int errors = 0;
    for (const Json& c : fx.at("styles")) {
        errors += check_style(c);
    }
    CHECK(fx.at("styles").size() >= 150);
    CHECK(errors == 1); // solo el ángulo de sombra infinito

    for (const Json& c : fx.at("effective")) {
        INFO(c.dump());
        CHECK(effective_text_style(c.at("track"), c.at("clip")) == c.at("out"));
    }
    for (const Json& c : fx.at("alpha")) {
        INFO(c.at("op").dump());
        CHECK(alpha_hex(num(c.at("op"))) == c.at("out").get<std::string>());
    }
    for (const Json& c : fx.at("case")) {
        check_case(c);
    }
    for (const Json& c : fx.at("active_word")) {
        INFO(c.dump());
        REQUIRE(c.at("out").contains("ok"));
        CHECK(active_word_index(c.at("n").get<std::int64_t>(), c.at("t").get<double>(), c.at("d").get<double>()) ==
              c.at("out").at("ok").get<std::int64_t>());
    }
}

TEST_CASE("paridad del texto: alto de línea de un TTF y carpetas de fuentes", "[parity][text][fonts]") {
    const Json fx = load(kFixtures / "text" / "synthetic.json");
    const fs::path tmp = fs::temp_directory_path() / "gc_text_fonts";
    fs::remove_all(tmp);
    int i = 0;
    for (const Json& f : fx.at("font_files")) {
        INFO(f.at("name").get<std::string>());
        const fs::path p = tmp / ("f" + std::to_string(i++) + ".ttf");
        if (!f.at("hex").is_null()) {
            write_bytes(p, f.at("hex").get<std::string>());
        }
        CHECK(font_line_height_em(p) == f.at("line_height_em").get<double>());
    }
    i = 0;
    for (const Json& layout : fx.at("font_layouts")) {
        const fs::path root = tmp / ("l" + std::to_string(i++));
        for (const auto& [rel, hex] : layout.at("files").items()) {
            write_bytes(root / path_from_utf8(rel), hex.get<std::string>());
        }
        fs::create_directories(root / "system");
        fs::create_directories(root / "bundled");
        const FontDirs dirs{root / "system", root / "bundled"};
        for (const Json& f : layout.at("fonts")) {
            const auto name = f.at("name").get<std::string>();
            const bool bold = f.at("bold").get<bool>();
            INFO(root.filename().string() << " " << name << (bold ? " negrita" : ""));
            CHECK(fs::relative(resolve_font_path(name, bold, dirs), root).generic_string() ==
                  f.at("path").get<std::string>());
            CHECK(ass_size_factor(name, bold, dirs) == f.at("factor").get<double>());
        }
    }
    // Anton va con la app.
    CHECK(font_line_height_em(path_from_utf8(GC_RESOURCES_DIR) / "fonts" / "Anton-Regular.ttf") ==
          fx.at("anton").get<double>());
    fs::remove_all(tmp);
}

TEST_CASE("paridad del texto: tus estilos y tus fuentes", "[parity][text][local]") {
    const auto path = kFixtures / "local" / "text" / "real.json";
    if (!fs::exists(path)) {
        SKIP("Sin fixtures locales: export_fixtures.py --real las genera desde data/projects.json");
    }
    const Json fx = load(path);
    for (const Json& c : fx.at("styles")) {
        CHECK(check_style(c) == 0);
    }
    for (const Json& c : fx.at("case")) {
        check_case(c);
    }
    const fs::path system = path_from_utf8(fx.at("system_dir").get<std::string>());
    if (!fs::exists(system)) {
        SKIP("Sin la carpeta de fuentes del sistema de la fixture");
    }
    const FontDirs dirs{system, path_from_utf8(GC_RESOURCES_DIR) / "fonts"};
    for (const Json& f : fx.at("fonts")) {
        const auto name = f.at("name").get<std::string>();
        INFO(name << (f.at("bold").get<bool>() ? " negrita" : ""));
        CHECK(ass_size_factor(name, f.at("bold").get<bool>(), dirs) == f.at("factor").get<double>());
    }
}
