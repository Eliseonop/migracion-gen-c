// Paridad de las CALIDADES de un vídeo de YouTube con video-yt (la parte sin red de youtube_video.py).
//
// Fixture: tests/fixtures/youtube/video.json (export_fixtures.py --solo youtube_video, casos de
// youtube_video_cases.py): quality_of de cada formato, available_qualities, resolve_quality y
// format_spec de cada `info`, y _mb. {"error": …} = Python lanza.

#include <catch2/catch_test_macros.hpp>

#include <optional>

#include "gc/core/io.hpp"
#include "gc/core/youtube_video.hpp"

using gc::Json;
using namespace gc::core;

namespace {

template <class T>
void check_result(const gc::Result<T>& got, const Json& want) {
    if (want.contains("error")) {
        INFO("Python: " << want.dump());
        CHECK_FALSE(got);
        return;
    }
    INFO((got ? std::string() : got.error().to_string()));
    REQUIRE(got);
    if constexpr (std::is_same_v<T, std::optional<std::int64_t>>) {
        CHECK((*got ? Json(**got) : Json()) == want["ok"]);
    } else {
        CHECK(Json(*got) == want["ok"]);
    }
}

} // namespace

TEST_CASE("paridad de las calidades de un vídeo de YouTube", "[parity][youtube]") {
    const auto fixture = read_json_file(path_from_utf8(GC_FIXTURES_DIR) / "youtube" / "video.json");
    REQUIRE(fixture); // faltan: export_fixtures.py --solo youtube_video
    for (const Json& c : fixture->at("quality")) {
        INFO("quality_of(" << c[0].dump() << ")");
        check_result(youtube_format_quality(c[0]), c[1]);
    }
    for (const Json& c : fixture->at("infos")) {
        INFO("info: " << c["info"].dump().substr(0, 300));
        check_result(youtube_available_qualities(c["info"]), c["qualities"]);
        if (c.contains("resolve")) {
            for (const Json& r : c["resolve"]) {
                const auto wanted = r[0].is_null() ? std::nullopt : std::optional(r[0].get<std::int64_t>());
                const auto got = youtube_resolve_quality(c["qualities"]["ok"], wanted);
                INFO("resolve_quality(" << r[0] << ")");
                CHECK((got ? Json(*got) : Json()) == r[1]);
            }
        }
        for (const Json& s : c["spec"]) {
            INFO("format_spec(" << s[0] << ")");
            const auto got = youtube_format_spec(c["info"], s[0].get<std::int64_t>());
            check_result(got, s[1]);
            if (!got && s[1].contains("message") && s[1]["error"] == "ValueError" &&
                s[1]["message"].get<std::string>().starts_with("El vídeo")) {
                CHECK(got.error().message == s[1]["message"].get<std::string>());
            }
        }
    }
    for (const Json& c : fixture->at("mb")) {
        CHECK(mb_label(c[0].get<double>()) == c[1].get<std::string>());
    }
}
