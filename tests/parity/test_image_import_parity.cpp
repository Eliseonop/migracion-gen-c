// Paridad del PNG de trabajo de «Importar imagen» (gc/export/image_import.hpp) con
// images.to_working_png de video-yt, que decodifica con OpenCV (cv2.imdecode, IMREAD_UNCHANGED).
//
// Fixture (export_fixtures.py --solo imagenes):
//   tests/fixtures/media/image_import.json   cada caso: el original, el PNG de cv2 y su probe_size,
//                                            o el error.
//
// No se compara el archivo (otro codificador de PNG) sino sus píxeles: exactos en lo que no tiene
// pérdida (BMP, TIFF, WebP sin pérdida); en JPEG y WebP con pérdida, FFmpeg y libjpeg/libwebp
// reconstruyen el croma algo distinto, y se mide por PSNR.

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <limits>
#include <span>
#include <string>

#include "disk_steps.hpp"
#include "gc/core/io.hpp"
#include "gc/export/image_import.hpp"
#include "gc/media/image.hpp"
#include "gc/media/probe.hpp"

using gc::Json;

namespace {

const std::filesystem::path kFixtures = gc::core::path_from_utf8(GC_FIXTURES_DIR);

std::span<const std::uint8_t> bytes_of(const std::string& s) {
    return {reinterpret_cast<const std::uint8_t*>(s.data()), s.size()}; // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast): los mismos bytes
}

// PSNR de los canales de color (de todos los píxeles) y del alfa.
struct Psnr {
    double color = 0;
    double alpha = 0;
};

Psnr psnr(const gc::media::RgbaFrame& a, const gc::media::RgbaFrame& b) {
    double sc = 0.0;
    double sa = 0.0;
    const std::size_t n = a.pixels.size() / 4;
    for (std::size_t k = 0; k < n; ++k) {
        for (std::size_t c = 0; c < 3; ++c) {
            const double d = static_cast<double>(a.pixels[(4 * k) + c]) - b.pixels[(4 * k) + c];
            sc += d * d;
        }
        const double d = static_cast<double>(a.pixels[(4 * k) + 3]) - b.pixels[(4 * k) + 3];
        sa += d * d;
    }
    const auto db = [](double sum, double count) {
        const double mse = sum / count;
        return mse == 0.0 ? std::numeric_limits<double>::infinity() : 10.0 * std::log10(255.0 * 255.0 / mse);
    };
    return {db(sc, 3.0 * static_cast<double>(n)), db(sa, static_cast<double>(n))};
}

} // namespace

TEST_CASE("paridad de «Importar imagen»: el PNG de trabajo de OpenCV", "[parity][import]") {
    const auto path = kFixtures / "media" / "image_import.json";
    REQUIRE(std::filesystem::exists(path)); // faltan: export_fixtures.py --solo imagenes
    auto file = gc::core::read_json_file(path);
    REQUIRE(file);
    gc::media::silence_ffmpeg_log();
    const gc::core::ImageCodec codec = gc::exporter::image_codec();
    const char* dump = std::getenv("GC_IMPORT_DUMP"); // NOLINT(concurrency-mt-unsafe): carpeta donde dejar los PNG del C++
    std::size_t compared = 0;
    for (const Json& c : *file) {
        const auto name = c.at("name").get<std::string>();
        INFO(name);
        const std::string src = gc::test::read_file(kFixtures / gc::core::path_from_utf8(c.at("src").get<std::string>()));
        REQUIRE_FALSE(src.empty());
        const auto png = codec.to_png(src);
        if (c.contains("error")) { // cv2 no la lee (AVIF, cortada, un vídeo…): aquí tampoco
            CHECK_FALSE(png.has_value());
            continue;
        }
        REQUIRE(png.has_value());
        CHECK(png->starts_with("\x89PNG\r\n\x1a\n"));
        if (dump != nullptr) {
            (void)gc::test::write_file(gc::core::path_from_utf8(dump) / (name + ".png"), *png);
        }
        const auto want_path = kFixtures / gc::core::path_from_utf8(c.at("want").get<std::string>());
        const auto got = gc::media::decode_image(bytes_of(*png));
        const auto want = gc::media::decode_image(bytes_of(gc::test::read_file(want_path)));
        REQUIRE(got.has_value());
        REQUIRE(want.has_value());
        REQUIRE(got->width == want->width); // sin girar por el EXIF, como cv2
        REQUIRE(got->height == want->height);
        // probe_size del PNG guardado.
        const auto size = codec.size(want_path);
        REQUIRE(size.has_value());
        CHECK(size->first == c.at("size")[0].get<std::int64_t>());
        CHECK(size->second == c.at("size")[1].get<std::int64_t>());
        const Psnr p = psnr(*got, *want);
        UNSCOPED_INFO(name << ": PSNR de color " << p.color << " dB, de alfa " << p.alpha << " dB");
        if (c.at("lossless").get<bool>()) {
            const bool same = got->pixels == want->pixels;
            CHECK(same);
        } else {
            // Medido: de 52,4 a 53,2 dB en JPEG 4:2:0 y WebP (también con lados impares), 59,2 en
            // JPEG 4:4:4 y 64,2 en gris; el error máximo, 3 niveles. Con el croma de swscale tal
            // cual, 40 dB, y con lados impares, 31.
            CHECK(p.color >= 50.0);
        }
        CHECK(p.alpha == std::numeric_limits<double>::infinity()); // el alfa, exacto
        ++compared;
    }
    CHECK(compared == 17);
}
