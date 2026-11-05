// Los proxies de la vista previa (gc/export/proxy.hpp): cuándo se hacen, que la caché los reconoce y
// caducan con el archivo, que pararlos no deja nada, que su fotograma k es el del original y que la
// vista previa con proxy coloca los clips igual que sin él.

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "gc/core/io.hpp"
#include "gc/core/serialize.hpp"
#include "gc/export/frame.hpp"
#include "gc/export/project.hpp"
#include "gc/export/proxy.hpp"
#include "gc/media/decode.hpp"
#include "gc/media/encode.hpp"
#include "gc/media/probe.hpp"

using gc::Json;
using gc::exporter::ProxyState;
namespace fs = std::filesystem;

namespace {

// Un vídeo de w × h a 30 fps: el fotograma k, gris 20 + 10·k con un cuadrado blanco que avanza.
// Un fotograma de más o de menos se nota en el gris.
void make_video(const fs::path& path, int w, int h, int frames) {
    gc::media::EncodeSettings s;
    s.width = w;
    s.height = h;
    s.fps = 30;
    s.crf = 18;
    s.audio = false;
    auto writer = gc::media::Mp4Writer::open(path, s);
    REQUIRE(writer);
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(w) * h * 4);
    for (int k = 0; k < frames; ++k) {
        const auto g = static_cast<std::uint8_t>(20 + (10 * k));
        const int box = h / 4;
        const int bx = k * (w - box) / std::max(1, frames - 1);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const bool white = x >= bx && x < bx + box && y >= h / 3 && y < h / 3 + box;
                std::uint8_t* p = &rgba[((static_cast<std::size_t>(y) * w) + x) * 4];
                p[0] = p[1] = p[2] = white ? 255 : g;
                p[3] = 255;
            }
        }
        REQUIRE(writer->video(rgba));
    }
    REQUIRE(writer->finish());
}

double mean_luma(const std::vector<std::uint8_t>& rgba) {
    double sum = 0.0;
    for (std::size_t i = 0; i < rgba.size(); i += 4) {
        sum += (0.2126 * rgba[i]) + (0.7152 * rgba[i + 1]) + (0.0722 * rgba[i + 2]);
    }
    return 4.0 * sum / static_cast<double>(rgba.size());
}

double psnr(const gc::render::Image& a, const gc::render::Image& b) {
    REQUIRE(a.pixels.size() == b.pixels.size());
    double se = 0.0;
    for (std::size_t i = 0; i < a.pixels.size(); ++i) {
        const double d = static_cast<double>(a.pixels[i]) - b.pixels[i];
        se += d * d;
    }
    const double mse = se / static_cast<double>(a.pixels.size());
    return mse == 0.0 ? std::numeric_limits<double>::infinity() : 10.0 * std::log10(255.0 * 255.0 / mse);
}

struct TempDir {
    fs::path path;
    explicit TempDir(const std::string& name) : path(fs::temp_directory_path() / name) {
        fs::remove_all(path);
        fs::create_directories(path);
    }
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    TempDir(TempDir&&) = delete;
    TempDir& operator=(TempDir&&) = delete;
    ~TempDir() {
        try { // aun con error_code, el remove_all de MSVC puede lanzar bad_alloc al copiar rutas
            fs::remove_all(path);
        } catch (...) { // NOLINT(bugprone-empty-catch): la carpeta temporal se queda, el destructor no lanza
        }
    }
};

gc::core::TimelineClip video_clip(double in, double out) {
    gc::core::TimelineClip c;
    c.id = "v";
    c.track_id = "V1";
    c.kind = "video";
    c.asset_kind = "clips";
    c.filename = "big.mp4";
    c.in_point = in;
    c.out_point = out;
    c.source_duration = 20.0 / 30.0;
    return c;
}

} // namespace

TEST_CASE("proxies: solo de los vídeos grandes, con el lado corto a 960", "[export][proxy]") {
    using gc::exporter::proxy_size;
    CHECK(proxy_size(3840, 2160) == std::pair{1706, 960});
    CHECK(proxy_size(2160, 3840) == std::pair{960, 1706}); // vertical
    CHECK(proxy_size(2560, 1440) == std::pair{1706, 960});
    CHECK(proxy_size(1440, 1440) == std::pair{960, 960});
    CHECK_FALSE(proxy_size(1920, 1080)); // un 1080p se decodifica sin problema
    CHECK_FALSE(proxy_size(1200, 2000)); // el lado corto, justo en el límite
    CHECK_FALSE(proxy_size(0, 2160));
}

TEST_CASE("proxies: se hacen, la caché los reconoce y caducan con el archivo", "[export][proxy]") {
    gc::media::silence_ffmpeg_log();
    const TempDir dir("gc_test_proxy");
    const fs::path cache = dir.path / "cache";
    const fs::path big = dir.path / "big.mp4";
    make_video(big, 2560, 1440, 20);

    CHECK(gc::exporter::find_proxy(cache, big, 30).state == ProxyState::Missing);
    double last = -1.0;
    auto made = gc::exporter::make_proxy(cache, big, 30, [&](double part) {
        CHECK(part >= last); // avanza
        last = part;
        return true;
    });
    INFO((made ? std::string() : made.error().to_string()));
    REQUIRE(made);
    REQUIRE(made->state == ProxyState::Ready);
    CHECK(made->file.source_width == 2560);
    CHECK(made->file.source_height == 1440);
    CHECK(fs::is_regular_file(made->file.path));
    CHECK(last > 0.9);
    const auto found = gc::exporter::find_proxy(cache, big, 30);
    CHECK(found.state == ProxyState::Ready);
    CHECK(found.file.path == made->file.path);
    CHECK(found.file.source_width == 2560);
    // Otro fps, otro proxy.
    CHECK(gc::exporter::find_proxy(cache, big, 25).state == ProxyState::Missing);

    // El proxy: los mismos fotogramas, más pequeños.
    gc::media::DecodeSpec spec;
    spec.input_args = {"-i", gc::core::path_to_utf8(made->file.path)};
    int frames = 0;
    auto decoded = gc::media::decode_frames(spec, [&](const gc::media::RgbaFrame& f) {
        CHECK(f.width == 1706);
        CHECK(f.height == 960);
        ++frames;
        return true;
    });
    REQUIRE(decoded);
    CHECK(frames == 20);

    // Uno pequeño no necesita proxy, y la caché lo recuerda.
    const fs::path small = dir.path / "small.mp4";
    make_video(small, 640, 360, 5);
    auto none = gc::exporter::make_proxy(cache, small, 30);
    REQUIRE(none);
    CHECK(none->state == ProxyState::NotNeeded);
    CHECK(gc::exporter::find_proxy(cache, small, 30).state == ProxyState::NotNeeded);

    // Pararlo no deja nada a medias.
    const auto before = std::distance(fs::directory_iterator(cache), fs::directory_iterator());
    auto stopped = gc::exporter::make_proxy(cache, big, 24, [](double) { return false; });
    REQUIRE(stopped);
    CHECK(stopped->state == ProxyState::Missing);
    CHECK(std::distance(fs::directory_iterator(cache), fs::directory_iterator()) == before);

    // Con el archivo cambiado, el de antes ya no vale.
    make_video(big, 2560, 1440, 21);
    CHECK(gc::exporter::find_proxy(cache, big, 30).state == ProxyState::Missing);
}

TEST_CASE("proxies: el fotograma k del proxy es el fotograma k del original", "[export][proxy]") {
    gc::media::silence_ffmpeg_log();
    const TempDir dir("gc_test_proxy_frames");
    const fs::path big = dir.path / "big.mp4";
    make_video(big, 2560, 1440, 20);
    auto made = gc::exporter::make_proxy(dir.path / "cache", big, 30);
    REQUIRE(made);
    REQUIRE(made->state == ProxyState::Ready);
    // Un clip que empieza dentro del vídeo, como los lee el export: el gris dice qué fotograma es.
    const auto clip = video_clip(0.1, 0.6);
    for (std::int64_t k = 0; k < 15; ++k) {
        INFO("tick " << k);
        auto a = gc::media::export_frame_at(clip, big, 30, k);
        auto b = gc::media::export_frame_at(clip, made->file.path, 30, k);
        REQUIRE(a);
        REQUIRE(b);
        REQUIRE(a->has_value());
        REQUIRE(b->has_value());
        // Un fotograma de más o de menos: ±10 de gris. Lo que separa al proxy (H.264 reducido) del
        // original no llega a la mitad.
        CHECK(std::abs(mean_luma((*a)->pixels) - mean_luma((*b)->pixels)) < 5.0);
    }
}

TEST_CASE("proxies: la vista previa con proxy coloca los clips igual que sin él", "[export][proxy]") {
    gc::media::silence_ffmpeg_log();
    const TempDir dir("gc_test_proxy_render");
    const fs::path big = dir.path / "big.mp4";
    make_video(big, 2560, 1440, 20);
    auto made = gc::exporter::make_proxy(dir.path / "cache", big, 30);
    REQUIRE(made);
    REQUIRE(made->state == ProxyState::Ready);

    // El vídeo horizontal encuadrado en un vertical (un recorte de verdad del original) y, encima,
    // el mismo como superpuesto, pequeño y desplazado.
    const Json clip = {{"track_id", "V1"},  {"kind", "video"},      {"asset_kind", "clips"},
                       {"asset_id", "big"}, {"name", "big"},        {"asset_scope", "collection"},
                       {"filename", "big.mp4"},                     {"start", 0.0},
                       {"in_point", 0.0},   {"out_point", 0.6},     {"source_duration", 20.0 / 30.0}};
    Json fill = clip;
    fill["id"] = "fill";
    Json over = clip;
    over["id"] = "over";
    over["track_id"] = "V2";
    over["in_point"] = 0.1;
    over["layout"] = "overlay";
    over["transform"] = {{"x", 0.62}, {"y", 0.3}, {"scale", 0.15}};
    const Json timeline = {{"version", 2},
                           {"fps", 30},
                           {"width", 720},
                           {"height", 1280},
                           {"tracks", Json::array({{{"id", "V1"}, {"kind", "video"}, {"name", "V1"}},
                                                   {{"id", "V2"}, {"kind", "video"}, {"name", "V2"}}})},
                           {"clips", Json::array({fill, over})}};
    auto project = gc::core::project_from_json(
        {{"id", "proxy"}, {"name", "proxy"}, {"created_at", "2026-11-05T00:00:00"}, {"timeline", timeline}});
    INFO((project ? std::string() : project.error().to_string()));
    REQUIRE(project);
    REQUIRE(project->timeline);
    gc::exporter::LoadedProject lp;
    lp.root = dir.path;
    lp.dirs = gc::core::MediaDirs{dir.path / "clips", dir.path / "data", dir.path}; // colecciones en la carpeta
    lp.settings = Json::object();
    lp.project = *project;
    lp.timeline = *project->timeline;

    // La clave es el material como lo resuelve el export (la de ProxyMaker).
    const auto material = gc::exporter::clip_path(lp, *lp.timeline.clips.front());
    REQUIRE(material);
    gc::exporter::FrameOptions plain;
    plain.scale = 0.5;
    gc::exporter::FrameOptions proxied = plain;
    proxied.proxies[*material] = made->file;
    auto a = gc::exporter::FrameRenderer::open(lp, plain);
    auto b = gc::exporter::FrameRenderer::open(lp, proxied);
    REQUIRE(a);
    REQUIRE(b);
    CHECK(a->proxied() == 0);
    CHECK(b->proxied() == 2);
    for (const std::int64_t k : {0, 5, 11, 17}) {
        INFO("tick " << k);
        auto fa = a->tick(k);
        auto fb = b->tick(k);
        REQUIRE(fa);
        REQUIRE(fb);
        REQUIRE(fa->image.width == fb->image.width);
        const double p = psnr(fa->image, fb->image);
        UNSCOPED_INFO("PSNR con y sin proxy: " << p << " dB");
        CHECK(p >= 30.0); // en otro sitio, o con un fotograma de diferencia, bajaría mucho
    }
}
