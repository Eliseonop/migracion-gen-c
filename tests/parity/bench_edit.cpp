// Medición (oculta: solo con `gc_tests "[.bench]"`): coste de una edición sobre la
// timeline real más grande. Un arrastre aplica una operación por fotograma, así que
// tiene que caber holgadamente en 16 ms (60 fps).

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <iostream>

#include "gc/core/edit.hpp"
#include "gc/core/io.hpp"
#include "gc/core/serialize.hpp"

using gc::Json;
using namespace gc::core;

TEST_CASE("coste de una edición en la timeline real más grande", "[.bench]") {
    const auto path = path_from_utf8(GC_FIXTURES_DIR) / "local" / "edit" / "real.json";
    if (!std::filesystem::exists(path)) {
        SKIP("Sin fixtures locales");
    }
    auto file = read_json_file(path);
    REQUIRE(file);
    Timeline biggest;
    for (const auto& [id, raw] : file->at("timelines").items()) {
        auto tl = timeline_from_json(raw);
        REQUIRE(tl);
        if (tl->clips.size() > biggest.clips.size()) {
            biggest = std::move(*tl);
        }
    }
    REQUIRE_FALSE(biggest.clips.empty());
    const std::string clip_id = biggest.clips[biggest.clips.size() / 2]->id;

    SequentialIdSource ids;
    constexpr int kRuns = 2000;
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kRuns; ++i) {
        auto res = apply_command(biggest, ops::MoveClip{clip_id, 1000.0 + i * 0.01, std::nullopt}, ids);
        REQUIRE(res);
    }
    const auto t1 = std::chrono::steady_clock::now();
    const double us = std::chrono::duration<double, std::micro>(t1 - t0).count() / kRuns;
    std::cout << "move_clip + settle + validación en " << biggest.clips.size() << " clips: " << us
              << " µs por operación\n";
}
