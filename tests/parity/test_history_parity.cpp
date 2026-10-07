// Paridad del HISTORIAL con video-yt (timeline_history.py y el historial en disco de
// timeline_store.py).
//
// Fixtures (tools/fixtures/export_fixtures.py):
//   tests/fixtures/history/synthetic.json   history_cases.py (en el repo)
//   tests/fixtures/local/history/           el historial de tus proyectos (solo local)
//
// Se repiten los mismos pasos (apilar, deshacer, rehacer, checkpoints, restaurar, pasar
// del límite) con History y se comparan las pilas tras cada paso. Al final, el historial
// escrito con history_to_json debe ser EXACTAMENTE el que escribe Python.

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <map>
#include <string>

#include "gc/core/history_store.hpp"
#include "gc/core/io.hpp"
#include "gc/core/serialize.hpp"
#include "json_diff.hpp"

using gc::Json;
using namespace gc::core;

namespace {

const std::filesystem::path kFixtures = path_from_utf8(GC_FIXTURES_DIR);

void check_same(const Json& want, const Json& got, const std::string& what) {
    const auto diff = gc::test::json_diff(want, got);
    INFO(what << ": " << diff.value_or(""));
    CHECK_FALSE(diff);
}

} // namespace

TEST_CASE("paridad del historial: pasos sintéticos y formato en disco", "[parity][history]") {
    const auto path = kFixtures / "history" / "synthetic.json";
    REQUIRE(std::filesystem::exists(path)); // faltan: ejecuta tools/fixtures/export_fixtures.py
    auto file = read_json_file(path);
    REQUIRE(file);

    std::map<std::string, TimelineSnapshot> tls;
    for (const auto& [name, raw] : file->at("timelines").items()) {
        auto tl = timeline_from_json(raw);
        REQUIRE(tl);
        check_same(raw, to_json(*tl), "timeline " + name);
        tls[name] = std::make_shared<const Timeline>(std::move(*tl));
    }
    auto name_of = [&](const TimelineSnapshot& s) -> Json {
        if (!s) {
            return nullptr;
        }
        for (const auto& [name, tl] : tls) {
            if (to_json(*tl) == to_json(*s)) {
                return name;
            }
        }
        return "?";
    };

    History h(file->at("limit").get<std::size_t>());
    for (const Json& s : file->at("steps")) {
        const Json& step = s.at("step");
        INFO("paso " << step.dump());
        const auto kind = step[0].get<std::string>();
        TimelineSnapshot restored;
        if (kind == "snapshot") {
            h.record(tls.at(step[1].get<std::string>()));
        } else if (kind == "undo") {
            restored = h.undo(tls.at(step[1].get<std::string>()));
        } else if (kind == "redo") {
            restored = h.redo(tls.at(step[1].get<std::string>()));
        } else if (kind == "checkpoint") {
            h.checkpoint(step[1].get<std::string>(), tls.at(step[2].get<std::string>()));
        } else if (kind == "restore") { // EditSession::restore_checkpoint
            restored = h.checkpoint_state(step[1].get<std::string>());
            if (restored) {
                h.record(tls.at(step[2].get<std::string>()));
            }
        }
        CHECK(name_of(restored) == s.at("restored"));
        Json undo = Json::array();
        for (const auto& t : h.undo_stack()) {
            undo.push_back(name_of(t));
        }
        Json redo = Json::array();
        for (const auto& t : h.redo_stack()) {
            redo.push_back(name_of(t));
        }
        Json cps = Json::array();
        for (const auto& [n, t] : h.checkpoint_states()) {
            cps.push_back(Json::array({n, name_of(t)}));
        }
        CHECK(undo == s.at("undo"));
        CHECK(redo == s.at("redo"));
        CHECK(cps == s.at("checkpoints"));
    }

    // El archivo: el mismo JSON que escribe Python, y leerlo da el mismo historial.
    check_same(file->at("final"), history_to_json(h), "historial final");
    check_same(file->at("final"), history_to_json(history_from_json(file->at("final"))), "ida y vuelta");

    // Uno escrito a mano: se migra lo antiguo y se descarta lo que no es una timeline.
    check_same(file->at("raw_expected"), history_to_json(history_from_json(file->at("raw"))), "historial crudo");
}

TEST_CASE("paridad del historial: tus proyectos", "[parity][history][local]") {
    const auto dir = kFixtures / "local" / "history";
    const auto path = dir / "real.json";
    if (!std::filesystem::exists(path)) {
        SKIP("Sin fixtures locales: export_fixtures.py --real copia el historial de tus proyectos");
    }
    auto file = read_json_file(path);
    REQUIRE(file);
    std::size_t entries = 0;
    for (const Json& c : file->at("cases")) {
        const auto pid = c.at("project").get<std::string>();
        INFO("proyecto " << pid);
        const History h = load_history(dir / path_from_utf8(c.at("file").get<std::string>()));
        REQUIRE(h.undo_stack().size() == c.at("undo_count").get<std::size_t>());
        REQUIRE(h.redo_stack().size() == c.at("redo_count").get<std::size_t>());
        const Json got = history_to_json(h);
        for (const char* key : {"undo", "redo"}) {
            const Json& want = c.at("first_last").at(key);
            const Json& all = got.at(key);
            Json sample = Json::array();
            if (all.size() > 1) {
                sample = Json::array({all.front(), all.back()});
            } else {
                sample = all;
            }
            check_same(want, sample, std::string(key) + " (primera y última)");
        }
        check_same(c.at("checkpoints"), got.at("checkpoints"), "checkpoints");
        entries += h.undo_stack().size();
    }
    CHECK(entries > 100);
}
