// Paridad de la DIRECCIÓN DE ESCENA con video-yt, el pack de contexto de un tramo (app/scene_direction.py,
// y timeline_context y style_context de app/motion/segment_context.py).
//
// Fixture: tests/fixtures/scene/pack.json (export_fixtures.py --solo escena, casos de scene_cases.py):
// caption_lines, range_words, timeline_context y style_context de cada proyecto en cada rango, su
// material_usage, timeline_context con el cursor y con un clip, recent_materials en cada inicio y, de cada
// llamada, el tramo y la escaleta normalizados, build_pack, pack_text, estimate_tokens, skeleton_beats con
// cada ritmo y brief_defaults, sobre un projects.json y un library.json temporales.

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <optional>
#include <string>
#include <thread>

#include "gc/core/ids.hpp"
#include "gc/core/io.hpp"
#include "gc/core/library.hpp"
#include "gc/core/projects_store.hpp"
#include "gc/core/scene_direction.hpp"
#include "gc/core/segment_context.hpp"
#include "json_diff.hpp"
#include "py.hpp"

using gc::Json;
namespace scene = gc::core::scene;

namespace {

Json load_fixture() {
    auto file = gc::core::read_json_file(gc::core::path_from_utf8(GC_FIXTURES_DIR) / "scene" / "pack.json");
    REQUIRE(file); // faltan: export_fixtures.py --solo escena
    return std::move(*file);
}

void same(const Json& expected, const Json& actual, const std::string& what) {
    const auto diff = gc::test::json_diff(expected, actual);
    INFO(what);
    INFO(diff.value_or(""));
    CHECK_FALSE(diff);
}

// El resultado o el error de Python: un LookupError es NotFound y un ValueError InvalidArgument, con su
// texto; el resto (TypeError, AttributeError, OverflowError) un crash.
bool check(const Json& c, const gc::Result<Json>& got, const std::string& what) {
    INFO(what);
    if (c.contains("error")) {
        REQUIRE_FALSE(got);
        INFO(got.error().message);
        if (c["lookup"].get<bool>() || c["value_error"].get<bool>()) {
            CHECK(got.error().code ==
                  (c["lookup"].get<bool>() ? gc::ErrorCode::NotFound : gc::ErrorCode::InvalidArgument));
            CHECK(got.error().message == c["error"].get<std::string>());
        } else {
            CHECK(got.error().code == gc::ErrorCode::Internal);
        }
        return false;
    }
    if (!got) {
        INFO(got.error().message);
        REQUIRE(got);
    }
    same(c["result"], *got, what);
    return true;
}

gc::Result<Json> string_json(const gc::Result<std::string>& r) {
    if (!r) {
        return std::unexpected(r.error());
    }
    return Json(*r);
}

void write_json(const std::filesystem::path& file, const Json& value) {
    auto text = gc::core::py_json_dumps(value);
    REQUIRE(text);
    std::ofstream(file, std::ios::binary) << *text;
}

// Un data/ temporal con los proyectos y la biblioteca de la fixture.
struct Data {
    std::filesystem::path tmp;
    gc::core::ProjectsStore store;
    gc::core::LibraryFile library;

    explicit Data(const Json& f, const char* name)
        : tmp(make(f, name)), store(tmp / "data" / "projects.json", tmp / "clips"), library(tmp / "data") {}
    Data(const Data&) = delete;
    Data& operator=(const Data&) = delete;
    ~Data() {
        std::error_code ec;
        std::filesystem::remove_all(tmp, ec);
    }

    static std::filesystem::path make(const Json& f, const char* name) {
        const auto dir = std::filesystem::temp_directory_path() /
                         (std::string(name) + std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())));
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir / "data" / "library");
        write_json(dir / "data" / "projects.json", Json{{"projects", f["projects"]}});
        write_json(dir / "data" / "library" / "library.json", f["library"]);
        return dir;
    }

    gc::core::Project project(const Json& pid) {
        auto p = store.get_project(pid.get<std::string>());
        REQUIRE(p);
        REQUIRE(*p);
        return std::move(**p);
    }
};

} // namespace

TEST_CASE("paridad de la dirección de escena: lo que ya ocupa un tramo (timeline, estilo y subtítulos)",
          "[parity][scene]") {
    const Json f = load_fixture();
    Data data(f, "gc_scene_pack_context_parity_");
    REQUIRE(f["context"].size() == f["projects"].size());
    for (const Json& ctx : f["context"]) {
        const auto p = data.project(ctx["id"]);
        const std::string pid = ctx["id"].get<std::string>();
        same(ctx["usage"], scene::material_usage(p), "material_usage " + pid);
        for (const Json& r : ctx["ranges"]) {
            const double a = r["range"][0].get<double>();
            const double b = r["range"][1].get<double>();
            const std::string what = pid + " " + r["range"].dump();
            same(r["captions"], scene::caption_lines(p, a, b), "caption_lines " + what);
            same(r["words"], scene::range_words(p, a, b), "range_words " + what);
            same(r["timeline"], gc::core::timeline_context(p, a, b), "timeline_context " + what);
            same(r["style"], gc::core::style_context(p, a, b), "style_context " + what);
        }
    }

    const auto main = data.project(Json("k-main"));
    for (const Json& c : f["timeline_calls"]) {
        const Json& a = c["args"];
        const std::optional<double> playhead = a[2].is_null() ? std::nullopt : std::optional(a[2].get<double>());
        const std::string clip = a[3].is_string() ? a[3].get<std::string>() : std::string();
        same(c["result"], gc::core::timeline_context(main, a[0].get<double>(), a[1].get<double>(), playhead, clip),
             "timeline_context " + a.dump());
    }
    const auto catalog = scene::material_catalog(main, data.library);
    REQUIRE(catalog);
    const Json usage = scene::material_usage(main);
    for (const Json& c : f["recent"]) {
        same(c["result"], scene::recent_materials(*catalog, usage, c["start"].get<double>()),
             "recent_materials " + c["start"].dump());
    }
}

TEST_CASE("paridad de la dirección de escena: el pack de un tramo, su texto, los beats y el brief", "[parity][scene]") {
    const Json f = load_fixture();
    Data data(f, "gc_scene_pack_parity_");
    std::size_t errors = 0;
    std::size_t texts = 0;
    for (const Json& c : f["packs"]) {
        const std::string what = c["project_id"].get<std::string>() + " " + c["segment_raw"].dump();
        INFO(what);
        // Como scene_direction_pack: el tramo y la escaleta normalizados con el reloj y los uuid desde cero.
        gc::core::SequentialIdSource ids;
        std::int64_t ticks = 0;
        scene::SceneEnv env{ids, [&ticks] { return 1700000000.0 + 0.25 * static_cast<double>(++ticks); }};
        const auto p = data.project(c["project_id"]);
        auto seg = scene::normalize_segment(env, c["segment_raw"]);
        REQUIRE(seg);
        REQUIRE(*seg);
        same(c["segment"], **seg, "normalize_segment");
        auto doc = c["segments_raw"].is_array() ? scene::normalize_doc(env, Json{{"segments", c["segments_raw"]}})
                                                : scene::load(env, p);
        REQUIRE(doc);
        same(c["doc"], *doc, "escaleta");
        const auto mine = scene::build_pack(env, p, data.library, **seg, *doc);
        const bool built = check(c["pack"], mine, "build_pack");
        errors += built ? 0 : 1;
        if (!built) {
            continue;
        }
        // Lo demás, sobre el pack de Python (así un fallo apunta a la función que lo tiene).
        const Json& pack = c["pack"]["result"];
        if (check(c["text"], string_json(scene::pack_text(pack)), "pack_text")) {
            ++texts;
            CHECK(scene::estimate_tokens(c["text"]["result"].get<std::string>()) == c["tokens"].get<std::int64_t>());
        } else {
            ++errors;
        }
        // Y el texto del pack de C++: lo que el JSON no distingue (un -0.0, el signo de un NaN) se ve al
        // escribirlo.
        check(c["text"], string_json(scene::pack_text(*mine)), "pack_text del pack de C++");
        for (const Json& s : c["skeleton"]) {
            errors += check(s, scene::skeleton_beats(pack, s["pace"].get<double>()), "skeleton_beats " + s["pace"].dump())
                          ? 0
                          : 1;
        }
        errors += check(c["brief"], scene::brief_defaults(pack), "brief_defaults") ? 0 : 1;
    }
    CHECK(f["packs"].size() == 55);
    CHECK(texts == 48);
    CHECK(errors == 23);
}

TEST_CASE("paridad de la dirección de escena: el pack de los tramos de tus proyectos", "[parity][scene][local]") {
    const auto path = gc::core::path_from_utf8(GC_FIXTURES_DIR) / "local" / "scene" / "real.json";
    if (!std::filesystem::exists(path)) {
        SKIP("Sin fixtures locales: export_fixtures.py --solo escena --real las genera desde tu projects.json");
    }
    auto file = gc::core::read_json_file(path);
    REQUIRE(file);
    const Json& f = *file;
    if (f["cases"].empty()) {
        SKIP("Tus proyectos no tienen tramos");
    }
    const auto tmp = std::filesystem::temp_directory_path() /
                     ("gc_scene_pack_real_parity_" +
                      std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())));
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(tmp / "data" / "library");
    write_json(tmp / "data" / "projects.json", f["projects"]);
    if (!f["library"].is_null()) {
        write_json(tmp / "data" / "library" / "library.json", f["library"]);
    }
    gc::core::ProjectsStore store(tmp / "data" / "projects.json", tmp / "clips");
    const gc::core::LibraryFile library(tmp / "data");
    std::size_t texts = 0;
    for (const Json& c : f["cases"]) {
        const std::string what = c["project_id"].get<std::string>() + " #" + c["index"].dump();
        INFO(what);
        auto loaded = store.get_project(c["project_id"].get<std::string>());
        REQUIRE(loaded);
        REQUIRE(*loaded);
        const gc::core::Project& p = **loaded;
        // Como en Python: la escaleta guardada (o la propuesta) con el reloj y los uuid desde cero.
        gc::core::SequentialIdSource ids;
        std::int64_t ticks = 0;
        scene::SceneEnv env{ids, [&ticks] { return 1700000000.0 + 0.25 * static_cast<double>(++ticks); }};
        Json doc;
        if (c["source"] == "load") {
            auto d = scene::load(env, p);
            REQUIRE(d);
            doc = std::move(*d);
        } else {
            auto segs = scene::auto_segments(env, p, Json::array());
            REQUIRE(segs);
            doc = Json{{"version", 1}, {"segments", std::move(*segs)}};
        }
        const auto i = c["index"].get<std::size_t>();
        REQUIRE(i < doc["segments"].size());
        const Json& seg = doc["segments"][i];
        same(c["segment"], seg, "tramo");
        const auto mine = scene::build_pack(env, p, library, seg, doc);
        if (!check(c["pack"], mine, "build_pack")) {
            continue;
        }
        const Json& pack = c["pack"]["result"];
        if (check(c["text"], string_json(scene::pack_text(pack)), "pack_text")) {
            ++texts;
            CHECK(scene::estimate_tokens(c["text"]["result"].get<std::string>()) == c["tokens"].get<std::int64_t>());
        }
        check(c["text"], string_json(scene::pack_text(*mine)), "pack_text del pack de C++");
        check(c["skeleton"], scene::skeleton_beats(pack, 2.5), "skeleton_beats");
        check(c["brief"], scene::brief_defaults(pack), "brief_defaults");
    }
    CHECK(texts > 0);
    std::error_code ec;
    std::filesystem::remove_all(tmp, ec);
}

TEST_CASE("dirección de escena: estimate_tokens redondea al par como round() de Python", "[scene]") {
    CHECK(scene::estimate_tokens("") == 1);
    CHECK(scene::estimate_tokens("ab") == 1);            // 0,56
    CHECK(scene::estimate_tokens("abcdefghi") == 2);     // 9 / 3,6 = 2,5 → 2 (al par)
    CHECK(scene::estimate_tokens("abcdefghijklmn") == 4); // 3,89
    CHECK(scene::estimate_tokens("ñññññññññ") == 2);     // caracteres, no bytes
}
