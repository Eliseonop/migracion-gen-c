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
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include "gc/core/font_metrics.hpp"
#include "gc/core/fx_catalog.hpp"
#include "gc/core/io.hpp"
#include "gc/core/serialize.hpp"
#include "gc/core/text_events.hpp"
#include "gc/core/text_metrics.hpp"
#include "gc/core/text_style.hpp"
#include "json_diff.hpp"
#include "json_reader.hpp"
#include "py.hpp"

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

// NaN e infinito como texto, como los escribe la fixture.
Json nan_safe(const Json& j) {
    if (j.is_number_float() && !std::isfinite(j.get<double>())) {
        const double v = j.get<double>();
        return std::isnan(v) ? "nan" : (v > 0 ? "inf" : "-inf");
    }
    if (j.is_object() || j.is_array()) {
        Json out = j;
        for (auto& v : out) {
            v = nan_safe(v);
        }
        return out;
    }
    return j;
}

void check_json(const Json& expected, const Json& actual, double tolerance = 0.0) {
    const auto diff = gc::test::json_diff(expected, nan_safe(actual), true, tolerance);
    INFO(diff.value_or(""));
    CHECK_FALSE(diff);
}

// El ass_size_factor falso de la fixture (_fake_size_factor): no depende de las fuentes.
double fake_size_factor(const Json& st) {
    const Json& font = st.is_object() && st.contains("font") ? st.at("font") : Json();
    const std::string name = font.is_string() && !font.get<std::string>().empty() ? font.get<std::string>() : "Arial";
    const bool bold = !st.is_object() || !st.contains("bold") || gc::core::detail::py_truthy(st.at("bold"));
    return 1.2 + 0.01 * static_cast<double>(gc::core::detail::py_len(name)) + (bold ? 0.05 : 0.0);
}

// Filas, cajas de Fondo y letras de la Curva de un estilo.
void check_geometry(const Json& c, const std::string& rows_text) {
    const Json& st = c.at("style");
    const auto rows = text_rows(st, 1080, 1920, rows_text);
    CHECK(Json(rows) == c.at("rows"));
    if (const Json* e = ok(c, "box_rects")) {
        Json got = Json::array();
        for (const BoxRect& r : box_rects(st, 1080, 1920, rows, fake_size_factor(st))) {
            got.push_back({r.x, r.y, r.w, r.h});
        }
        check_json(*e, got);
    }
    if (const Json* e = ok(c, "curve_glyphs")) {
        Json got = Json::array();
        for (const CurveGlyph& g : curve_glyphs(st, 1080, 1920, rows)) {
            got.push_back({g.word, g.ch, g.x, g.y, g.angle});
        }
        check_json(*e, got, 1e-12); // sin / cos de la biblioteca de C
    }
}

// "h:mm:ss.cc" del .ass en ms, como lo lee libass.
std::int64_t ass_ms(const std::string& s) {
    int h = 0;
    int m = 0;
    int sec = 0;
    int cs = 0;
    REQUIRE(std::sscanf(s.c_str(), "%d:%d:%d.%d", &h, &m, &sec, &cs) == 4);
    return ((static_cast<std::int64_t>(h) * 60 + m) * 60 + sec) * 1000 + cs * 10;
}

// Los eventos del .ass de un texto quieto (caption_dialogues): tiempos, primero, líneas y palabra activa.
void check_events(TimelineClip clip, const Json& st, const Json& expected) {
    if (st.is_object() && st.contains("text_case") && gc::core::detail::py_truthy(st.at("text_case"))) {
        clip.text = apply_text_case(clip.text.value_or(""), st.at("text_case"));
    }
    const Json posed = posed_text_style(clip, st, 0.0);
    const auto events = static_text_events(clip, posed);
    REQUIRE(events.size() == expected.size());
    for (std::size_t i = 0; i < events.size(); ++i) {
        const TextEvent& e = events[i];
        const Json& x = expected[i];
        INFO("evento " << i);
        CHECK(e.start_ms == ass_ms(x.at("start").get<std::string>()));
        CHECK(e.end_ms == ass_ms(x.at("end").get<std::string>()));
        CHECK(e.first == x.at("first").get<bool>());
        std::string rows;
        std::int64_t words = 0;
        for (const auto& row : text_rows(posed, 1080, 1920, e.content.text)) {
            std::string line;
            for (const auto& w : row) {
                line += (line.empty() ? "" : " ") + w;
                ++words;
            }
            rows += line + "\n";
        }
        if (!rows.empty()) {
            rows.pop_back();
        }
        CHECK(rows == x.at("rows").get<std::string>());
        std::string flags;
        for (std::int64_t w = 0; w < words; ++w) {
            flags += e.content.is_active(w) ? 'A' : 'I';
        }
        CHECK(flags == x.at("flags").get<std::string>());
    }
}

// Un clip de texto en sus instantes: pose, estilo con la pose, palabra activa y lo que se ve.
void check_clip(const Json& c) {
    INFO(c.at("name").get<std::string>());
    auto parsed = clip_from_json(c.at("clip"));
    REQUIRE(parsed);
    const TimelineClip& clip = *parsed;
    const Json& st = c.at("style");
    const double dur = text_clip_duration(clip);
    CHECK(same(dur, c.at("dur")));
    CHECK(text_animates(clip, 30) == c.at("animates").at(0).get<bool>());
    CHECK(text_animates(clip, 24) == c.at("animates").at(1).get<bool>());
    Json windows = Json::array();
    for (const auto& [t0, t1] : word_windows(clip)) {
        windows.push_back({t0, t1});
    }
    check_json(c.at("windows"), windows);
    CHECK(text_in_ass(clip) == c.at("in_ass").get<bool>());
    check_events(clip, st, c.at("events"));
    const auto n = static_cast<std::int64_t>(split_words(clip.text.value_or("")).size());
    for (const Json& at : c.at("times")) {
        const double t = num(at.at("t"));
        INFO("t = " << t);
        const TextPose p = text_pose_at(clip, t);
        check_json(at.at("pose"), Json{{"x", p.x}, {"y", p.y}, {"scale", p.scale}, {"rotation", p.rotation},
                                       {"opacity", p.opacity}, {"rot_x", p.rot_x}, {"rot_y", p.rot_y}},
                   1e-12);
        const Json posed = posed_text_style(clip, st, t);
        check_json(at.at("posed"), posed, 1e-12);
        CHECK(active_word_at(clip, n, t, dur) == at.at("active").get<std::int64_t>());
        const auto seen = text_content_at(clip, posed, t, dur);
        REQUIRE(seen.has_value() == !at.at("content").is_null());
        if (seen) {
            CHECK(seen->text == at.at("content").at("text").get<std::string>());
            std::string flags;
            const auto words = static_cast<std::int64_t>(split_words(seen->text).size());
            for (std::int64_t i = 0; i < words; ++i) {
                flags += seen->is_active(i) ? 'A' : 'I';
            }
            CHECK(flags == at.at("content").at("flags").get<std::string>());
        }
    }
}

void load_catalog() {
    auto catalog = FxCatalog::load(path_from_utf8(GC_RESOURCES_DIR) / "fx" / "catalog.json");
    REQUIRE(catalog);
    set_fx_catalog(std::make_shared<const FxCatalog>(std::move(*catalog)));
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
        check_geometry(c, fx.at("rows_text").get<std::string>());
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

TEST_CASE("paridad del texto: pose, palabras y lo que se ve de un clip de texto", "[parity][text]") {
    load_catalog();
    const Json fx = load(kFixtures / "text" / "synthetic.json");
    for (const Json& c : fx.at("clips")) {
        check_clip(c);
    }
    CHECK(fx.at("clips").size() >= 20);
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
    load_catalog();
    const Json fx = load(path);
    for (const Json& c : fx.at("styles")) {
        CHECK(check_style(c) == 0);
        check_geometry(c, fx.at("rows_text").get<std::string>());
    }
    for (const Json& c : fx.at("case")) {
        check_case(c);
    }
    for (const Json& c : fx.at("clips")) {
        check_clip(c);
    }
    CHECK(fx.at("clips").size() >= 600);
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
