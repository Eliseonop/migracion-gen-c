// Grabar un directo (live_capture.hpp): qué enlaces valen, lo que se pide a yt-dlp y lo que se lee de él, las órdenes
// de FFmpeg y su progreso, los tramos de las marcas y el nombre de la grabación; y adopt_video_file, que la mueve al
// proyecto en vez de copiarla.
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "gc/core/ids.hpp"
#include "gc/core/live_capture.hpp"
#include "gc/core/media_import.hpp"
#include "gc/core/projects_store.hpp"

using namespace gc::core;
using gc::Json;
namespace fs = std::filesystem;

namespace {

std::string url_of(std::string_view text) {
    const auto link = live_link(text);
    return link ? link->url : std::string("-");
}

std::ptrdiff_t at(const std::vector<std::string>& args, std::string_view arg) {
    return std::ranges::find(args, arg) - args.begin();
}

} // namespace

TEST_CASE("directo: qué enlaces valen", "[live]") {
    // TikTok: el /live de un usuario, su perfil (pasa a su /live) y el enlace de compartir del móvil.
    CHECK(url_of("https://www.tiktok.com/@pilar.magenta/live") == "https://www.tiktok.com/@pilar.magenta/live");
    CHECK(url_of("tiktok.com/@pilar_magenta") == "https://www.tiktok.com/@pilar_magenta/live");
    CHECK(url_of("  HTTPS://WWW.TikTok.com/@user/live?lang=es#x  ") == "https://www.tiktok.com/@user/live");
    CHECK(url_of("https://m.tiktok.com/share/live/7210809319192726273") ==
          "https://m.tiktok.com/share/live/7210809319192726273");
    CHECK(live_link("https://www.tiktok.com/@user/live")->platform == LivePlatform::TikTok);
    // Un vídeo de TikTok no es un directo; tampoco otra web u otro esquema.
    CHECK(url_of("https://www.tiktok.com/@user/video/7210809319192726273") == "-");
    CHECK(url_of("https://www.tiktok.com/") == "-");
    CHECK(url_of("https://www.tiktok.com/@/live") == "-");
    CHECK(url_of("https://m.tiktok.com/share/live/abc") == "-");
    CHECK(url_of("https://vimeo.com/123") == "-");
    CHECK(url_of("ftp://www.tiktok.com/@user/live") == "-");
    CHECK(url_of("   ") == "-");
    // YouTube: cualquier enlace suyo (yt-dlp dice luego si está en directo).
    CHECK(url_of("https://www.youtube.com/live/abc123") == "https://www.youtube.com/live/abc123");
    CHECK(url_of("youtu.be/abc123") == "https://youtu.be/abc123");
    CHECK(live_link("https://www.youtube.com/@canal/live")->platform == LivePlatform::YouTube);
    CHECK(live_platform_name(LivePlatform::TikTok) == "TikTok");
    CHECK(live_platform_name(LivePlatform::YouTube) == "YouTube");
}

TEST_CASE("directo: lo que se pide a yt-dlp y lo que se lee de su JSON", "[live]") {
    const LiveLink link{LivePlatform::TikTok, "https://www.tiktok.com/@user/live"};
    CHECK(live_resolve_args(link) ==
          std::vector<std::string>{"-J", "--no-warnings", "--no-playlist", "-f", "best", link.url});

    // El formato elegido va arriba del todo del JSON: su URL y sus cabeceras.
    Json info = {{"id", "7210809319192726273"},
                 {"title", "Directo"},
                 {"uploader", "user"},
                 {"is_live", true},
                 {"live_status", "is_live"},
                 {"url", "https://pull.example/stream.flv"},
                 {"http_headers", {{"User-Agent", "Mozilla/5.0"}, {"Referer", "https://www.tiktok.com/"}}}};
    auto stream = live_stream_from_info(info);
    REQUIRE(stream);
    CHECK(stream->url == "https://pull.example/stream.flv");
    CHECK(stream->headers.find("User-Agent: Mozilla/5.0\r\n") != std::string::npos);
    CHECK(stream->headers.find("Referer: https://www.tiktok.com/\r\n") != std::string::npos);
    CHECK(stream->headers.size() == std::string("User-Agent: Mozilla/5.0\r\nReferer: https://www.tiktok.com/\r\n").size());
    CHECK(stream->id == "7210809319192726273");
    CHECK(stream->title == "Directo");
    CHECK(stream->uploader == "user");
    // Sin autor: el canal. Sin live_status: vale is_live.
    info.erase("uploader");
    info["channel"] = "Canal";
    info.erase("live_status");
    stream = live_stream_from_info(info);
    REQUIRE(stream);
    CHECK(stream->uploader == "Canal");

    // Lo que no es un directo en marcha, con lo que ve el usuario.
    const auto error_of = [&](const char* status, bool is_live) {
        Json copy = info;
        copy["live_status"] = status;
        copy["is_live"] = is_live;
        const auto r = live_stream_from_info(copy);
        return r ? std::string("ok") : r.error().message;
    };
    CHECK(error_of("was_live", false) == "Ese directo ya acabó. Si quedó publicado, bájalo entero en «Cargar clips».");
    CHECK(error_of("post_live", false).starts_with("Ese directo ya acabó."));
    CHECK(error_of("is_upcoming", false) == "Ese directo aún no ha empezado.");
    CHECK(error_of("not_live", false) == "Ese enlace no es un directo en marcha.");
    info.erase("url");
    REQUIRE_FALSE(live_stream_from_info(info));
    CHECK(live_stream_from_info(info).error().message == "yt-dlp no dio la dirección del directo.");
    CHECK_FALSE(live_stream_from_info(Json::array()));
}

TEST_CASE("directo: los fallos de yt-dlp en palabras del usuario", "[live]") {
    CHECK(live_resolve_error("ERROR: [tiktok:live] user: The channel is not currently live\n") ==
          "No está en directo ahora mismo (o el directo ya acabó).");
    CHECK(live_resolve_error("ERROR: [youtube] abc: This live event will begin in 3 hours.") ==
          "Ese directo aún no ha empezado.");
    CHECK(live_resolve_error("ERROR: [youtube] abc: Private video. Sign in if you've been granted access") ==
          "Ese directo es privado o pide iniciar sesión.");
    CHECK(live_resolve_error("ERROR: Unsupported URL: https://example.com") == "yt-dlp no reconoce ese enlace.");
    CHECK(live_resolve_error("WARNING: algo\nERROR: [generic] x: Unable to download webpage: timed out\n") ==
          "yt-dlp: Unable to download webpage: timed out");
    CHECK(live_resolve_error("") == "yt-dlp no pudo abrir el directo.");
}

TEST_CASE("directo: las órdenes de FFmpeg", "[live]") {
    LiveStream stream{"https://pull.example/s.flv", "User-Agent: UA\r\n", "1", "Directo", "user"};
    // Graba sin recomprimir en MPEG-TS, con el progreso por stdout y las cabeceras antes de la entrada.
    auto args = live_record_args(stream, "rec.ts");
    CHECK(args.back() == "rec.ts");
    CHECK(args[static_cast<std::size_t>(at(args, "-progress") + 1)] == "pipe:1");
    CHECK(args[static_cast<std::size_t>(at(args, "-headers") + 1)] == "User-Agent: UA\r\n");
    CHECK(at(args, "-headers") < at(args, "-i"));
    CHECK(args[static_cast<std::size_t>(at(args, "-i") + 1)] == stream.url);
    CHECK(args[static_cast<std::size_t>(at(args, "-c") + 1)] == "copy");
    CHECK(args[static_cast<std::size_t>(at(args, "-f") + 1)] == "mpegts");
    CHECK(at(args, "-nostdin") == std::ssize(args)); // la «q» de parar llega por stdin
    stream.headers.clear();
    args = live_record_args(stream, "rec.ts");
    CHECK(at(args, "-headers") == std::ssize(args));

    // El TS a MP4, sin recomprimir y con el índice delante.
    const auto remux = live_remux_args("rec.ts", "rec.mp4");
    CHECK(remux[static_cast<std::size_t>(at(remux, "-i") + 1)] == "rec.ts");
    CHECK(remux[static_cast<std::size_t>(at(remux, "-c") + 1)] == "copy");
    CHECK(remux[static_cast<std::size_t>(at(remux, "-movflags") + 1)] == "+faststart");
    CHECK(remux.back() == "rec.mp4");
}

TEST_CASE("directo: el progreso de FFmpeg", "[live]") {
    LiveProgress p;
    for (const char* line : {"frame=10", "out_time_us=12500000", "total_size=2048", "progress=continue"}) {
        live_apply_progress(p, line);
    }
    CHECK(p.seconds == 12.5);
    CHECK(p.bytes == 2048);
    CHECK_FALSE(p.ended);
    live_apply_progress(p, "out_time_us=N/A"); // al empezar, FFmpeg aún no sabe
    live_apply_progress(p, "total_size=-5");
    live_apply_progress(p, "basura");
    CHECK(p.seconds == 12.5);
    CHECK(p.bytes == 2048);
    live_apply_progress(p, "progress=end\r");
    CHECK(p.ended);
}

TEST_CASE("directo: los tramos de las marcas", "[live]") {
    using Ranges = std::vector<std::pair<double, double>>;
    // 30 s antes y 10 después, de menor a mayor; las de 100 y 110 se pisan y van juntas; la de 500 cae fuera de la
    // grabación (300 s) y no deja nada.
    const std::vector<double> marks{100.0, 20.0, 110.0, 500.0};
    CHECK(live_mark_ranges(marks, 300.0, 30.0, 10.0) == Ranges{{0.0, 30.0}, {70.0, 120.0}});
    // Al final de la grabación, el tramo se corta ahí.
    CHECK(live_mark_ranges(std::vector<double>{295.0}, 300.0, 30.0, 10.0) == Ranges{{265.0, 300.0}});
    // Sin duración, sin tope; redondeado a ms.
    CHECK(live_mark_ranges(std::vector<double>{5.12345}, 0.0, 2.0, 1.0) == Ranges{{3.123, 6.123}});
    // Las que se tocan, juntas.
    CHECK(live_mark_ranges(std::vector<double>{10.0, 20.0}, 0.0, 5.0, 5.0) == Ranges{{5.0, 25.0}});
    // Lo que queda por debajo de kMinSegment no sale; un NaN tampoco.
    CHECK(live_mark_ranges(std::vector<double>{std::nan(""), 299.9}, 300.0, 0.1, 0.0).empty());
    CHECK(live_mark_ranges(std::vector<double>{}, 300.0, 30.0, 10.0).empty());
}

TEST_CASE("directo: el nombre de la grabación", "[live]") {
    CHECK(live_file_stem("Mi directo: ¿qué pasa? <hoy>", LivePlatform::TikTok, "2026-11-25 18-30") ==
          "Mi directo ¿qué pasa hoy 2026-11-25 18-30");
    CHECK(live_file_stem("   ", LivePlatform::YouTube, "2026-11-25 18-30") == "YouTube 2026-11-25 18-30");
    CHECK(live_file_stem("Fin.", LivePlatform::TikTok, "") == "Fin");
    // 60 bytes como mucho, sin partir una letra.
    CHECK(live_file_stem(std::string(70, 'a'), LivePlatform::TikTok, "") == std::string(60, 'a'));
    std::string accents = "a";
    for (int i = 0; i < 40; ++i) {
        accents += "é";
    }
    std::string expected = "a";
    for (int i = 0; i < 29; ++i) {
        expected += "é";
    }
    CHECK(live_file_stem(accents, LivePlatform::TikTok, "") == expected);
}

TEST_CASE("directo: la grabación entra al proyecto moviéndola, sin copiarla", "[live]") {
    std::random_device rd;
    const fs::path tmp = fs::temp_directory_path() / ("gc_live_" + std::to_string(rd()));
    fs::create_directories(tmp);
    {
        ProjectsStore store(tmp / "data" / "projects.json", tmp / "clips");
        SequentialIdSource ids;
        const auto project = store.create_project("Directos", std::nullopt, ids);
        REQUIRE(project);
        const fs::path rec = tmp / "grabacion.mp4";
        std::ofstream(rec, std::ios::binary) << "un vídeo de mentira";
        const DurationProbe probe = [](const fs::path&) -> std::optional<double> { return 42.0; };
        ImportMeta meta;
        meta.label = "Mi directo";
        meta.origin = "live";
        meta.provider = "tiktok";
        meta.source_url = "https://www.tiktok.com/@user/live";
        const auto clip = adopt_video_file(store, project->id, rec, "Mi directo.mp4", probe, ids, meta);
        REQUIRE(clip);
        CHECK_FALSE(fs::exists(rec)); // movido, no copiado
        CHECK(clip->label == "Mi directo");
        CHECK(clip->origin == "live");
        CHECK(clip->provider == "tiktok");
        CHECK(clip->end == 42.0);
        const auto saved = store.get_project(project->id);
        REQUIRE(saved);
        REQUIRE(*saved);
        REQUIRE_FALSE((**saved).clips.empty());
        CHECK((**saved).clips.back().filename == clip->filename);
        CHECK(fs::exists(project_base(store.clips_dir(), **saved) / "video" / clip->filename));

        // Lo que no es un vídeo, o está vacío, no entra y se queda donde estaba.
        const fs::path note = tmp / "nota.txt";
        std::ofstream(note) << "texto";
        CHECK_FALSE(adopt_video_file(store, project->id, note, "nota.txt", probe, ids, meta));
        CHECK(fs::exists(note));
        const fs::path empty = tmp / "vacio.mp4";
        std::ofstream(empty).close();
        CHECK_FALSE(adopt_video_file(store, project->id, empty, "vacio.mp4", probe, ids, meta));
        CHECK(fs::exists(empty));
    }
    std::error_code ec;
    fs::remove_all(tmp, ec);
}
