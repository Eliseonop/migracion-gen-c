// Paridad de la caché LRU con tope en bytes (gc/core/lru_bytes.hpp) con lib/lruBytes.js de video-yt.
//
// Fixture (export_fixtures.py --solo lru, con Node):
//   tests/fixtures/editor/lru_bytes.json   lru_cases.py: secuencias de set/get/peek/has/resize/delete/
//                                          purge/clear y, tras cada paso, lo que devuelve, las claves
//                                          en orden, los bytes, el tamaño y lo descartado (onEvict).

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <filesystem>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "gc/core/io.hpp"
#include "gc/core/lru_bytes.hpp"

using gc::Json;
using gc::core::LruBytes;

namespace {

const std::filesystem::path kFixtures = gc::core::path_from_utf8(GC_FIXTURES_DIR);

} // namespace

TEST_CASE("paridad de la caché LRU con tope en bytes (lruBytes.js)", "[parity][lru]") {
    const auto path = kFixtures / "editor" / "lru_bytes.json";
    REQUIRE(std::filesystem::exists(path)); // faltan: export_fixtures.py --solo lru
    auto file = gc::core::read_json_file(path);
    REQUIRE(file);
    std::size_t steps = 0;
    for (const Json& c : *file) {
        INFO(c.at("name").get<std::string>());
        std::vector<std::pair<std::string, Json>> evicted;
        const auto on_evict = [&](const std::string& k, const Json& v) { evicted.emplace_back(k, v); };
        LruBytes<std::string, Json> cache = c.at("maxBytes").is_null()
                                                ? LruBytes<std::string, Json>(LruBytes<std::string, Json>::kDefaultMaxBytes, on_evict)
                                                : LruBytes<std::string, Json>(c.at("maxBytes").get<std::size_t>(), on_evict);
        const Json& ops = c.at("ops");
        const Json& results = c.at("results");
        REQUIRE(ops.size() == results.size());
        for (std::size_t i = 0; i < ops.size(); ++i) {
            const Json& op = ops[i];
            const Json& want = results[i];
            INFO("paso " << i << ": " << op.dump());
            evicted.clear();
            const auto name = op.at(0).get<std::string>();
            Json ret = nullptr;
            if (name == "set") {
                ret = cache.set(op.at(1).get<std::string>(), op.at(2), op.at(3).get<std::size_t>());
            } else if (name == "get") {
                const Json* v = cache.get(op.at(1).get<std::string>());
                ret = v != nullptr ? *v : Json(nullptr);
            } else if (name == "peek") {
                const Json* v = cache.peek(op.at(1).get<std::string>());
                ret = v != nullptr ? *v : Json(nullptr);
            } else if (name == "has") {
                ret = cache.contains(op.at(1).get<std::string>());
            } else if (name == "resize") {
                cache.resize(op.at(1).get<std::string>(), op.at(2).get<std::size_t>());
            } else if (name == "delete") {
                cache.erase(op.at(1).get<std::string>());
            } else if (name == "purge") {
                const auto keep = op.at(1).get<std::set<std::string>>();
                cache.purge([&](const std::string& k, const Json&) { return keep.contains(k); });
            } else if (name == "clear") {
                cache.clear();
            } else {
                FAIL("op desconocida: " << name);
            }
            CHECK(ret == want.at("ret"));
            CHECK(cache.keys() == want.at("keys").get<std::vector<std::string>>());
            CHECK(cache.bytes() == want.at("bytes").get<std::size_t>());
            CHECK(cache.size() == want.at("size").get<std::size_t>());
            CHECK(cache.max_bytes() == want.at("maxBytes").get<std::size_t>());
            std::vector<std::pair<std::string, Json>> want_evicted;
            for (const Json& e : want.at("evicted")) {
                want_evicted.emplace_back(e.at(0).get<std::string>(), e.at(1));
            }
            CHECK(evicted == want_evicted);
            ++steps;
        }
    }
    CHECK(steps > 1000);
}

TEST_CASE("LruBytes: un onEvict que lanza no rompe la caché; pixel_bytes", "[lru]") {
    LruBytes<int, int> cache(10, [](const int&, const int&) { throw std::runtime_error("limpieza fallida"); });
    cache.set(1, 1, 6);
    cache.set(2, 2, 6); // descarta la 1 y su aviso lanza
    CHECK(cache.keys() == std::vector<int>{2});
    CHECK(cache.bytes() == 6);
    CHECK(gc::core::pixel_bytes(10, 20) == 800);
    CHECK(gc::core::pixel_bytes(0, 20) == 0);
    CHECK(gc::core::pixel_bytes(-3, 20) == 0);
    CHECK(gc::core::pixel_bytes(3840, 2160) == std::size_t{33'177'600});
}
