// Dónde está el material de un clip (compose._clip_path): clip_media.hpp.

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>

#include "gc/core/clip_media.hpp"

using namespace gc::core;

namespace {

TimelineClip clip(std::string kind, std::string asset_kind, std::string filename, std::string scope = "project") {
    TimelineClip c;
    c.id = "c1";
    c.kind = std::move(kind);
    c.asset_kind = std::move(asset_kind);
    c.filename = std::move(filename);
    c.asset_scope = std::move(scope);
    return c;
}

} // namespace

TEST_CASE("clip_media_path: la carpeta del proyecto, la biblioteca o una colección", "[clip_media]") {
    namespace fs = std::filesystem;
    const fs::path tmp = fs::temp_directory_path() / "gc_clip_media";
    fs::create_directories(tmp / "clips" / "p1" / "video");
    fs::create_directories(tmp / "data" / "library" / "image");
    fs::create_directories(tmp / "coleccion" / "fondos");
    { std::ofstream(tmp / "coleccion" / "fondos" / "mar.png") << "x"; }
    Project p;
    p.id = "p1";
    const MediaDirs dirs{tmp / "clips", tmp / "data", tmp / "coleccion"};

    const auto video = clip_media_path(p, clip("video", "clips", "a.mp4"), dirs);
    REQUIRE(video);
    CHECK(fs::weakly_canonical(*video) == fs::weakly_canonical(tmp / "clips" / "p1" / "video" / "a.mp4"));
    // Sin asset_scope (vacío) también es el proyecto: `getattr(…) or "project"`.
    CHECK(clip_media_path(p, clip("audio", "audios", "b.wav", ""), dirs).has_value());

    const auto lib = clip_media_path(p, clip("image", "images", "logo.png", "library"), dirs);
    REQUIRE(lib);
    CHECK(fs::weakly_canonical(*lib) == fs::weakly_canonical(tmp / "data" / "library" / "image" / "logo.png"));

    const auto col = clip_media_path(p, clip("image", "images", "fondos/mar.png", "collection"), dirs);
    REQUIRE(col);
    CHECK(fs::weakly_canonical(*col) == fs::weakly_canonical(tmp / "coleccion" / "fondos" / "mar.png"));
    // Sin carpeta de colecciones, no hay ruta.
    CHECK_FALSE(clip_media_path(p, clip("image", "images", "fondos/mar.png", "collection"),
                                MediaDirs{tmp / "clips", tmp / "data", std::nullopt}));

    // Un still sin asset_kind de disco va a image/; un vídeo sin él, a ninguna parte.
    const auto still = clip_media_path(p, clip("image", "text", "x.png"), dirs);
    REQUIRE(still);
    CHECK(still->parent_path().filename() == "image");
    CHECK_FALSE(clip_media_path(p, clip("video", "text", "x.mp4"), dirs));

    // Sin portar: figuras, motion y SFX. Y nada que salga de la carpeta del proyecto.
    CHECK_FALSE(clip_media_path(p, clip("shape", "shape", "s.png"), dirs));
    CHECK_FALSE(clip_media_path(p, clip("motion", "motion", "m.webm"), dirs));
    CHECK_FALSE(clip_media_path(p, clip("audio", "sfx", "boom.wav"), dirs));
    CHECK_FALSE(clip_media_path(p, clip("video", "clips", "../../fuera.mp4"), dirs));

    std::error_code ec;
    fs::remove_all(tmp, ec);
}
