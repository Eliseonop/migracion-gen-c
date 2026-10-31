// gc::media::ClipFrames: los fotogramas de un clip en orden de ticks, sin volver a decodificar
// desde el principio, tienen que ser los mismos que export_frame_at (que ya se compara con el
// export en la paridad del render), y los del tramo que se hornea (open_bake_input), los de
// bake_input_frame_at. Solo con FFmpeg (gc_media).

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "gc/core/bg_matte.hpp"
#include "gc/core/io.hpp"
#include "gc/core/json.hpp"
#include "gc/core/model.hpp"
#include "gc/media/decode.hpp"
#include "gc/media/probe.hpp"

using gc::core::TimelineClip;

namespace {

const std::filesystem::path kFiles = gc::core::path_from_utf8(GC_FIXTURES_DIR) / "render" / "files";

TimelineClip clip(const std::string& file, double start, double in, double out, double speed = 1.0,
                  bool image = false, bool reverse = false) {
    TimelineClip c;
    c.id = "c";
    c.kind = image ? "image" : "video";
    c.asset_kind = image ? "images" : "clips";
    c.filename = file;
    c.start = start;
    c.in_point = in;
    c.out_point = out;
    c.speed = speed;
    c.reverse = reverse;
    c.source_duration = image ? 0.0 : gc::media::probe_media(kFiles / file).duration;
    return c;
}

std::string md5_of(const gc::media::RgbaFrame* f) {
    return f == nullptr ? std::string("nada") : gc::media::md5_hex(f->pixels);
}

} // namespace

TEST_CASE("ClipFrames da los mismos fotogramas que export_frame_at", "[media][decode]") {
    gc::media::silence_ffmpeg_log();
    const int fps = 30;
    const std::vector<std::pair<std::string, TimelineClip>> cases{
        {"vídeo de 30 en 30", clip("moving30.mp4", 0.0, 0.0, 1.2)},
        {"vídeo de 25 en 30, empieza en 0,29", clip("moving25.mp4", 0.29, 0.1, 1.4)},
        {"velocidad 1,5", clip("moving30.mp4", 0.0, 0.0, 1.8, 1.5)},
        {"velocidad 0,75", clip("moving30.mp4", 0.1, 0.0, 1.0, 0.75)},
        {"velocidad 2,5", clip("moving30.mp4", 0.0, 0.0, 1.9, 2.5)},
        {"gif de 10 fps", clip("anim10.gif", 0.0, 0.0, 1.5, 1.0, true)},
        {"imagen", clip("waves.png", 0.2, 0.0, 1.0, 1.0, true)},
        // Con el salto previo a un fotograma clave (in_point a más de 1 s del principio): los
        // mismos fotogramas que decodificando desde 0, como export_frame_at.
        {"salto previo: H.264 con B-frames", clip("keys30.mp4", 0.0, 3.3, 4.6)},
        {"salto previo: GOP abierto a 25 fps en 30", clip("keys25_open.mp4", 0.4, 2.45, 4.0)},
        {"salto previo: VP9 en WebM", clip("keys30.webm", 0.0, 4.1, 5.5)},
        {"salto previo: MKV que empieza en 1,5 s", clip("keys30_offset.mkv", 0.0, 3.0, 4.2)},
        {"salto previo a velocidad 1,5", clip("keys30.mp4", 0.2, 2.2, 4.0, 1.5)},
        {"salto previo hasta el final del archivo", clip("keys30.mp4", 0.0, 4.5, 6.0)},
        // Al revés (el tramo por bloques, no un export_frame_at por tick).
        {"al revés", clip("moving30.mp4", 0.0, 0.0, 1.2, 1.0, false, true)},
        {"al revés, de 25 en 30 y empieza en 0,29", clip("moving25.mp4", 0.29, 0.1, 1.4, 1.0, false, true)},
        {"al revés a velocidad 1,5", clip("moving30.mp4", 0.0, 0.0, 1.8, 1.5, false, true)},
        {"al revés a velocidad 0,75", clip("moving30.mp4", 0.1, 0.0, 1.0, 0.75, false, true)},
        {"al revés con salto previo", clip("keys30.mp4", 0.2, 3.3, 4.6, 1.0, false, true)},
    };
    // Ticks crecientes (con repeticiones) y un salto hacia atrás al final.
    std::vector<std::int64_t> ticks;
    ticks.reserve(56);
    for (std::int64_t k = 0; k < 50; ++k) {
        ticks.push_back(k);
    }
    ticks.insert(ticks.end(), {50, 50, 55, 70, 3, 4});
    for (const auto& [name, c] : cases) {
        INFO(name);
        auto frames = gc::media::ClipFrames::open(c, kFiles / c.filename, fps);
        REQUIRE(frames);
        for (const std::int64_t k : ticks) {
            INFO("tick " << k);
            auto got = frames->at(k);
            REQUIRE(got);
            auto want = gc::media::export_frame_at(c, kFiles / c.filename, fps, k);
            REQUIRE(want);
            CHECK(md5_of(*got) == md5_of(*want ? &**want : nullptr));
        }
    }
}

// «Eliminar fondo» con el matte de la caché (la segunda entrada del grafo): en orden, los mismos
// fotogramas que export_frame_at (comparado con el export en la paridad del render), y con alfa.
TEST_CASE("ClipFrames con el matte de la caché da los mismos fotogramas que export_frame_at", "[media][decode][bg]") {
    gc::media::silence_ffmpeg_log();
    const auto cache = kFiles / "bgcache";
    const auto with_bg = [](TimelineClip c, const std::string& key) {
        c.bg_removal = gc::Json{{"enabled", true},
                                {"mode", "auto"},
                                {"auto",
                                 {{"enabled", true},
                                  {"provider", "u2net"},
                                  {"base_key", key},
                                  {"status", "ready"},
                                  {"mask_fps", 15},
                                  {"mask_height", 180}}}};
        return c;
    };
    const std::vector<std::pair<std::string, TimelineClip>> cases{
        {"vídeo con in_point", with_bg(clip("moving30.mp4", 0.0, 0.4, 1.8), "escena_video")},
        {"vídeo más largo que la caché", with_bg(clip("moving30.mp4", 0.1, 0.0, 2.0), "escena_corto")},
        {"velocidad 1,5", with_bg(clip("moving30.mp4", 0.0, 0.2, 1.7, 1.5), "escena_video")},
        {"GIF que da la vuelta", with_bg(clip("anim10.gif", 0.0, 0.0, 3.5, 1.0, true), "escena_gif")},
        {"imagen", with_bg(clip("waves.png", 0.2, 0.0, 1.0, 1.0, true), "escena_imagen")},
    };
    for (const auto& [name, c] : cases) {
        INFO(name);
        auto spec = gc::core::clip_matte_spec(c, cache);
        REQUIRE(spec);
        REQUIRE(spec->has_value());
        const gc::core::MatteSpec* m = &**spec;
        auto frames = gc::media::ClipFrames::open(c, kFiles / c.filename, 30, m);
        REQUIRE(frames);
        bool transparent = false;
        for (std::int64_t k = 0; k < 110; k += 3) {
            INFO("tick " << k);
            auto got = frames->at(k);
            REQUIRE(got);
            auto want = gc::media::export_frame_at(c, kFiles / c.filename, 30, k, m);
            REQUIRE(want);
            CHECK(md5_of(*got) == md5_of(*want ? &**want : nullptr));
            if (*got != nullptr) {
                for (std::size_t i = 3; i < (*got)->pixels.size(); i += 4) {
                    transparent = transparent || (*got)->pixels[i] < 128;
                }
            }
        }
        CHECK(transparent); // el matte recorta: sin él, el material es opaco
    }
    // Sin ningún fotograma del matte al principio de su tramo, image2 no abre la secuencia (el
    // export falla); sin matte (nullptr), el clip sale opaco, como el export sin la caché.
    gc::core::MatteSpec empty;
    empty.folder = cache / "matte" / "no_existe";
    empty.frames = 3;
    empty.sources = {std::nullopt, std::nullopt, std::nullopt};
    const TimelineClip v = with_bg(clip("moving30.mp4", 0.0, 0.0, 1.0), "no_existe");
    CHECK_FALSE(gc::media::export_frame_at(v, kFiles / v.filename, 30, 0, &empty));
    auto plain = gc::media::export_frame_at(v, kFiles / v.filename, 30, 0);
    REQUIRE(plain);
    REQUIRE(plain->has_value());
    CHECK((*plain)->pixels[3] == 255);
}

// La ruta de `genclip render` con efectos de vídeo: el tramo que hornea fx_bake.prepare_input,
// fotograma a fotograma, tiene que ser el de bake_input_frame_at (el que compara con el export la
// paridad del render), también con velocidad, al revés y saltando a un fotograma clave.
TEST_CASE("ClipFrames::open_bake_input da los mismos fotogramas que bake_input_frame_at", "[media][decode]") {
    gc::media::silence_ffmpeg_log();
    const double whole = gc::media::probe_media(kFiles / "moving30.mp4").duration;
    struct Case {
        std::string name;
        TimelineClip clip;
        int fps;
    };
    const std::vector<Case> cases{
        {"velocidad 2", clip("moving30.mp4", 0.0, 0.0, 1.8, 2.0), 30},
        {"velocidad 0,5", clip("moving30.mp4", 0.0, 0.2, 1.0, 0.5), 30},
        {"velocidad 0,75", clip("moving30.mp4", 0.1, 0.0, 1.0, 0.75), 30},
        {"velocidad 2,5", clip("moving30.mp4", 0.0, 0.0, 1.9, 2.5), 30},
        {"al revés", clip("moving30.mp4", 0.0, 0.2, 1.4, 1.0, false, true), 30},
        {"al revés a 1,5", clip("moving30.mp4", 0.3, 0.0, 1.8, 1.5, false, true), 30},
        {"inicio a 0,215", clip("moving30.mp4", 0.215, 0.1, 1.2), 30},
        {"hasta el final del archivo", clip("moving30.mp4", 0.0, whole - 0.9, whole), 30},
        {"timeline de 25 fps", clip("moving30.mp4", 0.0, 0.0, 1.2), 25},
        {"salto previo: H.264 con B-frames", clip("keys30.mp4", 0.0, 3.3, 4.6), 30},
        {"imagen", clip("waves.png", 0.2, 0.0, 1.0, 1.0, true), 30},
    };
    for (const auto& [name, c, fps] : cases) {
        INFO(name);
        auto frames = gc::media::ClipFrames::open_bake_input(c, kFiles / c.filename, fps);
        REQUIRE(frames);
        // Crecientes (con una repetición), uno pasado el final y un salto hacia atrás.
        const auto n = static_cast<std::int64_t>(std::llround((c.out_point - c.in_point) / c.speed * fps));
        std::vector<std::int64_t> seq;
        seq.reserve(static_cast<std::size_t>(n) + 4);
        for (std::int64_t m = 0; m < n; ++m) {
            seq.push_back(m);
        }
        seq.insert(seq.end(), {n - 1, n + 2, 2, 3});
        for (const std::int64_t m : seq) {
            INFO("fotograma " << m << " de " << n);
            auto got = frames->at(m);
            REQUIRE(got);
            auto want = gc::media::bake_input_frame_at(c, kFiles / c.filename, fps, m);
            REQUIRE(want);
            CHECK(md5_of(*got) == md5_of(*want ? &**want : nullptr));
        }
    }
}

