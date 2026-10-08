// Paridad de la BIBLIOTECA DE SFX EN DISCO con video-yt (la parte de lectura de sfx.py).
//
// Fixtures (tools/fixtures/export_fixtures.py):
//   tests/fixtures/sfx/synthetic.json   sfx_cases.py (en el repo)
//   tests/fixtures/local/sfx/real.json  tu carpeta de SFX con tus favoritos (solo local)
//
// Se repiten los pasos sobre una carpeta temporal (o la tuya) con un settings.json temporal
// y se compara lo que devuelve cada uno (o si falla). «{TMP}» es esa carpeta.

#include <catch2/catch_test_macros.hpp>

#include <charconv>
#include <filesystem>
#include <fstream>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "gc/core/io.hpp"
#include "gc/core/settings.hpp"
#include "gc/core/sfx_disk.hpp"
#include "json_diff.hpp"
#include "py.hpp"

namespace fs = std::filesystem;
using gc::Json;
using namespace gc::core;

namespace {

const fs::path kFixtures = path_from_utf8(GC_FIXTURES_DIR);

std::string replace_all(std::string s, std::string_view from, std::string_view to) {
    for (auto pos = s.find(from); pos != std::string::npos; pos = s.find(from, pos + to.size())) {
        s.replace(pos, from.size(), to);
    }
    return s;
}

// Los bytes latin-1 de un texto UTF-8 con caracteres < 256 (para escribir UTF-8 inválido).
std::string latin1(std::string_view utf8) {
    std::string out;
    for (std::size_t i = 0; i < utf8.size(); ++i) {
        const auto b = static_cast<unsigned>(static_cast<unsigned char>(utf8[i]));
        if (b >= 0xC0 && i + 1 < utf8.size()) {
            out += static_cast<char>(((b & 0x1FU) << 6U) | (static_cast<unsigned>(static_cast<unsigned char>(utf8[++i])) & 0x3FU));
        } else {
            out += static_cast<char>(b);
        }
    }
    return out;
}

std::string text_of(const Json& content) { return content.is_string() ? content.get<std::string>() : content.dump(); }

void write_file(const fs::path& file, const std::string& bytes) {
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary | std::ios::trunc) << bytes;
}

// La sonda falsa del exportador: el número que contiene el archivo (si no, sin duración).
std::optional<double> fake_probe(const fs::path& path) {
    const std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::stringstream text;
    text << in.rdbuf();
    const std::string s = text.str();
    const auto b = s.find_first_not_of(" \t\r\n\f\v");
    if (b == std::string::npos) {
        return std::nullopt;
    }
    const auto e = s.find_last_not_of(" \t\r\n\f\v") + 1;
    double v = 0.0;
    const auto [ptr, ec] = std::from_chars(s.data() + b, s.data() + e, v);
    return ec == std::errc() && ptr == s.data() + e ? std::optional<double>(v) : std::nullopt;
}

std::set<std::string> string_set(const Json& list) {
    std::set<std::string> out;
    for (const Json& x : list) {
        out.insert(x.get<std::string>());
    }
    return out;
}

void check_result(const Json& s, bool ok, const Json& result) {
    if (s.contains("error")) {
        INFO("Python falla con " << s.at("error").get<std::string>());
        CHECK_FALSE(ok);
        return;
    }
    CHECK(ok);
    const auto diff = gc::test::json_diff(s.at("result"), result);
    INFO("resultado: " << diff.value_or(""));
    CHECK_FALSE(diff);
}

std::size_t run_steps(const Json& fixture, const std::string& fname) {
    std::random_device rd;
    const fs::path tmp = fs::temp_directory_path() / ("gc_sfx_" + std::to_string(rd()));
    fs::create_directories(tmp);
    const fs::path root = fixture.at("root").is_null() ? tmp : path_from_utf8(fixture.at("root").get<std::string>());
    const std::string native = detail::py_path_str(path_to_utf8(root)); // str(Path) de Python
    const std::string posix = replace_all(path_to_utf8(root), "\\", "/");
    const fs::path settings_path = tmp / "__settings__.json";
    const SettingsFile settings(settings_path);
    fs::path default_dir = tmp / "__sin_carpeta__";
    const auto with_base = [&](Json r) {
        if (r.at("base").is_string()) {
            r["base"] = replace_all(r["base"].get<std::string>(), native, "{TMP}");
        }
        return r;
    };

    std::size_t n = 0;
    for (const Json& s : fixture.at("steps")) {
        const Json& step = s.at("step");
        INFO(fname << " :: paso " << n << ": " << step.dump().substr(0, 200));
        const auto kind = step[0].get<std::string>();
        const Json st = settings.load();
        const auto base = sfx_base(st, default_dir);
        bool ok = true;
        Json result;
        if (kind == "files") {
            for (const Json& rel : step[1]) {
                write_file(root / path_from_utf8(rel.get<std::string>()), "x");
            }
        } else if (kind == "file" || kind == "bytes") {
            const std::string text = text_of(step[2]);
            write_file(root / path_from_utf8(step[1].get<std::string>()), kind == "bytes" ? latin1(text) : text);
        } else if (kind == "dirs") {
            for (const Json& rel : step[1]) {
                fs::create_directories(root / path_from_utf8(rel.get<std::string>()));
            }
        } else if (kind == "remove") {
            fs::remove(root / path_from_utf8(step[1].get<std::string>()));
        } else if (kind == "default") {
            const auto rel = step[1].get<std::string>();
            default_dir = rel.empty() ? root : root / path_from_utf8(rel);
        } else if (kind == "settings") {
            write_file(settings_path, replace_all(text_of(step[1]), "{TMP}", posix));
        } else if (kind == "index") {
            result = with_base(sfx_load_index(base));
        } else if (kind == "scan") {
            ok = base.has_value(); // _scan(None) falla
            result = ok ? Json(sfx_scan(*base)) : Json();
        } else if (kind == "search") {
            result = with_base(sfx_search(base, st, step[1].get<std::string>(), step[2].get<std::string>(),
                                          step[3].get<std::size_t>()));
        } else if (kind == "resolve") {
            const auto target = sfx_resolve(base, replace_all(step[1].get<std::string>(), "{TMP}", posix));
            if (target) {
                // La ruta relativa a la carpeta, con «/» y sin distinguir mayúsculas (Python
                // da las de disco; std::filesystem, las pedidas).
                const auto parts = [](const fs::path& p) {
                    std::vector<std::string> out;
                    for (const fs::path& part : p) {
                        if (!part.empty()) {
                            out.push_back(path_to_utf8(part));
                        }
                    }
                    return out;
                };
                const auto t = parts(*target);
                std::string rel;
                for (std::size_t k = parts(fs::weakly_canonical(*base)).size(); k < t.size(); ++k) {
                    rel += (rel.empty() ? "" : "/") + t[k];
                }
                result = detail::py_lower(rel.empty() ? "." : rel);
            }
            if (s.contains("result") && s.at("result").is_string()) {
                const Json expected = detail::py_lower(s.at("result").get<std::string>());
                check_result(Json{{"step", step}, {"result", expected}}, ok, result);
                ++n;
                continue;
            }
        } else if (kind == "library") {
            const SfxLibrary lib = load_sfx_library(base, st, fake_probe);
            result = Json{{"items", lib.items}, {"durations", lib.durations}};
        } else if (kind == "favorite_ids") {
            const auto ids = sfx_favorite_ids(st);
            result = Json(std::vector<std::string>(ids.begin(), ids.end()));
        } else if (kind == "entry") {
            const auto e = sfx_entry(step[1], step[2], step[3], step[4], step[5]);
            ok = e.has_value();
            result = ok ? *e : Json();
        } else if (kind == "filter") {
            result = filter_sfx_items(step[1].get<std::vector<Json>>(), step[2].get<std::string>(),
                                      step[3].get<std::string>(), string_set(step[4]));
        } else if (kind == "favcat") {
            result = with_favorites_category(step[1].get<std::vector<Json>>(), step[2].get<std::vector<Json>>(),
                                             string_set(step[3]));
        } else if (kind == "audio") {
            result = is_sfx_audio(step[1].get<std::string>());
        } else {
            FAIL("paso desconocido: " << kind);
        }
        check_result(s, ok, result);
        ++n;
    }
    std::error_code ec;
    fs::remove_all(tmp, ec);
    return n;
}

} // namespace

TEST_CASE("paridad de la biblioteca de SFX en disco: pasos sintéticos", "[parity][sfx]") {
    const auto path = kFixtures / "sfx" / "synthetic.json";
    REQUIRE(fs::exists(path)); // faltan: ejecuta tools/fixtures/export_fixtures.py
    auto file = read_json_file(path);
    REQUIRE(file);
    CHECK(run_steps(*file, "synthetic.json") > 150);
}

TEST_CASE("paridad de la biblioteca de SFX en disco: la tuya", "[parity][sfx][local]") {
    const auto path = kFixtures / "local" / "sfx" / "real.json";
    if (!fs::exists(path)) {
        SKIP("Sin fixtures locales: export_fixtures.py --real las genera desde tu carpeta de SFX");
    }
    auto file = read_json_file(path);
    REQUIRE(file);
    if (file->at("root").is_null() || !fs::is_directory(path_from_utf8(file->at("root").get<std::string>()))) {
        SKIP("No está tu carpeta de SFX");
    }
    CHECK(run_steps(*file, "real.json") >= 8);
}
