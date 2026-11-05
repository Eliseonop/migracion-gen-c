// Paridad de los modelos de IA en RAM (gc/core/model_manager.hpp) con model_manager.py de video-yt.
//
// Fixture (export_fixtures.py --solo modelos):
//   tests/fixtures/ai/model_manager.json   model_manager_cases.py: secuencias de register/touch/forget/
//                                          in_use/unload/unload_all/unload_idle/loaded/memory_report con
//                                          el reloj falso del módulo REAL y, tras cada paso, lo que
//                                          devuelve y qué modelos se descargaron; e idle_minutes() con
//                                          distintos ajustes.

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "gc/core/io.hpp"
#include "gc/core/model_manager.hpp"

using gc::Json;
using gc::core::ModelManager;

namespace {

const std::filesystem::path kFixtures = gc::core::path_from_utf8(GC_FIXTURES_DIR);

// Un número de la fixture: NaN e infinito van como texto ("nan", "inf", "-inf").
double num(const Json& j) {
    if (j.is_string()) {
        const auto s = j.get<std::string>();
        if (s == "nan") {
            return std::numeric_limits<double>::quiet_NaN();
        }
        return s == "-inf" ? -std::numeric_limits<double>::infinity() : std::numeric_limits<double>::infinity();
    }
    return j.get<double>();
}

Json loaded_json(const std::vector<ModelManager::Loaded>& loaded) {
    Json out = Json::array();
    for (const auto& l : loaded) {
        out.push_back({{"name", l.name}, {"heavy", l.heavy}, {"busy", l.busy}, {"idle_s", l.idle_s}, {"loaded_s", l.loaded_s}});
    }
    return out;
}

} // namespace

TEST_CASE("paridad de los modelos en RAM (model_manager.py)", "[parity][models]") {
    const auto path = kFixtures / "ai" / "model_manager.json";
    REQUIRE(std::filesystem::exists(path)); // faltan: export_fixtures.py --solo modelos
    auto file = gc::core::read_json_file(path);
    REQUIRE(file);
    std::size_t steps = 0;
    for (const Json& c : file->at("cases")) {
        INFO(c.at("name").get<std::string>());
        double clock = 0.0;
        Json settings = Json::object();
        std::vector<std::string> events;
        ModelManager::Options options;
        options.clock = [&] { return clock; };
        options.idle_minutes = [&] { return gc::core::model_idle_minutes(settings); };
        options.reaper = false;
        ModelManager mm(options);
        std::vector<ModelManager::InUse> stack;
        const Json& ops = c.at("ops");
        const Json& results = c.at("results");
        REQUIRE(ops.size() == results.size());
        for (std::size_t i = 0; i < ops.size(); ++i) {
            const Json& op = ops[i];
            const Json& want = results[i];
            INFO("paso " << i << ": " << op.dump());
            events.clear();
            const auto kind = op.at(0).get<std::string>();
            Json ret = nullptr;
            if (kind == "clock") {
                clock = op.at(1).get<double>();
            } else if (kind == "settings") {
                settings = op.at(1);
            } else if (kind == "register" || kind == "register_boom") {
                const auto name = op.at(1).get<std::string>();
                const bool boom = kind == "register_boom";
                mm.register_model(name, [&events, name, boom] {
                    events.push_back(name);
                    if (boom) {
                        throw std::runtime_error("la descarga falla");
                    }
                }, op.at(2).get<bool>());
            } else if (kind == "touch") {
                mm.touch(op.at(1).get<std::string>());
            } else if (kind == "forget") {
                mm.forget(op.at(1).get<std::string>());
            } else if (kind == "unload") {
                ret = mm.unload(op.at(1).get<std::string>());
            } else if (kind == "unload_all") {
                ret = mm.unload_all();
            } else if (kind == "unload_idle") {
                ret = mm.unload_idle(op.at(1).is_null() ? std::nullopt : std::optional<double>(num(op.at(1))));
            } else if (kind == "enter") {
                stack.push_back(mm.in_use(op.at(1).get<std::string>()));
            } else if (kind == "exit") {
                REQUIRE_FALSE(stack.empty());
                stack.pop_back();
            } else if (kind == "loaded") {
                ret = loaded_json(mm.loaded());
            } else if (kind == "idle_minutes") {
                ret = mm.idle_minutes();
            } else if (kind == "report") {
                const Json r = mm.memory_report();
                CHECK(r.contains("rss_bytes"));
                ret = {{"idle_minutes", r.at("idle_minutes")}, {"models", r.at("models")}};
            } else {
                FAIL("paso desconocido: " << kind);
            }
            CHECK(ret == want.at("ret"));
            CHECK(Json(events) == want.at("events"));
            ++steps;
        }
    }
    CHECK(steps >= 100);

    std::size_t n = 0;
    for (const Json& s : file->at("settings")) {
        INFO(s.at("name").get<std::string>() << ": " << s.at("settings").dump());
        CHECK(gc::core::model_idle_minutes(s.at("settings")) == num(s.at("idle_minutes")));
        ++n;
    }
    CHECK(n >= 20);
}

TEST_CASE("modelos en RAM: el hilo descarga los inactivos y avisa en el log", "[models]") {
    std::mutex mutex;
    std::vector<std::string> unloaded;
    std::vector<std::string> log;
    ModelManager::Options options;
    options.idle_minutes = [] { return 1e-6; }; // 60 µs
    options.reaper_interval_s = 0.01;
    options.on_log = [&](const std::string& m) {
        const std::scoped_lock lock(mutex);
        log.push_back(m);
    };
    ModelManager mm(options);
    mm.register_model("rvm", [&] {
        const std::scoped_lock lock(mutex);
        unloaded.emplace_back("rvm");
    });
    {
        const auto guard = mm.in_use("sam"); // no está registrado: no protege nada
        mm.register_model("sam", [&] {
            const std::scoped_lock lock(mutex);
            unloaded.emplace_back("sam");
        });
    }
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < until) {
        {
            const std::scoped_lock lock(mutex);
            if (unloaded.size() >= 2) {
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const std::scoped_lock lock(mutex);
    CHECK(unloaded.size() == 2);
    CHECK(mm.loaded().empty());
    CHECK_FALSE(log.empty());
}

TEST_CASE("modelos en RAM: la memoria del proceso", "[models]") {
#if defined(_WIN32) || defined(__linux__)
    CHECK(gc::core::process_rss_bytes() > std::uint64_t{1024} * 1024); // este proceso ocupa más de 1 MB
#endif
    ModelManager::Options options;
    options.reaper = false;
    const ModelManager mm(options);
    const Json r = mm.memory_report();
    CHECK(r.at("idle_minutes").get<double>() == gc::core::kModelIdleMinutesDefault);
    CHECK(r.at("models").empty());
}
