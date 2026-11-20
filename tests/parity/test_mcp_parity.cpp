// Paridad del núcleo sin red del servidor MCP (gc/core/mcp_*.hpp) con backend/app/mcp_server de
// video-yt.
//
// Fixtures (export_fixtures.py --solo mcp, con los casos de mcp_cases.py):
//   tests/fixtures/mcp/help.json          help_content.py: TOOL_DOMAINS, HELP, domains() y guide()
//   tests/fixtures/mcp/registry.json      registry.py: niveles de acceso, códigos, MCPError, los ValueError
//                                         sueltos, _is_job_dto y lo que se registra de cada tool
//   tests/fixtures/mcp/dto.json           dto.py: el DTO de cada proyecto, aspect_ratio, analyze_dto y job_dto
//   tests/fixtures/mcp/capabilities.json  capabilities.py: enums, verbos y, con cada entorno, overview,
//                                         describe y runtime_info; project_summary
//   tests/fixtures/mcp/resources.json     resources.py: el catálogo y el texto de cada resource
//   tests/fixtures/mcp/tools_read.json  tools_capabilities, tools_context y tools_read: las tools de lectura
//                                         sobre un data/ temporal, con el resultado o el error de cada llamada
//   tests/fixtures/mcp/tools_scene.json scene_get_blueprint, scene_set_blueprint, scene_direction_status,
//                                      scene_direction_auto_split, scene_direction_update_segment y
//                                      scene_direction_set (tools_scene.py), paso a paso.
//   tests/fixtures/mcp/tools_media.json replace_media_version, delete_media, search_sfx y
//                                        set_clip_ai_description paso a paso, con el proyecto y los archivos
//   tests/fixtures/mcp/tools_edit.json  tools_edit y timeline_store: las tools de edición paso a paso,
//                                         con el resultado o el error y el archivo tras cada una
//   tests/fixtures/mcp/audit.json         audit.py: extract_meta y los pasos sobre un data/ temporal, con
//                                         el reloj, los uuid y los jobs sustituidos, y el archivo tras cada uno

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <thread>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

#include "gc/core/fx_catalog.hpp"
#include "gc/core/history_store.hpp"
#include "gc/core/ids.hpp"
#include "gc/core/io.hpp"
#include "gc/core/mcp_audit.hpp"
#include "gc/core/mcp_capabilities.hpp"
#include "gc/core/mcp_dto.hpp"
#include "gc/core/mcp_help.hpp"
#include "gc/core/mcp_registry.hpp"
#include "gc/core/mcp_resources.hpp"
#include "gc/core/mcp_tools_edit.hpp"
#include "gc/core/mcp_tools_media.hpp"
#include "gc/core/mcp_tools_scene.hpp"
#include "gc/core/mcp_tools_read.hpp"
#include "gc/core/projects_store.hpp"
#include "gc/core/serialize.hpp"

#include "json_diff.hpp"

using gc::Json;
namespace mcp = gc::core::mcp;

namespace {

const std::filesystem::path kFixtures = gc::core::path_from_utf8(GC_FIXTURES_DIR);

Json load(std::string_view name) {
    const auto path = kFixtures / "mcp" / (std::string(name) + ".json");
    REQUIRE(std::filesystem::exists(path)); // faltan: export_fixtures.py --solo mcp
    auto file = gc::core::read_json_file(path);
    REQUIRE(file);
    return std::move(*file);
}

void same(const Json& expected, const Json& actual, const std::string& what) {
    const auto diff = gc::test::json_diff(expected, actual);
    INFO(what);
    INFO(diff.value_or(""));
    CHECK_FALSE(diff);
}

template <std::size_t N>
Json list(const std::array<std::string_view, N>& values) {
    Json out = Json::array();
    for (const std::string_view v : values) {
        out.push_back(v);
    }
    return out;
}

gc::core::Project project(const Json& raw) {
    auto p = gc::core::project_from_json(raw);
    REQUIRE(p);
    return std::move(*p);
}

mcp::RuntimeEnv runtime_env(const Json& env) {
    mcp::RuntimeEnv out;
    out.transcribe_model = env["transcribe_model"].get<std::string>();
    out.tts_engines = env["tts_engines"];
    out.sfx_categories = env["sfx_categories"].get<std::vector<std::string>>();
    out.ffmpeg = env["ffmpeg"].get<bool>();
    out.ffprobe = env["ffprobe"].get<bool>();
    out.data_dir = env["data_dir"].get<std::string>();
    out.gpu = env["gpu"];
    out.ai_provider = env["ai_provider"];
    return out;
}

std::optional<std::string> opt_str(const Json& v) {
    return v.is_string() ? std::optional(v.get<std::string>()) : std::nullopt;
}

std::optional<std::vector<std::string>> opt_keys(const Json& kw) {
    if (!kw.contains("param_keys") || kw["param_keys"].is_null()) {
        return std::nullopt;
    }
    return kw["param_keys"].get<std::vector<std::string>>();
}

std::optional<double> opt_ms(const Json& kw) {
    if (!kw.contains("ms") || kw["ms"].is_null()) {
        return std::nullopt;
    }
    return kw["ms"].get<double>();
}

std::string read_bytes(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    const std::streamoff size = in ? static_cast<std::streamoff>(in.tellg()) : 0;
    std::string text(size > 0 ? static_cast<std::size_t>(size) : 0U, '\0');
    in.seekg(0);
    in.read(text.data(), size);
    return text;
}

void write_bytes(const std::filesystem::path& path, std::string_view bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::string from_hex(std::string_view hex) {
    std::string out;
    for (std::size_t i = 0; i + 1 < hex.size(); i += 2) {
        out.push_back(static_cast<char>(std::stoi(std::string(hex.substr(i, 2)), nullptr, 16)));
    }
    return out;
}

std::string to_hex(std::string_view bytes) {
    static constexpr std::string_view kDigits = "0123456789abcdef";
    std::string out;
    for (const char c : bytes) {
        const auto b = static_cast<unsigned char>(c);
        out.push_back(kDigits[b >> 4U]);
        out.push_back(kDigits[b & 0xFU]);
    }
    return out;
}

// Python escribe el registro en modo texto: en Windows, cada «\n» sale «\r\n». Fuera de Windows se
// comparan sin los CR (los dos lados), que es lo que cambia.
std::string native_eol(std::string text) {
#ifndef _WIN32
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (!(text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n')) {
            out.push_back(text[i]);
        }
    }
    return out;
#else
    return text;
#endif
}

} // namespace

TEST_CASE("paridad MCP: la ayuda (help_content.py)", "[parity][mcp]") {
    const Json f = load("help");
    const auto domains = mcp::tool_domains();
    REQUIRE(domains.size() == f["tool_domains"].size());
    for (std::size_t i = 0; i < domains.size(); ++i) {
        CHECK(f["tool_domains"][i][0].get<std::string>() == domains[i].tool);
        CHECK(f["tool_domains"][i][1].get<std::string>() == domains[i].domain);
    }
    const auto guides = mcp::help_guides();
    REQUIRE(guides.size() == f["help"].size());
    for (std::size_t i = 0; i < guides.size(); ++i) {
        INFO(guides[i].domain);
        CHECK(f["help"][i][0].get<std::string>() == guides[i].domain);
        CHECK(f["help"][i][1].get<std::string>() == guides[i].guide); // byte a byte
    }
    same(f["domains"], Json(mcp::help_domains()), "domains()");
    for (const Json& g : f["guide"]) {
        const auto text = mcp::guide(g[0].get<std::string>());
        INFO(g[0].dump());
        if (g[1].is_null()) {
            CHECK_FALSE(text);
        } else {
            REQUIRE(text);
            CHECK(*text == g[1].get<std::string>());
        }
    }
    CHECK(mcp::tool_domain("get_timeline") == "project");
    CHECK(mcp::tool_domain("no_existe") == "other");
}

TEST_CASE("paridad MCP: errores estructurados y registro de tools (registry.py)", "[parity][mcp]") {
    const Json f = load("registry");
    same(f["access_levels"], list(mcp::kAccessLevels), "ACCESS_LEVELS");
    same(f["error_codes"], list(mcp::kErrorCodes), "ERROR_CODES");
    for (const Json& c : f["mcp_errors"]) {
        const Json& in = c["input"];
        const mcp::McpError e{in[0].get<std::string>(), in[1].get<std::string>(), opt_str(in[2]), in[3].get<bool>(),
                              opt_str(in[4])};
        same(c["payload"], e.payload(), "payload de " + in.dump());
        CHECK(mcp::tool_error_text(e) == c["tool_error"].get<std::string>());
    }
    for (const Json& c : f["value_errors"]) {
        const std::string message = c["message"].get<std::string>();
        INFO(message);
        const auto [code, retryable] = mcp::classify_value_error(message);
        CHECK(c["classified"][0].get<std::string>() == code);
        CHECK(c["classified"][1].get<bool>() == retryable);
        CHECK(mcp::tool_error_text(message) == c["tool_error"].get<std::string>());
    }
    for (const Json& c : f["job_dto_checks"]) {
        INFO(c[0].dump());
        CHECK(mcp::is_job_dto(c[0]) == c[1].get<bool>());
    }
    mcp::ToolRegistry registry;
    for (const Json& r : f["registrations"]) {
        const std::string name = r["name"].get<std::string>();
        auto info = registry.add(name, r["access"].get<std::string>());
        INFO(name + " " + r["access"].dump());
        if (r.contains("error")) {
            REQUIRE_FALSE(info);
            CHECK(info.error().message == r["error"].get<std::string>());
        } else {
            REQUIRE(info);
            const Json tool = {{"name", info->name}, {"meta", info->meta}, {"kwargs", Json::array()},
                               {"annotations", info->annotations}};
            same(r["tool"], tool, name);
        }
    }
    Json registered = Json::array();
    for (const mcp::ToolInfo& t : registry.tools()) {
        registered.push_back({t.name, t.access});
    }
    same(f["registered"], registered, "registered()");
    // Las tools reales del servidor (server.py): cada una tiene su dominio en TOOL_DOMAINS.
    REQUIRE(f["builtin_tools"].size() == mcp::tool_domains().size());
    for (const Json& t : f["builtin_tools"]) {
        INFO(t.dump());
        CHECK(mcp::tool_domain(t[0].get<std::string>()) != "other");
    }
}

TEST_CASE("paridad MCP: el DTO semántico (dto.py)", "[parity][mcp]") {
    const Json f = load("dto");
    Json caps = Json::array();
    for (const std::string_view c : mcp::editor_capabilities()) {
        caps.push_back(c);
    }
    same(f["capabilities"], caps, "CAPABILITIES");
    for (const Json& a : f["aspect_ratio"]) {
        INFO(a.dump());
        CHECK(mcp::aspect_ratio(a[0].get<std::int64_t>(), a[1].get<std::int64_t>()) == a[2].get<std::string>());
    }
    REQUIRE(f["projects"].size() >= 10);
    for (const Json& c : f["projects"]) {
        const std::string name = c["name"].get<std::string>();
        const gc::core::Project p = project(c["input"]);
        const gc::core::Timeline* tl = p.timeline ? &*p.timeline : nullptr;
        const Json& histories = f["context_histories"];
        for (std::size_t i = 0; i < histories.size(); ++i) {
            same(c["project_context"][i], mcp::project_context(p, histories[i]),
                 name + ": project_context con el historial " + std::to_string(i));
        }
        same(c["media_list"], mcp::media_list(p), name + ": media_list");
        same(c["timeline_dto"], mcp::timeline_dto(tl), name + ": _timeline_dto");
        auto detail = mcp::timeline_detail(tl);
        if (c["timeline_detail"].contains("error")) {
            INFO(name);
            REQUIRE_FALSE(detail);
            CHECK(detail.error().code == gc::ErrorCode::Internal); // un TypeError de Python: un fallo, no un ValueError
            CHECK("TypeError: " + detail.error().message == c["timeline_detail"]["error"].get<std::string>());
        } else {
            REQUIRE(detail);
            same(c["timeline_detail"], *detail, name + ": timeline_detail");
        }
        REQUIRE(c["clips"].size() == (tl != nullptr ? tl->clips.size() : 0));
        for (std::size_t i = 0; i < c["clips"].size(); ++i) {
            const gc::core::TimelineClip& clip = *tl->clips[i];
            const Json& expected = c["clips"][i];
            CHECK(expected["id"].get<std::string>() == clip.id);
            same(expected["timeline_duration"], Json(mcp::clip_timeline_duration(clip)),
                 name + ": _clip_timeline_duration de " + clip.id);
            same(expected["detail"], mcp::clip_detail(clip), name + ": clip_detail de " + clip.id);
        }
    }
    for (const Json& c : f["analyze"]) {
        same(c["expected"], mcp::analyze_dto(c["dumped"]), "analyze_dto: " + c["name"].get<std::string>());
    }
    for (const Json& c : f["jobs"]) {
        same(c["expected"], mcp::job_dto(c["dumped"]), "job_dto: " + c["name"].get<std::string>());
    }
}

TEST_CASE("paridad MCP: capacidades descubribles (capabilities.py)", "[parity][mcp]") {
    const Json f = load("capabilities");
    const Json& e = f["enums"];
    same(e["ASPECTS"], list(mcp::kAspects), "ASPECTS");
    same(e["MOTIONS"], list(mcp::kMotions), "MOTIONS");
    same(e["DASH_STYLES"], list(mcp::kDashStyles), "DASH_STYLES");
    same(e["TRANSITIONS"], list(mcp::kTransitions), "TRANSITIONS");
    same(e["VISUAL_FX"], list(mcp::kVisualFx), "VISUAL_FX");
    same(e["MASK_TYPES"], list(mcp::kMaskTypes), "MASK_TYPES");
    same(e["AUDIO_FX"], Json(mcp::audio_fx_ids()), "AUDIO_FX");
    same(e["LOOKS"], Json(mcp::look_ids()), "LOOKS");
    same(e["POSITIONS"], list(mcp::kPositions), "POSITIONS");
    same(e["REFRAME_MODES"], list(mcp::kReframeModes), "REFRAME_MODES");
    same(e["TEXT_ROLES"], list(mcp::kTextRoles), "TEXT_ROLES");
    same(e["TRACK_KINDS"], list(mcp::kTrackKinds), "TRACK_KINDS");
    same(e["crop_modes"], list(mcp::kCropModes), "crop_modes()");
    same(f["verbs_by_domain"], mcp::verbs_by_domain(), "verbs_by_domain()");
    same(f["domains"], Json(mcp::help_domains()), "domains()");
    for (const Json& c : f["environments"]) {
        const std::string name = c["name"].get<std::string>();
        const mcp::RuntimeEnv env = runtime_env(c["env"]);
        same(c["overview"], mcp::overview(env), name + ": overview");
        same(c["transcribe_models"], mcp::transcribe_models(env), name + ": transcribe_models");
        same(c["runtime_info"], mcp::runtime_info(env), name + ": runtime_info");
        for (const Json& d : c["describe"]) {
            const std::string query = d[0].get<std::string>();
            auto r = mcp::describe(query, env);
            INFO(name + ": describe(" + d[0].dump() + ")");
            if (d[1].contains("error")) {
                REQUIRE_FALSE(r);
                CHECK(r.error().message == d[1]["error"].get<std::string>());
            } else {
                REQUIRE(r);
                same(d[1], *r, name + ": describe(" + d[0].dump() + ")");
            }
        }
    }
    for (const Json& s : f["project_summaries"]) {
        same(s["expected"], mcp::project_summary(project(s["input"])), "project_summary: " + s["name"].get<std::string>());
    }
}

TEST_CASE("paridad MCP: los resources (resources.py)", "[parity][mcp]") {
    const Json f = load("resources");
    const auto catalog = mcp::resource_catalog();
    REQUIRE(catalog.size() == f["catalog"].size());
    for (std::size_t i = 0; i < catalog.size(); ++i) {
        CHECK(f["catalog"][i]["uri"].get<std::string>() == catalog[i].uri);
        Json kwargs = Json::object();
        if (catalog[i].mime_type) {
            kwargs["mime_type"] = *catalog[i].mime_type;
        }
        kwargs["description"] = catalog[i].description;
        same(f["catalog"][i]["kwargs"], kwargs, std::string(catalog[i].uri));
    }

    std::map<std::string, Json, std::less<>> inputs;
    for (const Json& p : f["projects"]) {
        inputs[p["pid"].get<std::string>()] = p["input"];
    }
    std::map<std::string, Json, std::less<>> raw_histories;
    for (const Json& h : f["histories"]) {
        raw_histories[h["pid"].get<std::string>()] = h["raw"];
        same(h["summary"], mcp::history_summary(gc::core::history_from_json(h["raw"])),
             "_history(" + h["pid"].get<std::string>() + ")");
    }
    std::map<std::string, Json, std::less<>> envs;
    for (const Json& env : f["environments"]) {
        envs[env[0].get<std::string>()] = env[1];
    }
    mcp::ResourceContext ctx;
    ctx.project = [&](std::string_view pid) -> std::optional<gc::core::Project> {
        const auto it = inputs.find(pid);
        if (it == inputs.end()) {
            return std::nullopt;
        }
        return project(it->second);
    };
    ctx.history = [&](std::string_view pid) {
        const auto it = raw_histories.find(pid);
        return gc::core::history_from_json(it == raw_histories.end() ? Json(nullptr) : it->second);
    };
    std::size_t texts = 0;
    for (const Json& c : f["calls"]) {
        mcp::ResourceContext call = ctx;
        if (c.contains("env")) {
            call.env = runtime_env(envs.at(c["env"].get<std::string>()));
        }
        const std::string uri = c["uri"].get<std::string>();
        auto r = mcp::read_resource(uri, c["params"], call);
        INFO(uri + " " + c["params"].dump());
        if (c.contains("error")) {
            REQUIRE_FALSE(r);
            if (c.value("crash", false)) {
                CHECK(r.error().code == gc::ErrorCode::Internal);
                CHECK("TypeError: " + r.error().message == c["error"].get<std::string>());
            } else {
                CHECK(r.error().code != gc::ErrorCode::Internal);
                CHECK(r.error().message == c["error"].get<std::string>());
            }
        } else {
            REQUIRE(r);
            CHECK(*r == c["text"].get<std::string>()); // el texto exacto de json.dumps
            ++texts;
        }
    }
    CHECK(texts > 80);

    // Una URI concreta encaja con su plantilla como en el SDK: cada {parámetro} es un trozo sin «/».
    const auto clip = mcp::match_resource("project://p1/clip/c9");
    REQUIRE(clip);
    CHECK(clip->first == "project://{pid}/clip/{cid}");
    same(Json{{"pid", "p1"}, {"cid", "c9"}}, clip->second, "parámetros de project://p1/clip/c9");
    CHECK(mcp::match_resource("project://p1")->first == "project://{pid}");
    CHECK(mcp::match_resource("project://p1/timeline")->first == "project://{pid}/timeline");
    CHECK(mcp::match_resource("help://audio")->second == Json{{"domain", "audio"}});
    CHECK(mcp::match_resource("capabilities://index")->second == Json::object());
    CHECK_FALSE(mcp::match_resource("project://"));
    CHECK_FALSE(mcp::match_resource("project://p1/clip/"));
    CHECK_FALSE(mcp::match_resource("project://p1/otra"));
    CHECK_FALSE(mcp::match_resource("help://a/b"));
}

TEST_CASE("paridad MCP: la auditoría (audit.py)", "[parity][mcp]") {
    const Json f = load("audit");
    for (const Json& c : f["extract_meta"]) {
        same(c["meta"],
             mcp::extract_meta(c["tool"].get<std::string>(), c["params"], c["settings_model"].get<std::string>()),
             "extract_meta(" + c["tool"].dump() + ", " + c["params"].dump() + ")");
    }

    const auto tmp = std::filesystem::temp_directory_path() / "gc_mcp_audit_parity";
    std::filesystem::remove_all(tmp);
    const auto data_dir = tmp / "nuevo" / "data"; // log la crea
    const auto file = data_dir / "mcp_audit.jsonl";
    std::int64_t now = 0;
    gc::core::SequentialIdSource ids;
    std::map<std::string, mcp::AuditJob, std::less<>> jobs;
    mcp::Audit audit(data_dir, {[&] { return now; }, &ids, [&](std::string_view id) -> std::optional<mcp::AuditJob> {
                                    const auto it = jobs.find(id);
                                    if (it == jobs.end()) {
                                        return std::nullopt;
                                    }
                                    return it->second;
                                }});
    CHECK(audit.path() == file);
    std::size_t files = 0;
    for (const Json& s : f["steps"]) {
        const Json& op = s["op"];
        const std::string kind = op[0].get<std::string>();
        INFO(op.dump());
        Json ret = nullptr;
        std::optional<std::string> error;
        const auto take = [&](const gc::Result<Json>& r) {
            if (r) {
                ret = *r;
            } else {
                error = r.error().message;
            }
        };
        if (kind == "now") {
            now = op[1].get<std::int64_t>();
        } else if (kind == "source") {
            mcp::Audit::set_source(opt_str(op[1]));
        } else if (kind == "begin") {
            ret = audit.begin(op[1].get<std::string>(), op[2].get<std::string>(), op[3], op[4]);
        } else if (kind == "finish") {
            const Json& kw = op[2];
            mcp::FinishArgs a;
            if (kw.contains("status")) {
                a.status = kw["status"].get<std::string>();
            }
            a.ms = opt_ms(kw);
            a.error = kw.contains("error") ? opt_str(kw["error"]) : std::nullopt;
            a.param_keys = opt_keys(kw);
            a.extra_meta = kw.value("extra_meta", Json(nullptr));
            take(audit.finish(op[1].get<std::string>(), a));
        } else if (kind == "log") {
            const Json& kw = op[3];
            mcp::LogArgs a;
            a.project_id = kw.value("project_id", Json(nullptr));
            a.param_keys = opt_keys(kw);
            if (kw.contains("status")) {
                a.status = kw["status"].get<std::string>();
            }
            a.ms = opt_ms(kw);
            a.error = kw.contains("error") ? opt_str(kw["error"]) : std::nullopt;
            a.meta = kw.value("meta", Json(nullptr));
            take(audit.log(op[1].get<std::string>(), op[2].get<std::string>(), a));
        } else if (kind == "track_job") {
            const Json& kw = op[4];
            audit.track_job(op[1].get<std::string>(), op[2].get<std::string>(), op[3].get<std::string>(),
                            kw.value("project_id", Json(nullptr)), kw.value("meta", Json(nullptr)),
                            kw.contains("source_name") ? opt_str(kw["source_name"]) : std::nullopt);
        } else if (kind == "jobs") {
            jobs.clear();
            for (const auto& [id, spec] : op[1].items()) {
                if (spec.is_null()) {
                    continue;
                }
                mcp::AuditJob job; // los valores por defecto de schemas.Job
                job.status = spec["status"].get<std::string>();
                if (spec.contains("progress")) {
                    job.progress = spec["progress"].get<double>();
                }
                if (spec.contains("message")) {
                    job.message = spec["message"];
                }
                jobs[id] = job;
            }
        } else if (kind == "active") {
            ret = audit.active(op[1]);
        } else if (kind == "reset_runtime") {
            audit.reset_runtime();
        } else if (kind == "read_all") {
            take(audit.read_all());
        } else if (kind == "read_recent") {
            take(audit.read_recent(op[1], op[2]));
        } else if (kind == "write_raw") {
            write_bytes(file, op[1].get<std::string>());
        } else if (kind == "write_hex") {
            write_bytes(file, from_hex(op[1].get<std::string>()));
        } else if (kind == "remove_file") {
            std::filesystem::remove(file);
        } else {
            FAIL("paso desconocido: " + kind);
        }
        if (s.contains("error")) {
            REQUIRE(error);
            const std::string expected = s["error"].get<std::string>();
            if (expected.starts_with("JSONDecodeError")) {
                CHECK(error->starts_with("JSONDecodeError")); // el tipo; el texto es el del lector de C++
            } else {
                CHECK(*error == expected);
            }
        } else {
            INFO(error.value_or(""));
            REQUIRE_FALSE(error);
            same(s["ret"], ret, "lo que devuelve " + op.dump());
        }
        if (s.contains("file")) {
            ++files;
            if (s["file"].is_null()) {
                CHECK_FALSE(std::filesystem::exists(file));
            } else {
                REQUIRE(std::filesystem::exists(file));
                CHECK(native_eol(read_bytes(file)) == native_eol(s["file"].get<std::string>()));
            }
        } else if (s.contains("file_hex")) {
            ++files;
            CHECK(to_hex(read_bytes(file)) == s["file_hex"].get<std::string>());
        }
    }
    mcp::Audit::set_source(std::nullopt);
    CHECK(files > 20);
    std::error_code ec;
    std::filesystem::remove_all(tmp, ec);
}

TEST_CASE("MCP: el error de UTF-8 como el de Python y splitlines", "[mcp]") {
    CHECK_FALSE(mcp::utf8_decode_error("hola ñ € 😀"));
    CHECK(mcp::utf8_decode_error("ab\xff") == "'utf-8' codec can't decode byte 0xff in position 2: invalid start byte");
    CHECK(mcp::utf8_decode_error("\xc3(") == "'utf-8' codec can't decode byte 0xc3 in position 0: invalid continuation byte");
    CHECK(mcp::utf8_decode_error("a\xe2\x82") == "'utf-8' codec can't decode bytes in position 1-2: unexpected end of data");
    CHECK(mcp::utf8_decode_error("\xe2\x82(") == "'utf-8' codec can't decode bytes in position 0-1: invalid continuation byte");
    CHECK(mcp::utf8_decode_error("\xed\xa0\x80") == "'utf-8' codec can't decode byte 0xed in position 0: invalid continuation byte");
    CHECK(mcp::utf8_decode_error("\xf4\x90\x80\x80") == "'utf-8' codec can't decode byte 0xf4 in position 0: invalid continuation byte");
    CHECK(mcp::utf8_decode_error("\xc0\xaf") == "'utf-8' codec can't decode byte 0xc0 in position 0: invalid start byte");
    using V = std::vector<std::string>;
    CHECK(mcp::py_splitlines("").empty());
    CHECK(mcp::py_splitlines("a\nb") == V{"a", "b"});
    CHECK(mcp::py_splitlines("a\r\nb\r\n") == V{"a", "b"});
    CHECK(mcp::py_splitlines("a\rb\x0b" "c\x0c" "d\x1c" "e\x1d" "f\x1e" "g") == V{"a", "b", "c", "d", "e", "f", "g"});
    CHECK(mcp::py_splitlines("a\xc2\x85" "b\xe2\x80\xa8" "c\xe2\x80\xa9" "d") == V{"a", "b", "c", "d"});
    CHECK(mcp::py_splitlines("\n\nx") == V{"", "", "x"});
    CHECK(mcp::py_splitlines("a\x1f" "b") == V{"a\x1f" "b"}); // \x1f es espacio, no salto
}

// tools_read.json: las tools de lectura REALES sobre un data/ temporal (projects.json y el historial
// de cada proyecto, tal cual), con el resultado o el error de cada llamada.
TEST_CASE("paridad MCP: las tools de lectura (tools_capabilities, tools_context y tools_read)", "[parity][mcp]") {
    const Json f = load("tools_read");
    const auto tmp = std::filesystem::temp_directory_path() / "gc_mcp_tools_read_parity";
    std::filesystem::remove_all(tmp);
    const auto data = tmp / "data";
    std::filesystem::create_directories(data / "history");
    {
        auto text = gc::core::py_json_dumps(Json{{"projects", f["projects"]}});
        REQUIRE(text);
        std::ofstream(data / "projects.json", std::ios::binary) << *text;
    }
    for (const auto& [pid, text] : f["history_files"].items()) {
        std::ofstream(gc::core::history_path(data, pid), std::ios::binary) << text.get<std::string>();
    }
    const gc::core::ProjectsStore store(data / "projects.json", tmp / "clips");

    mcp::RuntimeEnv env;
    const Json& e = f["environment"];
    env.transcribe_model = e["transcribe_model"].get<std::string>();
    env.tts_engines = e["tts_engines"];
    env.sfx_categories = e["sfx_categories"].get<std::vector<std::string>>();
    env.ffmpeg = e["ffmpeg"].get<bool>();
    env.ffprobe = e["ffprobe"].get<bool>();
    env.data_dir = e["data_dir"].get<std::string>();
    env.gpu = e["gpu"];
    env.ai_provider = e["ai_provider"];

    std::size_t errors = 0;
    for (const Json& c : f["calls"]) {
        const std::string tool = c["tool"].get<std::string>();
        const Json& a = c["args"];
        const auto s = [&](std::size_t i) { return a[i].get<std::string>(); };
        gc::Result<Json> r = gc::fail(gc::ErrorCode::Internal, "tool sin probar: " + tool);
        if (tool == "describe_capabilities") {
            r = mcp::describe_capabilities(a[0], env);
        } else if (tool == "list_projects") {
            r = mcp::list_projects(store);
        } else if (tool == "resolve_project") {
            r = mcp::resolve_project(store, a[0]);
        } else if (tool == "get_project_context") {
            r = mcp::get_project_context(store, data, s(0));
        } else if (tool == "get_timeline") {
            r = mcp::get_timeline(store, s(0));
        } else if (tool == "inspect_clip") {
            r = mcp::inspect_clip(store, s(0), s(1));
        } else if (tool == "list_media") {
            r = mcp::list_media(store, s(0));
        } else if (tool == "search_transcript") {
            r = a.size() > 2 ? mcp::search_transcript(store, s(0), a[1], a[2]) : mcp::search_transcript(store, s(0), a[1]);
        }
        const std::string what = tool + a.dump();
        INFO(what);
        if (c.contains("error")) {
            ++errors;
            REQUIRE_FALSE(r);
            INFO(r.error().message);
            if (c["value_error"].get<bool>()) {
                // Llega al agente: el mismo texto (y el mismo ToolError).
                CHECK(r.error().code != gc::ErrorCode::Internal);
                CHECK(r.error().message == c["error"].get<std::string>());
            } else {
                CHECK(r.error().code == gc::ErrorCode::Internal); // el servidor lo oculta
            }
        } else {
            REQUIRE(r);
            same(c["result"], *r, what);
        }
    }
    CHECK(errors > 30);
    std::error_code ec;
    std::filesystem::remove_all(tmp, ec);
}

TEST_CASE("paridad MCP: las tools de edición (tools_edit y timeline_store)", "[parity][mcp]") {
    const Json f = load("tools_edit");
    const auto tmp = std::filesystem::temp_directory_path() / "gc_mcp_tools_edit_parity";
    std::filesystem::remove_all(tmp);
    const auto data = tmp / "data";
    std::filesystem::create_directories(data / "history");
    const auto write_json = [](const std::filesystem::path& file, const Json& value) {
        auto text = gc::core::py_json_dumps(value);
        REQUIRE(text);
        std::ofstream(file, std::ios::binary) << *text;
    };
    write_json(data / "settings.json", f["settings"]);
    write_json(data / "projects.json", Json{{"projects", f["projects"]}});
    for (const Json& rel : f["sfx_files"]) {
        const auto file = tmp / "sfx" / gc::core::path_from_utf8(rel.get<std::string>());
        std::filesystem::create_directories(file.parent_path());
        std::ofstream(file, std::ios::binary) << "x";
    }
    auto catalog = gc::core::FxCatalog::load(gc::core::path_from_utf8(GC_RESOURCES_DIR) / "fx" / "catalog.json");
    REQUIRE(catalog);
    gc::core::set_fx_catalog(std::make_shared<const gc::core::FxCatalog>(std::move(*catalog)));

    gc::core::ProjectsStore store(data / "projects.json", tmp / "clips");
    gc::core::SequentialIdSource ids;
    mcp::EditToolsEnv env{.store = store, .data_dir = data, .ids = ids, .sfx_base = tmp / "sfx"};
    env.probe_duration = [](const std::filesystem::path&) { return 2.5; };
    env.audio_envelope = [](const gc::core::Project&, const gc::core::TimelineClip& audio, double src, double dur)
        -> std::optional<std::vector<std::pair<double, double>>> {
        if (audio.filename == "falta.mp3") {
            return std::nullopt;
        }
        const auto round_to = [](double v, double scale) { return std::round(v * scale) / scale; };
        return std::vector<std::pair<double, double>>{
            {0.0, round_to(src / 100, 10000)}, {round_to(dur / 2, 1000), 1.0}, {round_to(dur, 1000), 0.5}};
    };

    // Las tools de tools_edit.register: las hechas, en su orden, más las que faltan.
    std::vector<std::pair<std::string, std::string>> done;
    for (const Json& t : f["registered"]) {
        const auto name = t[0].get<std::string>();
        if (std::ranges::find(mcp::kPendingEditTools, name) == mcp::kPendingEditTools.end()) {
            done.emplace_back(name, t[1].get<std::string>());
        }
    }
    REQUIRE(done.size() == mcp::edit_tools().size());
    for (std::size_t i = 0; i < done.size(); ++i) {
        CHECK(done[i].first == mcp::edit_tools()[i].name);
        CHECK(done[i].second == mcp::edit_tools()[i].access);
    }

    std::size_t errors = 0;
    std::map<std::string, Json> timelines;
    for (const Json& c : f["steps"]) {
        const std::string tool = c["tool"].get<std::string>();
        const Json& a = c["args"];
        const auto r = mcp::call_edit_tool(env, tool, a);
        const std::string what = tool + a.dump();
        INFO(what);
        if (c.contains("error")) {
            ++errors;
            REQUIRE_FALSE(r);
            INFO(r.error().message);
            REQUIRE(c["value_error"].get<bool>()); // ningún paso es un crash
            CHECK(r.error().code != gc::ErrorCode::Internal);
            CHECK(r.error().message == c["error"].get<std::string>());
            CHECK(r.error().path == (c["param"].is_string() ? c["param"].get<std::string>() : std::string()));
        } else {
            if (!r) {
                INFO(r.error().message);
                REQUIRE(r);
            }
            same(c["result"], *r, what);
        }
        if (!a.contains("project_id")) {
            continue;
        }
        const auto pid = a["project_id"].get<std::string>();
        if (c.contains("timeline")) {
            timelines[pid] = c["timeline"];
        }
        const Json saved = store.load();
        for (const Json& p : saved["projects"]) {
            if (p["id"] == pid) {
                same(timelines.contains(pid) ? timelines[pid] : Json(), p.value("timeline", Json()), what + " (archivo)");
            }
        }
        const auto file = gc::core::history_path(data, pid);
        CHECK(std::filesystem::exists(file) == c.contains("history"));
        if (c.contains("history")) {
            const auto history = gc::core::load_history(file);
            CHECK(history.undo_stack().size() == c["history"]["undo"].get<std::size_t>());
            CHECK(history.redo_stack().size() == c["history"]["redo"].get<std::size_t>());
            auto names = history.checkpoints();
            std::ranges::sort(names);
            CHECK(Json(names) == c["history"]["checkpoints"]);
        }
    }
    CHECK(errors > 60);

    gc::core::set_fx_catalog(std::make_shared<const gc::core::FxCatalog>(f["catalog"]));
    for (const Json& c : f["catalog_calls"]) {
        const auto r = mcp::call_edit_tool(env, "list_effects", c["args"]);
        REQUIRE(r);
        same(c["result"], *r, "list_effects" + c["args"].dump());
    }
    // Los demás tests no deben ver este catálogo.
    auto real = gc::core::FxCatalog::load(gc::core::path_from_utf8(GC_RESOURCES_DIR) / "fx" / "catalog.json");
    REQUIRE(real);
    gc::core::set_fx_catalog(std::make_shared<const gc::core::FxCatalog>(std::move(*real)));

    const auto unknown = mcp::call_edit_tool(env, "freeze_frame", Json{{"project_id", "e-main"}});
    REQUIRE_FALSE(unknown);
    CHECK(unknown.error().code == gc::ErrorCode::NotFound);
    std::error_code ec;
    std::filesystem::remove_all(tmp, ec);
}

TEST_CASE("paridad MCP: las tools de material sin IA (tools_media, tools_audio y tools_vision)", "[parity][mcp]") {
    const Json f = load("tools_media");
    const auto tmp = std::filesystem::temp_directory_path() /
                     ("gc_mcp_tools_media_parity_" + std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())));
    std::filesystem::remove_all(tmp);
    const auto data = tmp / "data";
    std::filesystem::create_directories(data / "history");
    {
        auto text = gc::core::py_json_dumps(Json{{"projects", f["projects"]}});
        REQUIRE(text);
        std::ofstream(data / "projects.json", std::ios::binary) << *text;
        std::ofstream(data / "settings.json", std::ios::binary) << "{}";
    }
    for (const char* list : {"files", "sfx_files"}) {
        for (const Json& rel : f[list]) {
            const auto file = (std::string(list) == "files" ? tmp : tmp / "sfx") / gc::core::path_from_utf8(rel.get<std::string>());
            std::filesystem::create_directories(file.parent_path());
            std::ofstream(file, std::ios::binary) << "x";
        }
    }
    gc::core::ProjectsStore store(data / "projects.json", tmp / "clips");
    gc::core::SequentialIdSource ids;
    mcp::EditToolsEnv env{.store = store, .data_dir = data, .ids = ids, .sfx_base = tmp / "sfx"};
    const Json dims = f["dims"];
    env.video_dims = [&dims](const std::filesystem::path& p) -> std::optional<std::pair<std::int64_t, std::int64_t>> {
        const std::string name = gc::core::path_to_utf8(p.filename());
        if (!dims.contains(name)) {
            return std::nullopt; // detect.dims falla: sin medidas
        }
        return std::pair<std::int64_t, std::int64_t>(dims[name][0].get<std::int64_t>(), dims[name][1].get<std::int64_t>());
    };

    REQUIRE(f["registered"].size() == mcp::media_tools().size());
    for (std::size_t i = 0; i < mcp::media_tools().size(); ++i) {
        CHECK(f["registered"][i][0].get<std::string>() == mcp::media_tools()[i].name);
        CHECK(f["registered"][i][1].get<std::string>() == mcp::media_tools()[i].access);
    }
    std::size_t errors = 0;
    for (const Json& c : f["steps"]) {
        const std::string tool = c["tool"].get<std::string>();
        const Json& a = c["args"];
        const auto r = mcp::call_media_tool(env, tool, a);
        const std::string what = tool + a.dump();
        INFO(what);
        if (c.contains("error")) {
            ++errors;
            REQUIRE_FALSE(r);
            INFO(r.error().message);
            REQUIRE(c["value_error"].get<bool>()); // ningún paso es un crash
            CHECK(r.error().code != gc::ErrorCode::Internal);
            CHECK(r.error().message == c["error"].get<std::string>());
            const Json mcp_error = c.value("mcp", Json());
            const auto [code, retryable] = mcp::classify_value_error(r.error().message);
            if (mcp_error.is_object()) { // lo que el servidor saca de un MCPError: código, pista, param
                CHECK(code == mcp_error["code"].get<std::string>());
                CHECK(retryable == mcp_error["retryable"].get<bool>());
                CHECK(r.error().hint == (mcp_error["hint"].is_string() ? mcp_error["hint"].get<std::string>() : std::string()));
                CHECK(r.error().path == (mcp_error["param"].is_string() ? mcp_error["param"].get<std::string>() : std::string()));
            } else {
                CHECK(r.error().hint.empty());
                CHECK(r.error().path.empty());
            }
        } else {
            if (!r) {
                INFO(r.error().message);
                REQUIRE(r);
            }
            same(c["result"], *r, what);
        }
        if (!c.contains("project")) {
            continue;
        }
        const Json saved = store.load();
        Json project;
        for (const Json& p : saved["projects"]) {
            if (p["id"] == a["project_id"]) {
                project = p;
            }
        }
        same(c["project"], project, what + " (projects.json)");
        Json files = Json::array();
        std::vector<std::string> found;
        for (const auto& e : std::filesystem::recursive_directory_iterator(tmp / "clips")) {
            if (e.is_regular_file()) {
                std::string rel = gc::core::path_to_utf8(std::filesystem::relative(e.path(), tmp));
                std::ranges::replace(rel, '\\', '/');
                found.push_back(std::move(rel));
            }
        }
        std::ranges::sort(found);
        CHECK(Json(found) == c["files"]);
    }
    CHECK(errors == 18);
    // Un limit null explícito es int(None) de Python: un crash, no el 50 por defecto (el servidor ya lo
    // rechaza antes, al validar los argumentos).
    const auto null_limit = mcp::call_media_tool(env, "search_sfx", Json{{"limit", nullptr}});
    REQUIRE_FALSE(null_limit);
    CHECK(null_limit.error().code == gc::ErrorCode::Internal);
    const auto unknown = mcp::call_media_tool(env, "fetch_image", Json::object());
    REQUIRE_FALSE(unknown);
    CHECK(unknown.error().code == gc::ErrorCode::NotFound);
    std::error_code ec;
    std::filesystem::remove_all(tmp, ec);
}

namespace {

// Las tools de `registered` (las de una tanda, en el orden de tools_scene.register) son las de
// scene_tools() con ese nombre, en el mismo orden y con el mismo acceso.
void check_scene_registered(const Json& registered) {
    std::vector<const mcp::EditToolInfo*> mine;
    for (const auto& t : mcp::scene_tools()) {
        for (const Json& r : registered) {
            if (r[0].get<std::string>() == t.name) {
                mine.push_back(&t);
            }
        }
    }
    REQUIRE(mine.size() == registered.size());
    for (std::size_t i = 0; i < mine.size(); ++i) {
        CHECK(registered[i][0].get<std::string>() == mine[i]->name);
        CHECK(registered[i][1].get<std::string>() == mine[i]->access);
    }
}

} // namespace

TEST_CASE("paridad MCP: las tools de dirección de escena sin IA (tools_scene)", "[parity][mcp]") {
    const Json f = load("tools_scene");
    const auto tmp = std::filesystem::temp_directory_path() /
                     ("gc_mcp_tools_scene_parity_" + std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())));
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(tmp / "data");
    {
        auto text = gc::core::py_json_dumps(Json{{"projects", f["projects"]}});
        REQUIRE(text);
        std::ofstream(tmp / "data" / "projects.json", std::ios::binary) << *text;
    }
    gc::core::ProjectsStore store(tmp / "data" / "projects.json", tmp / "clips");
    gc::core::SequentialIdSource ids;
    std::int64_t ticks = 0; // el time.time() de scene_direction en Python: 1700000000 + n/4
    mcp::EditToolsEnv env{.store = store, .data_dir = tmp / "data", .ids = ids};
    env.unix_now = [&ticks] { return 1700000000.0 + 0.25 * static_cast<double>(++ticks); };

    check_scene_registered(f["registered"]);
    std::size_t errors = 0;
    std::size_t crashes = 0;
    for (const Json& c : f["steps"]) {
        const std::string tool = c["tool"].get<std::string>();
        const Json& a = c["args"];
        const auto r = mcp::call_scene_tool(env, tool, a);
        const std::string what = tool + a.dump();
        INFO(what);
        if (c.contains("error")) {
            ++errors;
            REQUIRE_FALSE(r);
            INFO(r.error().message);
            if (!c["value_error"].get<bool>()) { // un TypeError de Python: un crash
                ++crashes;
                CHECK(r.error().code == gc::ErrorCode::Internal);
            } else {
                REQUIRE(c.contains("mcp")); // todos los errores de estas tools son MCPError
                const Json& m = c["mcp"];
                CHECK(r.error().message == c["error"].get<std::string>());
                CHECK(r.error().mcp_code == m["code"].get<std::string>());
                CHECK(r.error().retryable == m["retryable"].get<bool>());
                CHECK(r.error().path == (m["param"].is_string() ? m["param"].get<std::string>() : std::string()));
                CHECK(r.error().hint.empty());
            }
        } else {
            if (!r) {
                INFO(r.error().message);
                REQUIRE(r);
            }
            same(c["result"], *r, what);
        }
        Json project;
        const Json stored = store.load(); // el temporal no vive lo que el bucle en GCC 13 (sin P2718)
        for (const Json& p : stored["projects"]) {
            if (p["id"] == a["project_id"]) {
                project = p;
            }
        }
        same(c["project"], project, what + " (projects.json)");
    }
    CHECK(errors == 18);
    CHECK(crashes == 2);
    const auto unknown = mcp::call_scene_tool(env, "scene_direction_pack", Json::object());
    REQUIRE_FALSE(unknown);
    CHECK(unknown.error().code == gc::ErrorCode::NotFound);
    std::error_code ec;
    std::filesystem::remove_all(tmp, ec);
}

TEST_CASE("paridad MCP: las tools del material de la dirección de escena (tools_scene_material)", "[parity][mcp]") {
    const Json f = load("tools_scene_material");
    const auto tmp = std::filesystem::temp_directory_path() /
                     ("gc_mcp_tools_scene_material_parity_" +
                      std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())));
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(tmp / "data" / "library");
    const auto write = [](const std::filesystem::path& file, const Json& value) {
        auto text = gc::core::py_json_dumps(value);
        REQUIRE(text);
        std::ofstream(file, std::ios::binary) << *text;
    };
    write(tmp / "data" / "projects.json", Json{{"projects", f["projects"]}});
    write(tmp / "data" / "library" / "library.json", f["library"]);
    gc::core::ProjectsStore store(tmp / "data" / "projects.json", tmp / "clips");
    gc::core::SequentialIdSource ids;
    std::int64_t ticks = 0; // el time.time() de scene_direction en Python: 1700000000 + n/4
    mcp::EditToolsEnv env{.store = store, .data_dir = tmp / "data", .ids = ids};
    env.unix_now = [&ticks] { return 1700000000.0 + 0.25 * static_cast<double>(++ticks); };

    REQUIRE(mcp::scene_tools().size() == 10);
    check_scene_registered(f["registered"]);
    std::size_t errors = 0;
    std::size_t crashes = 0;
    for (const Json& c : f["steps"]) {
        const std::string tool = c["tool"].get<std::string>();
        const Json& a = c["args"];
        const auto r = mcp::call_scene_tool(env, tool, a);
        const std::string what = tool + a.dump();
        INFO(what);
        if (c.contains("error")) {
            ++errors;
            REQUIRE_FALSE(r);
            INFO(r.error().message);
            if (!c.contains("mcp")) { // OverflowError o AttributeError de Python: un crash
                ++crashes;
                CHECK(r.error().code == gc::ErrorCode::Internal);
            } else {
                const Json& m = c["mcp"];
                CHECK(r.error().message == c["error"].get<std::string>());
                CHECK(r.error().mcp_code == m["code"].get<std::string>());
                CHECK(r.error().retryable == m["retryable"].get<bool>());
                CHECK(r.error().path == (m["param"].is_string() ? m["param"].get<std::string>() : std::string()));
                CHECK(r.error().hint.empty());
            }
        } else {
            if (!r) {
                INFO(r.error().message);
                REQUIRE(r);
            }
            same(c["result"], *r, what);
        }
        Json project;
        const Json stored = store.load(); // el temporal no vive lo que el bucle en GCC 13 (sin P2718)
        for (const Json& p : stored["projects"]) {
            if (p["id"] == a["project_id"]) {
                project = p;
            }
        }
        same(c["project"], project, what + " (projects.json)");
        auto lib = gc::core::read_json_file(tmp / "data" / "library" / "library.json");
        REQUIRE(lib);
        same(c["library"], *lib, what + " (library.json)");
    }
    CHECK(errors == 20);
    CHECK(crashes == 3);
    std::error_code ec;
    std::filesystem::remove_all(tmp, ec);
}

TEST_CASE("paridad MCP: el pack de un tramo de la dirección de escena (tools_scene_pack)", "[parity][mcp]") {
    const Json f = load("tools_scene_pack");
    const auto tmp = std::filesystem::temp_directory_path() /
                     ("gc_mcp_tools_scene_pack_parity_" +
                      std::to_string(std::hash<std::thread::id>{}(std::this_thread::get_id())));
    std::filesystem::remove_all(tmp);
    std::filesystem::create_directories(tmp / "data" / "library");
    const auto write = [](const std::filesystem::path& file, const Json& value) {
        auto text = gc::core::py_json_dumps(value);
        REQUIRE(text);
        std::ofstream(file, std::ios::binary) << *text;
    };
    write(tmp / "data" / "projects.json", Json{{"projects", f["projects"]}});
    write(tmp / "data" / "library" / "library.json", f["library"]);
    gc::core::ProjectsStore store(tmp / "data" / "projects.json", tmp / "clips");
    const Json saved = store.load();
    gc::core::SequentialIdSource ids;
    std::int64_t ticks = 0; // el time.time() de scene_direction en Python: 1700000000 + n/4, seguido
    mcp::EditToolsEnv env{.store = store, .data_dir = tmp / "data", .ids = ids};
    env.unix_now = [&ticks] { return 1700000000.0 + 0.25 * static_cast<double>(++ticks); };

    check_scene_registered(f["registered"]);
    std::size_t errors = 0;
    std::size_t crashes = 0;
    std::size_t value_errors = 0;
    for (const Json& c : f["steps"]) {
        const std::string tool = c["tool"].get<std::string>();
        const Json& a = c["args"];
        const auto r = mcp::call_scene_tool(env, tool, a);
        const std::string what = tool + a.dump();
        INFO(what);
        if (c.contains("error")) {
            ++errors;
            REQUIRE_FALSE(r);
            INFO(r.error().message);
            if (c.contains("mcp")) {
                const Json& m = c["mcp"];
                CHECK(r.error().message == c["error"].get<std::string>());
                CHECK(r.error().mcp_code == m["code"].get<std::string>());
                CHECK(r.error().retryable == m["retryable"].get<bool>());
                CHECK(r.error().path == (m["param"].is_string() ? m["param"].get<std::string>() : std::string()));
                CHECK(r.error().hint.empty());
            } else if (c["value_error"].get<bool>()) { // un ValueError suelto: llega al agente con su texto
                ++value_errors;
                CHECK(r.error().code == gc::ErrorCode::InvalidArgument);
                CHECK(r.error().mcp_code.empty());
                CHECK(r.error().message == c["error"].get<std::string>());
            } else { // TypeError, AttributeError u OverflowError de Python: un crash
                ++crashes;
                CHECK(r.error().code == gc::ErrorCode::Internal);
            }
        } else {
            if (!r) {
                INFO(r.error().message);
                REQUIRE(r);
            }
            same(c["result"], *r, what);
        }
    }
    // La tool solo lee.
    same(saved, store.load(), "projects.json");
    auto lib = gc::core::read_json_file(tmp / "data" / "library" / "library.json");
    REQUIRE(lib);
    same(f["library"], *lib, "library.json");
    CHECK(f["steps"].size() == 33);
    CHECK(errors == 14);
    CHECK(crashes == 6);
    CHECK(value_errors == 1);
    std::error_code ec;
    std::filesystem::remove_all(tmp, ec);
}
