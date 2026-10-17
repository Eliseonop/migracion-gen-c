// Paridad del EXPORT entero con video-yt: el MP4 de gc_export (render_project) frente al de
// compose.render (vídeo y audio), con la misma timeline y el mismo material.
//
// Solo con FFmpeg y Skia (gc_export, presets msvc-vcpkg-*). Fixture (export_fixtures.py →
// synthetic_export_fixtures; basta con `--solo export`):
//   tests/fixtures/export/synthetic.json   media_cases.EXPORT: la timeline normalizada por Python
//                                          y el MP4 del export real (eNN.mp4, git-lfs).
//
// Las dos salidas pasan por codificadores con pérdidas distintos (x264 o el de la GPU en el
// export; OpenH264 o la GPU aquí), así que no se comparan bytes: el mismo número de fotogramas,
// la luma de cada uno por PSNR (≥ 35 dB, el umbral del plan) y el audio de los dos AAC, con la
// misma sonoridad en cada bloque de 100 ms (±0,5 dB, el ±0,5 LU del plan). Las escenas usan
// geometría entera y par, así que la divergencia del redondeo a pares del export no entra.
// Sí entra la del borde (notas de F3, decisión de Edu): el export enciende cada clip con
// `between(t, start, end)`, cerrado, y con `eof_action=repeat` lo deja ver en t = fin; el C++ usa
// [inicio, fin). El fotograma que cae justo en el fin de un clip con material no se compara.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <set>
#include <string>
#include <vector>

#include "gc/core/io.hpp"
#include "gc/core/serialize.hpp"
#include "gc/core/timeline_rules.hpp"
#include "gc/export/render.hpp"
#include "gc/media/audio.hpp"
#include "gc/media/decode.hpp"
#include "gc/media/probe.hpp"

using gc::Json;
using namespace gc::core;

namespace {

const std::filesystem::path kFixtures = path_from_utf8(GC_FIXTURES_DIR);

double psnr_luma(const gc::media::RgbaFrame& a, const gc::media::RgbaFrame& b) {
    const auto luma = [](const gc::media::RgbaFrame& f, std::size_t k) {
        return 0.2126 * f.pixels[4 * k] + 0.7152 * f.pixels[4 * k + 1] + 0.0722 * f.pixels[4 * k + 2];
    };
    double sum = 0.0;
    const std::size_t n = a.pixels.size() / 4;
    for (std::size_t i = 0; i < n; ++i) {
        const double d = luma(a, i) - luma(b, i);
        sum += d * d;
    }
    const double mse = sum / static_cast<double>(n);
    return mse == 0.0 ? std::numeric_limits<double>::infinity() : 10.0 * std::log10(255.0 * 255.0 / mse);
}

std::vector<gc::media::RgbaFrame> video_frames(const std::filesystem::path& mp4) {
    std::vector<gc::media::RgbaFrame> frames;
    auto n = gc::media::decode_frames(gc::media::DecodeSpec{{"-i", path_to_utf8(mp4)}, ""},
                                      [&](const gc::media::RgbaFrame& f) {
                                          frames.push_back(f);
                                          return true;
                                      });
    REQUIRE(n);
    return frames;
}

// El audio del MP4 como lo lee el CLI: float, 48 kHz, estéreo entrelazado.
std::vector<float> audio_pcm(const std::filesystem::path& mp4, double total) {
    auto pcm = gc::media::mix_audio({{"-i", path_to_utf8(mp4)}}, {"[0:a]anull[aout]"}, total);
    REQUIRE(pcm);
    return std::move(*pcm);
}

// Nivel (dBFS) de cada bloque de 100 ms: la media de los cuadrados de los dos canales.
std::vector<double> block_db(const std::vector<float>& pcm) {
    constexpr std::size_t kBlock = 4800 * 2;
    std::vector<double> out;
    for (std::size_t i = 0; i + kBlock <= pcm.size(); i += kBlock) {
        double sum = 0.0;
        for (std::size_t k = i; k < i + kBlock; ++k) {
            sum += static_cast<double>(pcm[k]) * pcm[k];
        }
        const double ms = sum / kBlock;
        out.push_back(ms > 0 ? 10.0 * std::log10(ms) : -std::numeric_limits<double>::infinity());
    }
    return out;
}

gc::exporter::LoadedProject fixture_project(const Json& timeline, const std::filesystem::path& root) {
    const Json raw = {{"id", "export-fixture"},
                      {"name", "export"},
                      {"created_at", "2026-10-16T00:00:00"},
                      {"timeline", timeline}};
    auto project = project_from_json(raw);
    REQUIRE(project);
    REQUIRE(project->timeline);
    gc::exporter::LoadedProject lp;
    lp.root = root;
    // El material son clips de colección con la raíz de colecciones en tests/fixtures.
    lp.dirs = MediaDirs{root / "clips", root / "data", kFixtures};
    lp.settings = Json::object();
    lp.project = *project;
    lp.timeline = *project->timeline;
    return lp;
}

// Los fotogramas que caen justo en el fin de un clip con material (la divergencia del borde).
std::set<std::int64_t> end_ticks(const Timeline& timeline, int fps) {
    std::set<std::int64_t> out;
    for (const auto& c : timeline.clips) {
        if (c->kind != "video" && c->kind != "image") {
            continue;
        }
        const double end = std::max(0.0, c->start) + clip_timeline_duration(*c);
        const auto k = static_cast<std::int64_t>(std::llround(end * fps));
        if (std::abs((static_cast<double>(k) / fps) - end) < 1e-9) {
            out.insert(k);
        }
    }
    return out;
}

} // namespace

TEST_CASE("paridad del export: el MP4 entero frente al de compose.render", "[parity][export]") {
    const auto path = kFixtures / "export" / "synthetic.json";
    REQUIRE(std::filesystem::exists(path)); // faltan: export_fixtures.py --solo export
    auto file = read_json_file(path);
    REQUIRE(file);
    gc::media::silence_ffmpeg_log();
    const auto root = std::filesystem::temp_directory_path() / "gc_test_export";
    std::size_t cases = 0;
    for (const Json& c : *file) {
        const auto name = c.at("name").get<std::string>();
        INFO(name);
        const auto lp = fixture_project(c.at("timeline"), root);
        const auto ref = kFixtures / "export" / c.at("file").get<std::string>();
        const auto out = std::filesystem::temp_directory_path() / ("gc_test_export_" + c.at("file").get<std::string>());
        auto stats = gc::exporter::render_project(lp, out);
        INFO((stats ? std::string() : stats.error().to_string()));
        REQUIRE(stats);

        // Lo que dice ffprobe de los dos.
        const auto pr = gc::media::probe_media(ref);
        const auto po = gc::media::probe_media(out);
        CHECK(po.has_audio == pr.has_audio);
        CHECK_FALSE(po.color_untagged);
        CHECK(std::abs(po.duration - pr.duration) <= 1.0 / stats->fps);

        // El vídeo, fotograma a fotograma.
        const auto a = video_frames(ref);
        const auto b = video_frames(out);
        REQUIRE(a.size() == b.size());
        REQUIRE(static_cast<std::int64_t>(b.size()) == stats->frames);
        const auto skipped = end_ticks(lp.timeline, stats->fps);
        double worst = std::numeric_limits<double>::infinity();
        for (std::size_t k = 0; k < a.size(); ++k) {
            INFO("fotograma " << k);
            REQUIRE(a[k].width == b[k].width);
            REQUIRE(a[k].height == b[k].height);
            if (skipped.contains(static_cast<std::int64_t>(k))) {
                continue;
            }
            const double p = psnr_luma(a[k], b[k]);
            worst = std::min(worst, p);
            CHECK(p >= 35.0);
        }
        CHECK(skipped.size() < a.size() / 10); // casi todos se comparan
        UNSCOPED_INFO(name << ": peor fotograma " << worst << " dB (" << skipped.size() << " en el fin de un clip)");

        // El audio: la misma sonoridad en cada bloque de 100 ms con sonido.
        if (pr.has_audio) {
            const auto da = block_db(audio_pcm(ref, pr.duration));
            const auto db = block_db(audio_pcm(out, pr.duration));
            REQUIRE(da.size() == db.size());
            std::size_t loud = 0;
            for (std::size_t k = 0; k < da.size(); ++k) {
                if (da[k] < -50.0 && db[k] < -50.0) {
                    continue; // silencio en los dos
                }
                INFO("bloque de audio " << k << ": " << da[k] << " frente a " << db[k] << " dBFS");
                CHECK(std::abs(da[k] - db[k]) <= 0.5);
                ++loud;
            }
            CHECK(loud > 0);
        }
        std::filesystem::remove(out);
        ++cases;
    }
    CHECK(cases == file->size());
}

TEST_CASE("export: una timeline vacía es un error, como en compose.py", "[export]") {
    const Json empty = {{"version", 2}, {"width", 360}, {"height", 640}, {"fps", 30}, {"clips", Json::array()}};
    const auto lp = fixture_project(empty, std::filesystem::temp_directory_path() / "gc_test_export");
    auto stats = gc::exporter::render_project(lp, std::filesystem::temp_directory_path() / "gc_test_export_vacio.mp4");
    REQUIRE_FALSE(stats);
    CHECK(stats.error().message.find("vacía") != std::string::npos);
}
