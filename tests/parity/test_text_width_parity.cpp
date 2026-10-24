// Paridad de la MEDIDA DEL TEXTO con video-yt: font_metrics.text_width (Pillow 12.3, con la
// maquetación básica de su FreeType 2.14.3) frente a gc::render::TextFonts::text_width (la misma
// FreeType con las mismas llamadas, src/render/src/pillow_font.cpp).
//
// Solo se compila con Skia (gc_render, presets msvc-vcpkg-*).
// Fixtures (tools/fixtures/export_fixtures.py --solo anchos; casos en text_cases.WIDTH_*):
//   tests/fixtures/text/widths.json        cada archivo de fuente del export con una rejilla de
//                                          tamaños, textos y espaciados; los nombres de FONT_NAMES;
//                                          tamaños y espaciados raros; más de 1 000 000 de
//                                          caracteres; y carpetas de fuentes de mentira (lo que
//                                          Pillow no abre lo busca en WINDIR\fonts y, si no, estima)
//   tests/fixtures/local/text/widths.json  tus textos (con --real): el reparto en líneas y las cajas
//                                          de Fondo con la medida real, y cada medida que hacen
//
// Todo exacto: el ancho es un múltiplo de 1/64 de px más el espaciado por carácter. Las fuentes
// van con su tamaño y su CRC-32: si en este equipo no son las mismas, sus casos se saltan y se
// avisa. Donde Python lanza (el round() de un em no finito, fuera del try de text_width), el C++
// da la estimación.

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <map>
#include <string>
#include <thread>
#include <vector>

#include "gc/core/font_metrics.hpp"
#include "gc/core/io.hpp"
#include "gc/core/text_metrics.hpp"
#include "gc/core/text_style.hpp"
#include "gc/render/text.hpp"
#include "json_diff.hpp"
#include "json_reader.hpp"
#include "py.hpp"

using gc::Json;
using namespace gc::core;
namespace fs = std::filesystem;

namespace {

const fs::path kFixtures = path_from_utf8(GC_FIXTURES_DIR);
const fs::path kBundled = path_from_utf8(GC_RESOURCES_DIR) / "fonts";

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

bool same(double a, double b) {
    return (std::isnan(a) && std::isnan(b)) || a == b;
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

std::vector<char> read_bytes(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

void write_bytes(const fs::path& p, const std::vector<char>& data) {
    fs::create_directories(p.parent_path());
    std::ofstream(p, std::ios::binary).write(data.data(), static_cast<std::streamsize>(data.size()));
}

std::vector<char> unhex(const std::string& hex) {
    std::vector<char> out;
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
        out.push_back(static_cast<char>(std::stoi(hex.substr(i, 2), nullptr, 16)));
    }
    return out;
}

// zlib.crc32 de Python.
std::uint32_t crc32(const std::vector<char>& data) {
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t i = 0; i < table.size(); ++i) {
        std::uint32_t c = i;
        for (int k = 0; k < 8; ++k) {
            c = (c & 1U) != 0 ? 0xEDB88320U ^ (c >> 1U) : c >> 1U;
        }
        table[i] = c;
    }
    std::uint32_t crc = 0xFFFFFFFFU;
    for (const char ch : data) {
        crc = table[(crc ^ static_cast<unsigned char>(ch)) & 0xFFU] ^ (crc >> 8U);
    }
    return crc ^ 0xFFFFFFFFU;
}

// Los textos de la fixture: cadenas o {"repeat", "times"} (más de un millón de caracteres).
std::vector<std::string> texts_of(const Json& fx) {
    std::vector<std::string> out;
    for (const Json& t : fx.at("texts")) {
        if (t.is_string()) {
            out.push_back(t.get<std::string>());
            continue;
        }
        const auto piece = t.at("repeat").get<std::string>();
        const auto times = t.at("times").get<std::size_t>();
        std::string s;
        s.reserve(piece.size() * times);
        for (std::size_t i = 0; i < times; ++i) {
            s += piece;
        }
        out.push_back(std::move(s));
    }
    return out;
}

// La estimación de text_width (sin la fuente, o donde Python lanza).
double estimate(double em, const std::string& text, double ls) {
    const auto n = static_cast<double>(detail::py_len(text));
    return n * em * 0.55 + ls * n;
}

// El archivo de la fixture («system/arial.ttf», «bundled/Anton-Regular.ttf») en estas carpetas.
fs::path file_in(const std::string& rel, const FontDirs& dirs) {
    const auto slash = rel.find('/');
    const fs::path& base = rel.substr(0, slash) == "bundled" ? dirs.bundled : dirs.system;
    return base / path_from_utf8(rel.substr(slash + 1));
}

// Los casos de un grupo ({font, bold, file, widths}); devuelve cuántos se comprobaron.
std::size_t check_group(const gc::render::TextFonts& fonts, const FontDirs& dirs, const Json& g,
                        const std::vector<std::string>& texts) {
    const auto font = g.at("font").get<std::string>();
    const bool bold = g.at("bold").get<bool>();
    const auto file = g.at("file").get<std::string>();
    INFO(font << (bold ? " negrita" : "") << " (" << file << ")");
    // La misma fuente que resolve_font_path de font_metrics.py.
    CHECK(resolve_font_path(font.empty() ? "Arial" : font, bold, dirs) == file_in(file, dirs));
    std::size_t n = 0;
    for (const Json& w : g.at("widths")) {
        const double em = num(w.at(0));
        const auto& text = texts.at(w.at(1).get<std::size_t>());
        const double ls = num(w.at(2));
        INFO("em " << em << ", espaciado " << ls << ", «" << (text.size() > 80 ? text.substr(0, 80) + "…" : text)
                   << "»");
        const double got = fonts.text_width(font, bold, em, text, ls);
        if (w.at(3).is_object()) { // Python lanza: aquí, la estimación
            CHECK(same(got, estimate(em, text, ls)));
        } else {
            CHECK(same(got, num(w.at(3))));
        }
        ++n;
    }
    return n;
}

} // namespace

TEST_CASE("paridad de la medida del texto: las fuentes del export", "[parity][render][text]") {
    const auto fx = read_json_file(kFixtures / "text" / "widths.json");
    REQUIRE(fx);
    // La medida es la de Pillow con su misma FreeType, y sin raqm.
    CHECK(gc::render::freetype_version() == fx->at("freetype").get<std::string>());
    CHECK_FALSE(fx->at("raqm").get<bool>());
    const fs::path system = path_from_utf8(fx->at("system_dir").get<std::string>());
    if (!fs::exists(system)) {
        SKIP("Sin la carpeta de fuentes del sistema de la fixture");
    }
    const FontDirs dirs{system, kBundled};
    // Las fuentes de este equipo, ¿son las de la fixture?
    std::map<std::string, bool> same_file;
    for (const auto& [rel, info] : fx->at("files").items()) {
        const auto data = read_bytes(file_in(rel, dirs));
        same_file[rel] =
            data.size() == info.at("size").get<std::size_t>() && crc32(data) == info.at("crc32").get<std::uint32_t>();
        if (!same_file[rel]) {
            WARN(rel << " no es la fuente de la fixture: sus casos se saltan");
        }
    }
    CHECK(same_file["bundled/Anton-Regular.ttf"]); // la de la app es siempre la misma
    const auto texts = texts_of(*fx);
    const gc::render::TextFonts fonts(dirs);
    std::size_t checked = 0;
    std::size_t skipped = 0;
    for (const char* key : {"fonts", "aliases", "odd"}) {
        for (const Json& g : fx->at(key)) {
            const auto file = g.at("file").get<std::string>();
            if (!same_file[file]) {
                skipped += g.at("widths").size();
                continue;
            }
            checked += check_group(fonts, dirs, g, texts);
        }
    }
    UNSCOPED_INFO(checked << " medidas comprobadas, " << skipped << " saltadas");
    CHECK(skipped == 0);
    CHECK(checked >= 10000);
}

TEST_CASE("paridad de la medida del texto: carpetas de fuentes de mentira", "[parity][render][text]") {
    const auto fx = read_json_file(kFixtures / "text" / "widths.json");
    REQUIRE(fx);
#ifndef _WIN32
    SKIP("Lo que Pillow no abre lo busca en WINDIR\\fonts: la fixture es de Windows");
#endif
    // Lo que no abre, ImageFont.truetype lo busca por su nombre en WINDIR\fonts: ¿aquí encuentra lo
    // mismo que al generar la fixture?
    const char* windir = std::getenv("WINDIR"); // NOLINT(concurrency-mt-unsafe): solo se lee
    if (windir == nullptr) {
        SKIP("Sin WINDIR");
    }
    const fs::path windows_fonts = fs::path(windir) / "fonts";
    for (const auto& [name, info] : fx->at("fallback").items()) {
        if (info.is_null()) {
            if (fs::exists(windows_fonts / path_from_utf8(name))) {
                SKIP(name << " está en WINDIR\\fonts y al generar la fixture no estaba");
            }
            continue;
        }
        const auto data = read_bytes(windows_fonts / path_from_utf8(info.at("path").get<std::string>()));
        if (data.size() != info.at("size").get<std::size_t>() || crc32(data) != info.at("crc32").get<std::uint32_t>()) {
            SKIP(name << " de WINDIR\\fonts no es el de la fixture");
        }
    }
    const auto texts = texts_of(*fx);
    const auto anton = read_bytes(kBundled / "Anton-Regular.ttf");
    REQUIRE(anton.size() > 100000);
    const fs::path tmp = fs::temp_directory_path() / "gc_text_widths";
    fs::remove_all(tmp);
    std::size_t i = 0;
    std::size_t checked = 0;
    for (const Json& d : fx->at("dirs")) {
        INFO(d.at("name").get<std::string>());
        const fs::path root = tmp / ("d" + std::to_string(i++));
        for (const char* sub : {"system", "bundled"}) {
            fs::create_directories(root / sub);
            for (const auto& [name, recipe] : d.at(sub).items()) {
                const fs::path p = root / sub / path_from_utf8(name);
                if (!recipe.contains("anton")) {
                    write_bytes(p, unhex(recipe.at("hex").get<std::string>()));
                } else if (recipe.at("anton").is_null()) {
                    write_bytes(p, anton);
                } else {
                    const auto cut = recipe.at("anton").get<std::size_t>();
                    REQUIRE(cut <= anton.size());
                    write_bytes(p, std::vector<char>(anton.begin(), anton.begin() + static_cast<std::ptrdiff_t>(cut)));
                }
            }
        }
        const FontDirs dirs{root / "system", root / "bundled"};
        const gc::render::TextFonts fonts(dirs);
        for (const Json& c : d.at("cases")) {
            checked += check_group(fonts, dirs, c, texts);
        }
    }
    fs::remove_all(tmp);
    CHECK(checked >= 200);
}

TEST_CASE("la medida del texto da lo mismo desde varios hilos", "[render][text]") {
    const auto fx = read_json_file(kFixtures / "text" / "widths.json");
    REQUIRE(fx);
    const fs::path system = path_from_utf8(fx->at("system_dir").get<std::string>());
    if (!fs::exists(system)) {
        SKIP("Sin la carpeta de fuentes del sistema de la fixture");
    }
    const auto texts = texts_of(*fx);
    const FontDirs dirs{system, kBundled};
    // Los de la rejilla, medidos antes uno a uno (la mitad con otra TextFonts, así que esa mitad no
    // está en la caché de `fonts`), y después en cuatro hilos a la vez, cada uno desde un sitio.
    struct Case {
        std::string font;
        bool bold = false;
        double em = 0;
        std::size_t text = 0;
        double ls = 0;
        double want = 0;
    };
    std::vector<Case> cases;
    for (const Json& g : fx->at("fonts")) {
        for (const Json& w : g.at("widths")) {
            cases.push_back({g.at("font").get<std::string>(), g.at("bold").get<bool>(), num(w.at(0)),
                             w.at(1).get<std::size_t>(), num(w.at(2)), 0.0});
        }
    }
    REQUIRE(cases.size() > 1000);
    const gc::render::TextFonts fonts(dirs);
    const gc::render::TextFonts other(dirs);
    for (std::size_t k = 0; k < cases.size(); ++k) {
        auto& c = cases[k];
        c.want = (k % 2 == 0 ? fonts : other).text_width(c.font, c.bold, c.em, texts[c.text], c.ls);
    }
    std::array<bool, 4> ok{};
    std::vector<std::thread> threads;
    threads.reserve(ok.size());
    for (std::size_t t = 0; t < ok.size(); ++t) {
        threads.emplace_back([&, t] {
            bool all = true;
            const auto width = fonts.width_fn();
            for (std::size_t k = 0; k < cases.size(); ++k) {
                const auto& c = cases[(k + (t * cases.size() / ok.size())) % cases.size()];
                all = all && same(width(c.font, c.bold, c.em, texts[c.text], c.ls), c.want);
            }
            ok.at(t) = all;
        });
    }
    for (auto& th : threads) {
        th.join();
    }
    for (const bool b : ok) {
        CHECK(b);
    }
}

TEST_CASE("paridad de la medida del texto: tus textos", "[parity][render][text][local]") {
    const auto path = kFixtures / "local" / "text" / "widths.json";
    if (!fs::exists(path)) {
        SKIP("Sin fixtures locales: export_fixtures.py --solo anchos --real las genera desde data/projects.json");
    }
    const auto fx = read_json_file(path);
    REQUIRE(fx);
    const fs::path system = path_from_utf8(fx->at("system_dir").get<std::string>());
    if (!fs::exists(system)) {
        SKIP("Sin la carpeta de fuentes del sistema de la fixture");
    }
    const FontDirs dirs{system, kBundled};
    const gc::render::TextFonts fonts(dirs);
    const ScopedTextWidth measure(fonts.width_fn()); // como draw_timeline_texts
    std::size_t calls = 0;
    for (const Json& c : fx->at("cases")) {
        const Json& st = c.at("style");
        const double w = c.at("w").get<double>();
        const double h = c.at("h").get<double>();
        const auto text = c.at("text").get<std::string>();
        INFO("«" << text << "» en " << w << "×" << h << ": " << st.dump());
        const auto rows = text_rows(st, w, h, text);
        CHECK(Json(rows) == c.at("rows"));
        // box_rects con el ass_size_factor real de la fuente del estilo (st.get("font") or "Arial").
        const Json font = st.is_object() && st.contains("font") ? st.at("font") : Json();
        const std::string name = detail::py_truthy(font) ? detail::py_str(font) : "Arial";
        const bool bold = !st.is_object() || !st.contains("bold") || detail::py_truthy(st.at("bold"));
        Json got = Json::array();
        for (const BoxRect& r : box_rects(st, w, h, rows, ass_size_factor(name, bold, dirs))) {
            got.push_back({r.x, r.y, r.w, r.h});
        }
        const auto diff = gc::test::json_diff(c.at("box_rects"), nan_safe(got), true, 0.0);
        INFO(diff.value_or(""));
        CHECK_FALSE(diff);
        for (const Json& call : c.at("calls")) {
            const Json& f = call.at(0);
            const std::string font_name = f.is_string() ? f.get<std::string>() : detail::py_str(f);
            const double em = num(call.at(2));
            const auto t = call.at(3).get<std::string>();
            const double ls = num(call.at(4));
            INFO("text_width(" << f.dump() << ", " << call.at(1).dump() << ", " << em << ", «" << t << "», " << ls
                               << ")");
            CHECK(same(text_width(font_name, call.at(1).get<bool>(), em, t, ls), num(call.at(5))));
            ++calls;
        }
    }
    UNSCOPED_INFO(fx->at("cases").size() << " textos, " << calls << " medidas");
    CHECK(fx->at("cases").size() >= 100);
}
