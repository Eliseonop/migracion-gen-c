// Paridad de las COLECCIONES DE MATERIAL con video-yt (collections.py).
//
// Fixtures (tools/fixtures/export_fixtures.py):
//   tests/fixtures/collections/synthetic.json   collections_cases.py (en el repo)
//   tests/fixtures/local/collections/real.json  tu carpeta de material, solo lectura (solo local)
//
// Se repiten los pasos sobre una carpeta temporal (o la tuya) con un settings.json temporal
// y se compara lo que devuelve cada uno o, si falla, que falle (y con el mismo mensaje si es
// uno de los que ve el usuario). «{TMP}» es esa carpeta.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "disk_steps.hpp"
#include "gc/core/collections.hpp"
#include "gc/core/io.hpp"
#include "gc/core/settings.hpp"
#include "json_diff.hpp"

namespace fs = std::filesystem;
using gc::Json;
using namespace gc::core;
using gc::test::replace_all;
using gc::test::text_of;
using gc::test::write_file;

namespace {

const fs::path kFixtures = path_from_utf8(GC_FIXTURES_DIR);

std::optional<bool> opt_bool(const Json& v) { return v.is_null() ? std::nullopt : std::optional<bool>(v.get<bool>()); }

// [ruta relativa, contenido o null si es carpeta] de lo que hay bajo `top`, por ruta.
Json tree_of(const fs::path& top) {
    std::vector<std::pair<std::string, Json>> entries;
    for (const auto& e : fs::recursive_directory_iterator(top)) {
        std::string rel = replace_all(path_to_utf8(fs::relative(e.path(), top)), "\\", "/");
        entries.emplace_back(std::move(rel), e.is_regular_file() ? Json(gc::test::read_file(e.path())) : Json());
    }
    std::ranges::sort(entries, {}, &std::pair<std::string, Json>::first); // por puntos de código
    Json out = Json::array();
    for (auto& [rel, content] : entries) {
        out.push_back(Json::array({rel, std::move(content)}));
    }
    return out;
}

template <class T>
void check_result(const Json& s, const gc::Result<T>& r, const Json& result) {
    if (s.contains("error")) {
        INFO("Python falla con " << s.at("error").get<std::string>());
        REQUIRE_FALSE(r);
        if (s.contains("message")) {
            CHECK(r.error().message == s.at("message").get<std::string>());
        }
        return;
    }
    INFO("error: " << (r ? std::string() : r.error().to_string()));
    REQUIRE(r);
    const auto diff = gc::test::json_diff(s.at("result"), result);
    INFO("resultado: " << diff.value_or(""));
    CHECK_FALSE(diff);
}

std::size_t run_steps(const Json& fixture, const std::string& fname) {
    std::random_device rd;
    const fs::path tmp = fs::temp_directory_path() / ("gc_col_" + std::to_string(rd()));
    fs::create_directories(tmp);
    const fs::path root = fixture.at("root").is_null() ? tmp : path_from_utf8(fixture.at("root").get<std::string>());
    const std::string native = detail::py_path_str(path_to_utf8(root)); // str(Path) de Python
    const std::string posix = replace_all(path_to_utf8(root), "\\", "/");
    SettingsFile settings(tmp / "__settings__.json");
    fs::path default_dir = tmp / "__sin_carpeta__";
    const auto tmp_of = [&](const Json& v) {
        return v.is_string() ? Json(replace_all(v.get<std::string>(), native, "{TMP}")) : v;
    };
    const auto with_root = [&](Json r) {
        r["root"] = tmp_of(r["root"]);
        for (Json& c : r["collections"]) {
            c["path"] = tmp_of(c["path"]);
        }
        return r;
    };

    std::size_t n = 0;
    for (const Json& s : fixture.at("steps")) {
        const Json& step = s.at("step");
        INFO(fname << " :: paso " << n << ": " << step.dump().substr(0, 200));
        const auto kind = step[0].get<std::string>();
        const Json st = settings.load();
        const auto base = collections_root(st, default_dir);
        gc::Result<void> ok;
        Json result;
        // Un resultado que puede fallar: lo deja en `result` y su error en `ok`.
        const auto take = [&](auto r, auto&& convert) {
            if (r) {
                result = convert(std::move(*r));
            } else {
                ok = std::unexpected(r.error());
            }
        };
        const auto as_json = [](auto v) { return Json(std::move(v)); };
        if (kind == "files") {
            for (const Json& rel : step[1]) {
                write_file(root / path_from_utf8(rel.get<std::string>()), "x");
            }
        } else if (kind == "file") {
            write_file(root / path_from_utf8(step[1].get<std::string>()), text_of(step[2]));
        } else if (kind == "dirs") {
            for (const Json& rel : step[1]) {
                fs::create_directories(root / path_from_utf8(rel.get<std::string>()));
            }
        } else if (kind == "assets") {
            default_dir = root / path_from_utf8(step[1].get<std::string>()) / "material";
        } else if (kind == "settings") {
            write_file(tmp / "__settings__.json", replace_all(text_of(step[1]), "{TMP}", posix));
        } else if (kind == "save") {
            take(settings.save(step[1]), [](const Json&) { return Json(); });
        } else if (kind == "meta") {
            result = Json{{"material_root", tmp_of(st.value("material_root", Json()))},
                          {"collections_meta", st.value("collections_meta", Json())}};
        } else if (kind == "root") {
            result = base ? tmp_of(detail::py_path_str(path_to_utf8(*base))) : Json();
        } else if (kind == "set_root") {
            take(set_collections_root(settings, replace_all(step[1].get<std::string>(), "{TMP}", posix)),
                 [&](const std::optional<std::string>& r) { return r ? tmp_of(*r) : Json(); });
        } else if (kind == "prefs") {
            const auto label = step[4].is_null() ? std::nullopt
                                                 : std::optional<std::string_view>(step[4].get_ref<const std::string&>());
            take(set_collection_prefs(settings, step[1].get<std::string>(), opt_bool(step[2]), opt_bool(step[3]), label),
                 as_json);
        } else if (kind == "safe_id") {
            take(safe_collection_id(step[1].get<std::string>()), as_json);
        } else if (kind == "kind") {
            const auto k = collection_kind(step[1].get<std::string>());
            result = k ? Json(std::string(*k)) : Json();
        } else if (kind == "entry") {
            result = collection_entry(step[1].get<std::string>(), step[2].get<std::string>());
        } else if (kind == "scan") {
            if (base) { // _scan_dir(None, …): TypeError
                result = collection_scan(*base, step[1].get<std::string>());
            } else {
                ok = gc::fail(gc::ErrorCode::InvalidArgument, "sin raíz");
            }
        } else if (kind == "list") {
            take(list_collections(base, st), with_root);
        } else if (kind == "match") {
            result = collection_match(step[1], step[2].get<std::string>());
        } else if (kind == "search") {
            take(collections_search(base, st, step[1].get<std::string>(), step[2].get<std::string>(),
                                    step[3].get<std::string>(), step[4].get<std::int64_t>()),
                 with_root);
        } else if (kind == "create") {
            take(create_collection(base, step[1].get<std::string>()), as_json);
        } else if (kind == "add") {
            std::vector<IncomingFile> files;
            for (const Json& f : step[2]) {
                const std::string bytes = f[1].get<std::string>();
                files.push_back({f[0], [bytes](const fs::path& target) -> gc::Result<void> {
                                     if (!write_file(target, bytes)) {
                                         return gc::fail(gc::ErrorCode::Io, "no se pudo escribir");
                                     }
                                     return {};
                                 }});
            }
            take(add_collection_files(base, step[1].get<std::string>(), files), as_json);
        } else if (kind == "resolve") {
            const auto target = collection_resolve(base, replace_all(step[1].get<std::string>(), "{TMP}", posix));
            result = target ? Json(gc::test::lowered_relative(*target, *base)) : Json();
            if (s.contains("result") && s.at("result").is_string()) { // sin distinguir mayúsculas
                check_result(Json{{"step", step}, {"result", detail::py_lower(s.at("result").get<std::string>())}},
                             ok, result);
                ++n;
                continue;
            }
        } else if (kind == "resolve_items") {
            auto found = collections_search(base, st, "", "", "", 100000);
            REQUIRE(found);
            result = Json::object();
            for (const Json& it : found->at("items")) {
                const auto& ref = it.at("filename").get_ref<const std::string&>();
                const auto target = collection_resolve(base, ref);
                result[ref] = target ? Json(gc::test::lowered_relative(*target, *base)) : Json();
            }
            Json expected = Json::object();
            for (const auto& [ref, rel] : s.at("result").items()) {
                expected[ref] = rel.is_string() ? Json(detail::py_lower(rel.get<std::string>())) : rel;
            }
            check_result(Json{{"step", step}, {"result", expected}}, ok, result);
            ++n;
            continue;
        } else if (kind == "tree") {
            result = tree_of(root / path_from_utf8(step[1].get<std::string>()));
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

TEST_CASE("paridad de las colecciones de material: pasos sintéticos", "[parity][collections]") {
    const auto path = kFixtures / "collections" / "synthetic.json";
    REQUIRE(fs::exists(path)); // faltan: ejecuta tools/fixtures/export_fixtures.py
    auto file = read_json_file(path);
    REQUIRE(file);
    CHECK(run_steps(*file, "synthetic.json") > 200);
}

TEST_CASE("paridad de las colecciones de material: la tuya", "[parity][collections][local]") {
    const auto path = kFixtures / "local" / "collections" / "real.json";
    if (!fs::exists(path)) {
        SKIP("Sin fixtures locales: export_fixtures.py --real las genera desde tu carpeta de material");
    }
    auto file = read_json_file(path);
    REQUIRE(file);
    if (file->at("root").is_null() || !fs::is_directory(path_from_utf8(file->at("root").get<std::string>()))) {
        SKIP("No está tu carpeta de material");
    }
    CHECK(run_steps(*file, "real.json") >= 10);
}
