// Paridad de la DIRECCIÓN DE ESCENA con video-yt (app/scene_direction.py), la parte sin biblioteca ni IA.
//
// Fixture: tests/fixtures/scene/direction.json (export_fixtures.py --solo escena, casos de
// scene_cases.py): los catálogos, normalize_segment/component/doc/blueprint de cada valor (con el reloj
// y los uuid desde el principio), de cada proyecto timeline_duration, script_units, text_in_range,
// status_summary y auto_segments, y los pasos de guardar sobre un projects.json temporal.

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <thread>

#include "gc/core/ids.hpp"
#include "gc/core/io.hpp"
#include "gc/core/projects_store.hpp"
#include "gc/core/scene_direction.hpp"
#include "json_diff.hpp"

using gc::Json;
namespace scene = gc::core::scene;

namespace {

Json load_fixture() {
    auto file = gc::core::read_json_file(gc::core::path_from_utf8(GC_FIXTURES_DIR) / "scene" / "direction.json");
    REQUIRE(file); // faltan: export_fixtures.py --solo escena
    return std::move(*file);
}

void same(const Json& expected, const Json& actual, const std::string& what) {
    const auto diff = gc::test::json_diff(expected, actual);
    INFO(what);
    INFO(diff.value_or(""));
    CHECK_FALSE(diff);
}

// El reloj y los uuid de Python en los casos: time.time() = 1700000000 + n/4 en la n-ésima llamada.
struct Fakes {
    gc::core::SequentialIdSource ids;
    std::int64_t n = 0;
    scene::SceneEnv env{ids, [this] { return 1700000000.0 + 0.25 * static_cast<double>(++n); }};

    void reset() {
        ids = gc::core::SequentialIdSource();
        n = 0;
    }
};

// El resultado o el error de Python: un LookupError es NotFound con su texto y el resto (TypeError,
// OverflowError) un crash.
void check(const Json& c, const gc::Result<Json>& got, const std::string& what) {
    INFO(what);
    if (c.contains("error")) {
        REQUIRE_FALSE(got);
        INFO(got.error().message);
        if (c["lookup"].get<bool>()) {
            CHECK(got.error().code == gc::ErrorCode::NotFound);
            CHECK(got.error().message == c["error"].get<std::string>());
        } else {
            CHECK(got.error().code == gc::ErrorCode::Internal);
        }
        return;
    }
    if (!got) {
        INFO(got.error().message);
        REQUIRE(got);
    }
    same(c["result"], *got, what);
}

gc::Result<Json> optional_json(const gc::Result<std::optional<Json>>& r) {
    if (!r) {
        return std::unexpected(r.error());
    }
    return r->value_or(Json());
}

std::filesystem::path temp_dir(const char* name) {
    const auto tmp = std::filesystem::temp_directory_path() /
                     (std::string(name) + std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())));
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(tmp);
    return tmp;
}

void write_projects(const std::filesystem::path& file, const Json& list) {
    auto text = gc::core::py_json_dumps(Json{{"projects", list}});
    REQUIRE(text);
    std::ofstream(file, std::ios::binary) << *text;
}

} // namespace

TEST_CASE("paridad de la dirección de escena: catálogos", "[parity][scene]") {
    const Json f = load_fixture();
    const Json& cat = f["catalog"];
    REQUIRE(cat["directions"].size() == scene::directions().size());
    for (std::size_t i = 0; i < scene::directions().size(); ++i) {
        const auto& d = scene::directions()[i];
        CHECK(cat["directions"][i] == Json{{"key", d.key}, {"label", d.label}, {"summary", d.summary}});
    }
    CHECK(cat["default_direction"] == scene::kDefaultDirection);
    REQUIRE(cat["modes"].size() == scene::modes().size());
    for (std::size_t i = 0; i < scene::modes().size(); ++i) {
        const auto& m = scene::modes()[i];
        CHECK(cat["modes"][i] == Json{{"key", m.key}, {"label", m.label}, {"ai", m.ai}});
    }
    const auto list = [](const std::vector<std::string_view>& v) {
        Json out = Json::array();
        for (const std::string_view s : v) {
            out.push_back(s);
        }
        return out;
    };
    CHECK(cat["statuses"] == list(scene::statuses()));
    CHECK(cat["material_roles"] == list(scene::material_roles()));
    CHECK(cat["vocabulary"] == list(scene::vocabulary()));
}

TEST_CASE("paridad de la dirección de escena: normalizar tramos, escaletas y blueprints", "[parity][scene]") {
    const Json f = load_fixture();
    Fakes fk;
    std::size_t n = 0;
    std::size_t errors = 0;
    for (const Json& c : f["segments"]) {
        fk.reset();
        check(c, optional_json(scene::normalize_segment(fk.env, c["raw"])), "segment " + c["raw"].dump());
        errors += c.contains("error") ? 1 : 0;
        ++n;
    }
    for (const Json& c : f["components"]) {
        same(c["result"], scene::normalize_component(c["raw"]).value_or(Json()), "component " + c["raw"].dump());
        ++n;
    }
    for (const Json& c : f["docs"]) {
        fk.reset();
        check(c, scene::normalize_doc(fk.env, c["raw"]), "doc " + c["raw"].dump());
        errors += c.contains("error") ? 1 : 0;
        ++n;
    }
    for (const Json& c : f["blueprints"]) {
        fk.reset();
        same(c["result"], scene::normalize_blueprint(fk.env, c["raw"]), "blueprint " + c["raw"].dump());
        ++n;
    }
    CHECK(n == 178);
    CHECK(errors == 10);
}

TEST_CASE("paridad de la dirección de escena: guion, estado y escaleta propuesta", "[parity][scene]") {
    const Json f = load_fixture();
    const auto tmp = temp_dir("gc_scene_parity_");
    write_projects(tmp / "projects.json", f["projects"]);
    gc::core::ProjectsStore store(tmp / "projects.json", tmp / "clips");
    Fakes fk;
    std::size_t autos = 0;
    for (const Json& c : f["cases"]) {
        const std::string pid = c["id"].get<std::string>();
        INFO(pid);
        auto loaded = store.get_project(pid);
        REQUIRE(loaded);
        REQUIRE(*loaded);
        const gc::core::Project& proj = **loaded;
        same(c["duration"], Json(scene::timeline_duration(proj)), pid + " duration");
        const auto [units, source] = scene::script_units(proj);
        same(c["units"], units, pid + " units");
        CHECK(source == c["source"].get<std::string>());
        for (const Json& r : c["ranges"]) {
            CHECK(scene::text_in_range(units, r[0].get<double>(), r[1].get<double>()) == r[2].get<std::string>());
        }
        fk.reset();
        check(c["status"], scene::status_summary(fk.env, proj), pid + " status");
        for (const Json& a : c["auto"]) {
            fk.reset();
            check(a, scene::auto_segments(fk.env, proj, a["keep"]), pid + " auto " + a["keep"].dump());
            ++autos;
        }
    }
    CHECK(autos == 32);
    std::error_code ec;
    std::filesystem::remove_all(tmp, ec);
}

TEST_CASE("paridad de la dirección de escena: guardar en projects.json", "[parity][scene]") {
    const Json f = load_fixture();
    const auto tmp = temp_dir("gc_scene_store_parity_");
    write_projects(tmp / "projects.json", f["store_projects"]);
    gc::core::ProjectsStore store(tmp / "projects.json", tmp / "clips");
    Fakes fk; // el reloj y los uuid siguen de un paso al siguiente, como en Python
    const auto project = [&](const Json& pid) -> gc::core::Project {
        auto p = store.get_project(pid.get<std::string>());
        REQUIRE(p);
        REQUIRE(*p);
        return std::move(**p);
    };
    std::size_t errors = 0;
    for (const Json& c : f["steps"]) {
        const std::string call = c["call"].get<std::string>();
        const Json& a = c["args"];
        const std::string pid = a[0].get<std::string>();
        gc::Result<Json> got;
        if (call == "load") {
            got = scene::load(fk.env, project(a[0]));
        } else if (call == "load_blueprint") {
            got = scene::load_blueprint(fk.env, project(a[0]));
        } else if (call == "get_segment") {
            auto r = scene::get_segment(fk.env, project(a[0]), a[1].get<std::string>());
            got = r ? gc::Result<Json>(Json::array({r->first, r->second})) : std::unexpected(r.error());
        } else if (call == "update_segment") {
            got = scene::update_segment(fk.env, store, pid, a[1].get<std::string>(), a[2]);
        } else if (call == "apply_plan_all") {
            got = scene::apply_plan_all(fk.env, store, pid, a[1]);
        } else if (call == "save") {
            got = scene::save(fk.env, store, pid, a[1]);
        } else if (call == "save_blueprint") {
            got = scene::save_blueprint(fk.env, store, pid, a[1]);
        } else if (call == "status_summary") {
            got = scene::status_summary(fk.env, project(a[0]));
        } else {
            FAIL("paso desconocido: " << call);
        }
        const std::string what = call + a.dump();
        check(c, got, what);
        errors += c.contains("error") ? 1 : 0;
        Json saved;
        for (const Json& p : store.load()["projects"]) {
            if (p["id"] == pid) {
                saved = p;
            }
        }
        same(c["project"], saved, what + " (projects.json)");
    }
    CHECK(errors == 10);
    std::error_code ec;
    std::filesystem::remove_all(tmp, ec);
}

TEST_CASE("dirección de escena: un NaN al ordenar no rompe el orden", "[scene]") {
    // Con un NaN, `<` no es un orden débil estricto (indefinido en std::stable_sort). Los NaN van al
    // final y, entre ellos, en el orden de entrada. Python no falla, pero su orden depende de timsort.
    Fakes fk;
    const Json raw{{"segments", Json::array({Json{{"id", "sd_a"}, {"start", 0}, {"end", "nan"}},
                                             Json{{"id", "sd_b"}, {"start", 0}, {"end", 3}},
                                             Json{{"id", "sd_c"}, {"start", 0}, {"end", "nan"}},
                                             Json{{"id", "sd_d"}, {"start", 0}, {"end", 1}}})}};
    const auto doc = scene::normalize_doc(fk.env, raw);
    REQUIRE(doc);
    std::string order;
    for (const Json& s : (*doc)["segments"]) {
        order += s["id"].get<std::string>() + " ";
    }
    CHECK(order == "sd_d sd_b sd_a sd_c ");
}
