// Paridad del FOTOGRAMA con el preview de video-yt: lo que drawComposite (canvas.js) dibuja
// del material de cada capa, ejecutado con Node sobre un contexto 2D que graba cada
// drawImage con su transformación (tools/fixtures/frame_js.mjs).
//
// Fixtures (tools/fixtures/export_fixtures.py):
//   tests/fixtures/frame/synthetic.json   frame_cases.py (en el repo)
//   tests/fixtures/local/frame/real.json  tus timelines, lejos de los bordes de los clips
//
// Por cada dibujo: el clip, el rectángulo de la fuente, la transformación afín de la fuente
// al cuadro, la opacidad, el recorte de la cortinilla y el tiempo de fuente, con las
// reglas del preview (kPreviewLayers). Exacto salvo en las capas giradas, donde entran
// sin/cos de V8 frente a los de la libm (tolerancia relativa de 1e-12). Se dejan fuera los
// clips cuyo clipDur de JS (sin redondeo) no es la duración de Python: sus ventanas de
// aparición y salida no coinciden por diseño.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "gc/core/clip_adjust.hpp"
#include "gc/core/frame.hpp"
#include "gc/core/fx_catalog.hpp"
#include "gc/core/io.hpp"
#include "gc/core/serialize.hpp"

using gc::Json;
using namespace gc::core;

namespace {

constexpr double kTrigTol = 1e-12;
const std::filesystem::path kFixtures = path_from_utf8(GC_FIXTURES_DIR);

void load_catalog() {
    auto catalog = FxCatalog::load(path_from_utf8(GC_RESOURCES_DIR) / "fx" / "catalog.json");
    REQUIRE(catalog);
    set_fx_catalog(std::make_shared<const FxCatalog>(std::move(*catalog)));
}

struct Drawn {
    std::string id;
    std::vector<double> nums; // src (4), m (6), alpha, src_t
    std::vector<double> clip; // 8 números o vacío
    bool rotated = false;
};

std::string base_id(std::string id) {
    if (id.ends_with(kGhostSuffix)) {
        id.resize(id.size() - kGhostSuffix.size());
    }
    return id;
}

std::vector<Drawn> from_cpp(const std::vector<FrameLayer>& layers) {
    std::vector<Drawn> out;
    for (const FrameLayer& l : layers) {
        for (const Blit& b : l.blits) {
            Drawn d{base_id(l.clip->id),
                    {b.src.sx, b.src.sy, b.src.sw, b.src.sh, b.m.a, b.m.b, b.m.c, b.m.d, b.m.e, b.m.f, l.alpha,
                     l.src_t},
                    {},
                    l.pose.rotation != 0.0};
            if (l.wipe) {
                for (const Point& p : *l.wipe) {
                    d.clip.push_back(p.x);
                    d.clip.push_back(p.y);
                }
            }
            out.push_back(std::move(d));
        }
    }
    return out;
}

std::vector<Drawn> from_js(const Json& frame) {
    std::vector<Drawn> out;
    for (const Json& b : frame) {
        Drawn d{b.at("id").get<std::string>(), {}, {}, false};
        for (const Json& v : b.at("src")) {
            d.nums.push_back(v.get<double>());
        }
        for (const Json& v : b.at("m")) {
            d.nums.push_back(v.get<double>());
        }
        d.nums.push_back(b.at("alpha").get<double>());
        d.nums.push_back(b.at("src_t").get<double>());
        if (!b.at("clip").is_null()) {
            for (const Json& p : b.at("clip")) {
                d.clip.push_back(p.at(0).get<double>());
                d.clip.push_back(p.at(1).get<double>());
            }
        }
        out.push_back(std::move(d));
    }
    return out;
}

bool near(double want, double got, double tol) {
    return want == got || std::abs(want - got) <= tol * std::max({1.0, std::abs(want), std::abs(got)});
}

bool same(const std::vector<double>& want, const std::vector<double>& got, double tol) {
    if (want.size() != got.size()) {
        return false;
    }
    for (std::size_t i = 0; i < want.size(); ++i) {
        if (!near(want[i], got[i], tol)) {
            return false;
        }
    }
    return true;
}

std::string dump(const std::vector<double>& v) {
    Json j = Json::array();
    for (const double x : v) {
        j.push_back(x);
    }
    return j.dump();
}

struct Stats {
    std::size_t heads = 0;
    std::size_t blits = 0;
    std::size_t skipped = 0;
};

Stats check_cases(const Json& file, const std::string& fname) {
    load_catalog();
    Stats st;
    for (const Json& c : file.at("cases")) {
        INFO(fname << " :: " << c.at("name").get<std::string>());
        auto tl = timeline_from_json(c.at("timeline"));
        REQUIRE(tl);
        std::map<std::string, MediaSize> sizes;
        for (const auto& [id, wh] : c.at("media").items()) {
            sizes[id] = {wh.at(0).get<double>(), wh.at(1).get<double>()};
        }
        const MediaSizeFn media = [&](const TimelineClip& clip) -> std::optional<MediaSize> {
            const auto it = sizes.find(clip.id);
            return it == sizes.end() ? std::nullopt : std::optional(it->second);
        };
        const std::set<std::string> skip = c.at("js_dur").get<std::set<std::string>>();
        const FrameSpec spec{c.at("out").at(0).get<double>(), c.at("out").at(1).get<double>(), kPreviewLayers};
        const Json& heads = c.at("heads");
        for (std::size_t i = 0; i < heads.size(); ++i) {
            const double head = heads[i].get<double>();
            INFO("head = " << head);
            const auto layers = evaluate(*tl, head, spec, media);
            REQUIRE(layers);
            auto got = from_cpp(*layers);
            auto want = from_js(c.at("frames")[i]);
            const auto skipped = [&](const Drawn& d) { return skip.contains(d.id); };
            st.skipped += static_cast<std::size_t>(std::ranges::count_if(want, skipped));
            std::erase_if(got, skipped);
            std::erase_if(want, skipped);
            REQUIRE(got.size() == want.size());
            for (std::size_t k = 0; k < want.size(); ++k) {
                INFO("dibujo " << k << " de " << want[k].id);
                CHECK(got[k].id == want[k].id);
                const double tol = got[k].rotated ? kTrigTol : 0.0;
                INFO("[src(4), m(6), alpha, src_t] JS " << dump(want[k].nums) << "\n C++ " << dump(got[k].nums));
                CHECK(same(want[k].nums, got[k].nums, tol));
                INFO("cortinilla JS " << dump(want[k].clip) << "\n C++ " << dump(got[k].clip));
                CHECK(same(want[k].clip, got[k].clip, tol));
            }
            st.blits += want.size();
            ++st.heads;
        }
    }
    return st;
}

} // namespace

TEST_CASE("paridad del fotograma: casos sintéticos", "[parity][frame]") {
    const auto path = kFixtures / "frame" / "synthetic.json";
    REQUIRE(std::filesystem::exists(path)); // faltan: ejecuta tools/fixtures/export_fixtures.py
    auto file = read_json_file(path);
    REQUIRE(file);
    const Stats st = check_cases(*file, "synthetic.json");
    CHECK(st.heads > 80);
    CHECK(st.blits > 120);
    CHECK(st.skipped == 0);
}

TEST_CASE("paridad del fotograma: tus timelines", "[parity][frame][local]") {
    const auto path = kFixtures / "local" / "frame" / "real.json";
    if (!std::filesystem::exists(path)) {
        SKIP("Sin fixtures locales: export_fixtures.py --real las genera desde data/projects.json");
    }
    auto file = read_json_file(path);
    REQUIRE(file);
    const Stats st = check_cases(*file, "real.json");
    CHECK(st.heads > 700);
    CHECK(st.blits > 700);
    WARN("dibujos comparados: " << st.blits << "; fuera por clipDur de JS: " << st.skipped);
}
