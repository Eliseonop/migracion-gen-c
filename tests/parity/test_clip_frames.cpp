// gc::media::ClipFrames: los fotogramas de un clip en orden de ticks, sin volver a decodificar
// desde el principio, tienen que ser los mismos que export_frame_at (que ya se compara con el
// export en la paridad del render). Solo con FFmpeg (gc_media).

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "gc/core/io.hpp"
#include "gc/core/model.hpp"
#include "gc/media/decode.hpp"
#include "gc/media/probe.hpp"

using gc::core::TimelineClip;

namespace {

const std::filesystem::path kFiles = gc::core::path_from_utf8(GC_FIXTURES_DIR) / "render" / "files";

TimelineClip clip(const std::string& file, double start, double in, double out, double speed = 1.0,
                  bool image = false) {
    TimelineClip c;
    c.id = "c";
    c.kind = image ? "image" : "video";
    c.asset_kind = image ? "images" : "clips";
    c.filename = file;
    c.start = start;
    c.in_point = in;
    c.out_point = out;
    c.speed = speed;
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
    };
    // Ticks crecientes (con repeticiones) y un salto hacia atrás al final.
    std::vector<std::int64_t> ticks;
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
