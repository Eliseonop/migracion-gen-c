// Paridad del ALMACÉN DE PROYECTOS con video-yt (projects.py, y lo que va encima: segments.py y
// media_upgrade.py).
//
// Fixtures (tools/fixtures/export_fixtures.py):
//   tests/fixtures/projects/synthetic.json   projects_cases.py (en el repo)
//   tests/fixtures/local/projects/real.json  una copia de tu projects.json (solo local)
//
// Se repiten los pasos sobre un projects.json temporal con los mismos ids (SequentialIdSource)
// y la misma hora que el exportador, y se compara lo que devuelve cada uno (o si falla, y con
// qué mensaje si es de los que ve el usuario) y el texto de projects.json después. «{TMP}» es
// la carpeta temporal.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <format>
#include <optional>
#include <random>
#include <string>
#include <utility>
#include <vector>

#include "disk_steps.hpp"
#include "gc/core/edit.hpp"
#include "gc/core/ids.hpp"
#include "gc/core/io.hpp"
#include "gc/core/media_import.hpp"
#include "gc/core/media_upgrade.hpp"
#include "gc/core/projects_store.hpp"
#include "gc/core/segments.hpp"
#include "gc/core/serialize.hpp"
#include "json_diff.hpp"

namespace fs = std::filesystem;
using gc::Json;
using namespace gc::core;
using gc::test::replace_all;
using gc::test::text_of;
using gc::test::write_file;

namespace {

const fs::path kFixtures = path_from_utf8(GC_FIXTURES_DIR);

// _now() fijado del exportador: la n-ésima llamada da 2026-01-01T00:00:00 + n segundos.
struct FixedNow {
    int n = 0;
    std::string operator()() {
        ++n;
        return std::format("2026-01-01T{:02d}:{:02d}:{:02d}+00:00", n / 3600, n / 60 % 60, n % 60);
    }
};

std::optional<std::string_view> opt_text(const Json& v) {
    return v.is_null() ? std::nullopt : std::optional<std::string_view>(v.get_ref<const std::string&>());
}

// Lo que devuelve cada paso, como JSON (model_dump() de lo validado).
Json dump(const Json& v) { return v; }
Json dump(bool v) { return v; }
Json dump(const Project& v) { return to_json(v); }
Json dump(const ProjectGroup& v) { return to_json(v); }
Json dump(const ClipInfo& v) { return to_json(v); }
Json dump(const AudioInfo& v) { return to_json(v); }
Json dump(const ImageInfo& v) { return to_json(v); }

std::string from_hex(std::string_view hex) {
    std::string out;
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
        out.push_back(static_cast<char>(std::stoi(std::string(hex.substr(i, 2)), nullptr, 16)));
    }
    return out;
}

// El paso «tree»: el texto del archivo o, si no es UTF-8 (un PNG), «hex:» y sus bytes.
std::string tree_content(const std::string& bytes) {
    try {
        (void)Json(bytes).dump(); // lanza si no es UTF-8 válido
        return bytes;
    } catch (const Json::exception&) {
        std::string out = "hex:";
        for (const char c : bytes) {
            out += std::format("{:02x}", static_cast<unsigned char>(c));
        }
        return out;
    }
}

// Los argumentos con nombre de import_video e import_image (los textos; lo demás no cuenta).
ImportMeta import_meta(const Json& m) {
    const auto field = [&](const char* key) -> std::optional<std::string> {
        return m.contains(key) && m[key].is_string() ? std::optional(m[key].get<std::string>()) : std::nullopt;
    };
    ImportMeta meta;
    meta.label = field("label");
    meta.description = field("description");
    meta.origin = field("origin");
    meta.source = field("source");
    meta.provider = field("provider");
    meta.external_id = field("external_id");
    meta.source_url = field("source_url");
    meta.author = field("author");
    meta.license_info = field("license_info");
    return meta;
}

template <class T>
Json dump(const std::optional<T>& v) {
    return v ? dump(*v) : Json();
}
template <class T>
Json dump(const std::vector<T>& v) {
    Json out = Json::array();
    for (const auto& x : v) {
        out.push_back(dump(x));
    }
    return out;
}

std::size_t run_steps(const Json& fixture, const std::string& fname) {
    std::random_device rd;
    const fs::path tmp = fs::temp_directory_path() / ("gc_prj_" + std::to_string(rd()));
    fs::create_directories(tmp);
    const std::string posix = replace_all(path_to_utf8(tmp), "\\", "/");
    const fs::path file = tmp / "data" / "projects.json";
    FixedNow clock;
    ProjectsStore store(file, tmp / "clips", [&clock] { return clock(); });
    SequentialIdSource ids;
    // Las rutas de la carpeta temporal vuelven a ser «{TMP}» (como hace el exportador).
    const auto tmp_of = [&](const Json& v) { return gc::test::replace_in_strings(v, posix, "{TMP}"); };

    std::size_t n = 0;
    for (const Json& s : fixture.at("steps")) {
        const Json& step = s.at("step");
        INFO(fname << " :: paso " << n << ": " << step.dump().substr(0, 200));
        const auto kind = step[0].get<std::string>();
        // Los argumentos con «{TMP}» → la carpeta temporal.
        const Json a = gc::test::replace_in_strings(step, "{TMP}", posix);
        const auto text = [&](std::size_t i) { return a[i].get<std::string>(); };
        gc::Result<void> ok;
        Json result;
        const auto take = [&](auto r) {
            if (r) {
                result = dump(*r);
            } else {
                ok = std::unexpected(r.error());
            }
        };
        if (kind == "file") { // el texto de Python (json.dumps con sus separadores) si era un JSON
            const std::string content = s.contains("text") ? s.at("text").get<std::string>() : text_of(step[1]);
            REQUIRE(write_file(file, replace_all(content, "{TMP}", posix)));
        } else if (kind == "remove") {
            std::error_code ec;
            fs::remove(file, ec);
        } else if (kind == "disk") {
            for (const Json& rel : a[1]) {
                REQUIRE(write_file(tmp / path_from_utf8(rel.get<std::string>()), rel.get<std::string>()));
            }
        } else if (kind == "data") {
            result = fs::exists(file) ? Json(gc::test::read_file(file)) : Json();
        } else if (kind == "tree") {
            std::vector<std::pair<std::string, Json>> entries;
            for (const auto& e : fs::recursive_directory_iterator(tmp / "clips")) {
                entries.emplace_back(replace_all(path_to_utf8(fs::relative(e.path(), tmp / "clips")), "\\", "/"),
                                     e.is_regular_file() ? Json(tree_content(gc::test::read_file(e.path()))) : Json());
            }
            std::ranges::sort(entries, {}, &std::pair<std::string, Json>::first);
            result = Json::array();
            for (auto& [rel, content] : entries) {
                result.push_back(Json::array({rel, std::move(content)}));
            }
        } else if (kind == "load") {
            result = store.load();
        } else if (kind == "list") {
            take(store.list_projects());
        } else if (kind == "groups") {
            take(store.list_groups());
        } else if (kind == "get") {
            take(store.get_project(text(1)));
        } else if (kind == "create") {
            take(store.create_project(text(1), opt_text(a[2]), ids));
        } else if (kind == "create_group") {
            take(store.create_group(text(1), ids));
        } else if (kind == "rename_group") {
            take(store.rename_group(text(1), text(2)));
        } else if (kind == "delete_group") {
            take(store.delete_group(text(1)));
        } else if (kind == "move") {
            take(store.move_project(text(1), opt_text(a[2])));
        } else if (kind == "rename") {
            take(store.rename_project(text(1), text(2)));
        } else if (kind == "duplicate") {
            take(store.duplicate_project(text(1), ids));
        } else if (kind == "set_folder") {
            take(store.set_folder(text(1), a[2]));
        } else if (kind == "delete") {
            take(store.delete_project(text(1)));
        } else if (kind == "add_clips") {
            std::vector<ClipInfo> clips;
            for (const Json& c : a[2]) {
                auto clip = clip_info_from_json(c);
                REQUIRE(clip);
                clips.push_back(std::move(*clip));
            }
            ok = store.add_clips(text(1), clips, ids);
        } else if (kind == "add_transcript") {
            auto t = transcript_from_json(a[2]);
            REQUIRE(t);
            ok = store.add_transcript(text(1), *t);
        } else if (kind == "add_audio") {
            auto audio = audio_info_from_json(a[2]);
            REQUIRE(audio);
            ok = store.add_audio(text(1), *audio);
        } else if (kind == "add_image") {
            auto image = image_info_from_json(a[2]);
            REQUIRE(image);
            ok = store.add_image(text(1), *image);
        } else if (kind == "update") {
            take(store.update_material(text(1), text(2), text(3), a[4]));
        } else if (kind == "remove_material") {
            take(store.remove_material(text(1), text(2), text(3)));
        } else if (kind == "manifest") {
            take(store.apply_manifest(text(1), a[2]));
        } else if (kind == "timeline") {
            take(store.save_timeline(text(1), a[2]));
        } else if (kind == "motion") {
            take(store.save_motion_composition(text(1), a[2]));
        } else if (kind == "delete_motion") {
            take(store.delete_motion_composition(text(1), text(2)));
        } else if (kind == "scenes") {
            take(store.save_scene_directions(text(1), a[2]));
        } else if (kind == "blueprint") {
            take(store.save_visual_blueprint(text(1), a[2]));
        } else if (kind == "segments") { // la sonda de duración da a[4] (null: falla)
            const Json& dur = a[4];
            const DurationProbe probe = [&dur](const fs::path&) -> std::optional<double> {
                return dur.is_null() ? std::nullopt : std::optional(dur.get<double>());
            };
            take(create_segments(store, text(1), text(2), a[3], probe, ids));
        } else if (kind == "face_track") { // el detector da a[7] (null: falla)
            const Json& info = a[7];
            const FaceDetector detect = [&info](const fs::path&, double, double, int) -> gc::Result<FaceDetection> {
                if (info.is_null()) {
                    return gc::fail(gc::ErrorCode::Internal, "el detector falla");
                }
                FaceDetection d;
                d.width = info.at("width").get<std::int64_t>();
                d.height = info.at("height").get<std::int64_t>();
                d.fps = info.at("fps").get<double>();
                d.samples = info.at("samples").get<std::int64_t>();
                for (const Json& p : info.at("track")) {
                    auto point = track_point_from_json(p);
                    REQUIRE(point);
                    d.track.push_back(std::move(*point));
                }
                return d;
            };
            const auto opt = [](const Json& v) { return v.is_null() ? std::nullopt : std::optional(v.get<double>()); };
            auto r = face_track_material(store, text(1), text(2), opt(a[3]), opt(a[4]), a[5].get<int>(), a[6].get<bool>(),
                                         detect, [&clock] { return clock(); });
            if (r) {
                result = Json::array({to_json(r->track), r->from_cache});
            } else {
                ok = std::unexpected(r.error());
            }
        } else if (kind == "versions" || kind == "upgrade") { // detect.dims da dims[nombre]
            const Json& dims_map = kind == "versions" ? a[3] : a[4];
            const VideoDimsProbe dims = [&dims_map](const fs::path& p)
                -> std::optional<std::pair<std::int64_t, std::int64_t>> {
                const auto name = path_to_utf8(p.filename());
                if (!dims_map.contains(name)) {
                    return std::nullopt;
                }
                return std::pair{dims_map[name][0].get<std::int64_t>(), dims_map[name][1].get<std::int64_t>()};
            };
            if (kind == "versions") {
                take(find_media_versions(store, text(1), text(2), dims));
            } else {
                // timeline_store.apply_op: la op sobre la timeline guardada, con settle y validación.
                const TimelineOpApplier apply = [&](std::string_view pid, std::string_view op,
                                                    const Json& params) -> gc::Result<Json> {
                    auto proj = store.get_project(pid);
                    REQUIRE(proj);
                    REQUIRE(*proj);
                    REQUIRE((*proj)->timeline);
                    auto cmd = parse_command(op, params);
                    if (!cmd) {
                        return std::unexpected(cmd.error());
                    }
                    auto res = apply_command(*(*proj)->timeline, *cmd, ids);
                    if (!res) {
                        return std::unexpected(res.error());
                    }
                    if (auto saved = store.save_timeline(pid, to_json(res->timeline)); !saved) {
                        return std::unexpected(saved.error());
                    }
                    return Json{{"changed", res->changed}, {"warnings", res->warnings}, {"can_undo", true}};
                };
                std::vector<std::string> olds;
                for (const Json& o : a[3]) {
                    olds.push_back(o.is_string() ? o.get<std::string>() : o.dump()); // str(oid)
                }
                take(replace_media_with(store, text(1), text(2), olds, dims, apply));
            }
        } else if (kind == "yt_sources") {
            take(youtube_upgrade_sources(store, text(1), a[2].is_null() ? std::string() : text(2)));
        } else if (kind == "best_full") {
            const Json& dims_map = a[3];
            const VideoDimsProbe dims = [&dims_map](const fs::path& p)
                -> std::optional<std::pair<std::int64_t, std::int64_t>> {
                const auto name = path_to_utf8(p.filename());
                if (!dims_map.contains(name)) {
                    return std::nullopt;
                }
                return std::pair{dims_map[name][0].get<std::int64_t>(), dims_map[name][1].get<std::int64_t>()};
            };
            auto r = best_full_youtube_version(store, text(1), text(2), dims);
            if (!r) {
                ok = std::unexpected(r.error());
            } else if (*r) {
                result = Json::array({to_json((*r)->clip), (*r)->quality ? Json(*(*r)->quality) : Json()});
            }
        } else if (kind == "import_video" || kind == "import_audio") { // la sonda da a[4] (null: falla, y vale 0)
            const Json& dur = a[4];
            const DurationProbe probe = [&dur](const fs::path&) -> std::optional<double> {
                return dur.is_null() ? std::nullopt : std::optional(dur.get<double>());
            };
            const std::string data = text(3);
            if (kind == "import_video") {
                take(import_video(store, text(1), text(2), data, probe, ids, import_meta(a[5])));
            } else {
                take(import_audio(store, text(1), text(2), data, probe, ids));
            }
        } else if (kind == "import_image") { // OpenCV sustituido como en export_fixtures.py
            const Json& size = a[4];
            ImageCodec codec;
            codec.to_png = [](std::string_view d) -> std::optional<std::string> {
                if (!d.contains("OK")) {
                    return std::nullopt;
                }
                return std::string("\x89PNG\r\n\x1a\n") + "conv:" + std::string(d);
            };
            codec.size = [&size](const fs::path&) -> std::optional<std::pair<std::int64_t, std::int64_t>> {
                if (size.is_null()) {
                    return std::nullopt;
                }
                return std::pair{size[0].get<std::int64_t>(), size[1].get<std::int64_t>()};
            };
            take(import_image(store, text(1), text(2), from_hex(text(3)), codec, ids, import_meta(a[5]),
                              a[6].get<bool>()));
        } else if (kind == "retarget") {
            ok = store.retarget_timeline_asset(text(1), text(2), a[3], a[4], a[5], a[6], a[7].get<bool>());
        } else {
            FAIL("paso desconocido: " << kind);
        }

        if (s.contains("error")) {
            INFO("Python falla con " << s.at("error").get<std::string>());
            REQUIRE_FALSE(ok);
            if (s.contains("message")) {
                CHECK(ok.error().message == s.at("message").get<std::string>());
            }
        } else {
            INFO("error: " << (ok ? std::string() : ok.error().to_string()));
            REQUIRE(ok);
            const auto diff = gc::test::json_diff(gc::test::native_eol(s.at("result")),
                                                  gc::test::native_eol(tmp_of(result)));
            INFO("resultado: " << diff.value_or(""));
            CHECK_FALSE(diff);
        }
        if (s.contains("file")) { // el texto de projects.json después del paso
            const Json written = fs::exists(file) ? Json(replace_all(gc::test::read_file(file), posix, "{TMP}")) : Json();
            CHECK(gc::test::native_eol(written) == gc::test::native_eol(s.at("file")));
        }
        ++n;
    }
    std::error_code ec;
    fs::remove_all(tmp, ec);
    return n;
}

} // namespace

TEST_CASE("paridad del almacén de proyectos: pasos sintéticos", "[parity][projects]") {
    const auto path = kFixtures / "projects" / "synthetic.json";
    REQUIRE(fs::exists(path)); // faltan: ejecuta tools/fixtures/export_fixtures.py
    auto file = read_json_file(path);
    REQUIRE(file);
    CHECK(run_steps(*file, "synthetic.json") > 140);
}

TEST_CASE("paridad del almacén de proyectos: una copia de tu projects.json", "[parity][projects][local]") {
    const auto path = kFixtures / "local" / "projects" / "real.json";
    if (!fs::exists(path)) {
        SKIP("Sin fixtures locales: export_fixtures.py --real las genera desde tu projects.json");
    }
    auto file = read_json_file(path);
    REQUIRE(file);
    CHECK(run_steps(*file, "real.json") >= 5);
}
