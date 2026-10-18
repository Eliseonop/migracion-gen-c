// Paridad del TEXTO 3D con video-yt: text3d.py y text_warp_spec / text_is_3d de text_ass.py, y
// qué textos van en capa propia (_prepare_texts de compose.py).
//
// Fixtures (tools/fixtures/export_fixtures.py, casos en text_cases.py):
//   tests/fixtures/text/warp.json        TEXT_CLIPS y WARP_CLIPS en tres cuadros, y plane_project
//   tests/fixtures/local/text/warp.json  tus textos con el tamaño y los fps de su timeline
//
// Exacto salvo lo que pasa por sin() y cos(): 1e-12 relativo. Las esquinas van con los 6 decimales
// que lee FFmpeg; ahí, 2e-6 absoluto (otra libm puede mover el último decimal).

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

#include "gc/core/fx_catalog.hpp"
#include "gc/core/io.hpp"
#include "gc/core/serialize.hpp"
#include "gc/core/text3d.hpp"
#include "gc/core/text_metrics.hpp"
#include "gc/core/text_style.hpp"

using gc::Json;
using namespace gc::core;
namespace fs = std::filesystem;

namespace {

const fs::path kFixtures = path_from_utf8(GC_FIXTURES_DIR);

Json load(const fs::path& p) {
    auto j = read_json_file(p);
    REQUIRE(j);
    return std::move(*j);
}

// Las animaciones de catálogo (zoom_in…) cambian la pose: sin el catálogo no las hay.
void load_catalog() {
    auto catalog = FxCatalog::load(path_from_utf8(GC_RESOURCES_DIR) / "fx" / "catalog.json");
    REQUIRE(catalog);
    set_fx_catalog(std::make_shared<const FxCatalog>(std::move(*catalog)));
}

bool close(double a, double b, double rel = 1e-12, double abs = 0.0) {
    return a == b || std::abs(a - b) <= std::max(abs, rel * std::max(std::abs(a), std::abs(b)));
}

void check_clip(const Json& c) {
    INFO(c.at("name").get<std::string>());
    auto parsed = clip_from_json(c.at("clip"));
    REQUIRE(parsed);
    const TimelineClip& clip = *parsed;
    const Json& style = c.at("style");
    for (const Json& fr : c.at("frames")) {
        const double w = fr.at("w").get<double>();
        const double h = fr.at("h").get<double>();
        const double fps = fr.at("fps").get<double>();
        INFO(w << "×" << h << " a " << fps << " fps");
        CHECK(text_own_layer(clip, fps) == fr.at("own_layer").get<bool>());
        CHECK(text_is_3d(clip, fps) == fr.at("is_3d").get<bool>());
        const auto frames = text_frames(clip, fps);
        const Json& want_frames = fr.at("text_frames");
        REQUIRE(frames.size() == want_frames.size());
        for (std::size_t i = 0; i < frames.size(); ++i) {
            CHECK(frames[i].first == want_frames[i][0].get<std::int64_t>());
            CHECK(frames[i].second == want_frames[i][1].get<double>());
        }
        CHECK(text3d_focal(h, style.contains("perspective") ? style.at("perspective") : Json()) ==
              fr.at("focal").get<double>());
        const Json posed = posed_text_style(clip, style, 0.0);
        const auto ang = text3d_angles(posed);
        if (fr.at("angles").is_null()) {
            CHECK_FALSE(ang);
        } else {
            REQUIRE(ang);
            CHECK(ang->first == fr.at("angles")[0].get<double>());
            CHECK(ang->second == fr.at("angles")[1].get<double>());
        }
        const double font_px = text_font_px(posed, h);
        const std::string shown =
            apply_text_case(clip.text.value_or(""), style.contains("text_case") ? style.at("text_case") : Json());
        const auto half = text3d_half_extents(posed, shown, w, h, font_px, text_shadow(posed, font_px));
        INFO("semiextensión " << half.first << " × " << half.second << ", letra " << font_px << " px");
        CHECK(close(half.first, fr.at("half")[0].get<double>()));
        CHECK(close(half.second, fr.at("half")[1].get<double>()));
        const auto warp = text_warp_spec(clip, style, w, h, fps);
        const Json& want = fr.at("warp");
        if (want.is_null()) {
            CHECK_FALSE(warp);
            continue;
        }
        REQUIRE(warp);
        const Json& r = want.at("region");
        CHECK(warp->region.x0 == r[0].get<std::int64_t>());
        CHECK(warp->region.y0 == r[1].get<std::int64_t>());
        CHECK(warp->region.x1 == r[2].get<std::int64_t>());
        CHECK(warp->region.y1 == r[3].get<std::int64_t>());
        REQUIRE(warp->frames.size() == want.at("frames").size());
        REQUIRE(warp->corners.size() == want.at("corners").size());
        for (std::size_t i = 0; i < warp->frames.size(); ++i) {
            CHECK(warp->frames[i] == want.at("frames")[i].get<std::int64_t>());
            for (std::size_t k = 0; k < 4; ++k) {
                const Json& pt = want.at("corners")[i][k];
                CHECK(close(warp->corners[i][k].first, pt[0].get<double>(), 1e-12, 2e-6));
                CHECK(close(warp->corners[i][k].second, pt[1].get<double>(), 1e-12, 2e-6));
            }
        }
    }
}

} // namespace

TEST_CASE("paridad del texto 3D: deformación, fotogramas y capa propia", "[parity][text][text3d]") {
    load_catalog();
    const Json fx = load(kFixtures / "text" / "warp.json");
    std::size_t warped = 0;
    for (const Json& c : fx.at("clips")) {
        check_clip(c);
        for (const Json& fr : c.at("frames")) {
            warped += fr.at("warp").is_null() ? 0 : 1;
        }
    }
    CHECK(warped >= 50);
    for (const Json& p : fx.at("project")) {
        const Projected q = text3d_project(p.at("cx").get<double>(), p.at("cy").get<double>(), p.at("rx").get<double>(),
                                           p.at("ry").get<double>(), p.at("f").get<double>(), p.at("x").get<double>(),
                                           p.at("y").get<double>());
        INFO(p.dump());
        CHECK(close(q.x, p.at("out")[0].get<double>()));
        CHECK(close(q.y, p.at("out")[1].get<double>()));
        CHECK(close(q.w, p.at("out")[2].get<double>()));
    }
}

TEST_CASE("paridad del texto 3D: tus textos", "[parity][text][text3d][local]") {
    const auto path = kFixtures / "local" / "text" / "warp.json";
    if (!fs::exists(path)) {
        SKIP("Sin fixtures locales: export_fixtures.py --real las genera desde data/projects.json");
    }
    load_catalog();
    const Json fx = load(path);
    for (const Json& c : fx.at("clips")) {
        check_clip(c);
    }
}

TEST_CASE("texto 3D: las esquinas de un fotograma, como la expresión de perspective", "[text3d]") {
    TextWarp w;
    w.frames = {10, 11, 12};
    for (int i = 0; i < 3; ++i) {
        Corners c{};
        c[0] = {static_cast<double>(i), 0.0};
        w.corners.push_back(c);
    }
    CHECK(w.corners_at(0)[0].first == 0.0);  // antes del clip: el primero
    CHECK(w.corners_at(10)[0].first == 0.0);
    CHECK(w.corners_at(11)[0].first == 1.0);
    CHECK(w.corners_at(12)[0].first == 2.0);
    CHECK(w.corners_at(99)[0].first == 2.0); // después: el último
}
