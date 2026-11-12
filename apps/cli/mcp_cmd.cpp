#include "mcp_cmd.hpp"

#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "gc/agent/mcp_http_server.hpp"
#include "gc/core/fx_catalog.hpp"
#include "gc/core/ids.hpp"
#include "gc/core/io.hpp"
#include "gc/core/mcp_audit.hpp"
#include "gc/core/mcp_capabilities.hpp"
#include "gc/core/mcp_server.hpp"
#include "gc/core/projects_store.hpp"
#ifdef GC_WITH_RENDER
#include "gc/media/decode.hpp"
#include "gc/media/probe.hpp"
#endif
#include "gc/core/settings.hpp"

namespace genclip {

namespace fs = std::filesystem;
namespace mcp = gc::core::mcp;

namespace {

int usage_error(const std::string& message) {
    std::cerr << "error: " << message << "\nUso: genclip mcp --data <carpeta data> --clips <carpeta clips> "
              << "[--fx <catalog.json>] [--sfx <carpeta>] [--port 8000]\n";
    return 2;
}

// ¿Se encuentra el programa en el PATH? (shutil.which)
bool in_path(const std::string& name) {
    const char* path = std::getenv("PATH"); // NOLINT(concurrency-mt-unsafe): antes de abrir el servidor
    if (path == nullptr) {
        return false;
    }
#ifdef _WIN32
    constexpr char kSeparator = ';';
    const std::string file = name + ".exe";
#else
    constexpr char kSeparator = ':';
    const std::string& file = name;
#endif
    std::string_view rest = path;
    while (!rest.empty()) {
        const auto cut = rest.find(kSeparator);
        std::error_code ec;
        if (fs::exists(gc::core::path_from_utf8(rest.substr(0, cut)) / file, ec)) {
            return true;
        }
        rest = cut == std::string_view::npos ? std::string_view() : rest.substr(cut + 1);
    }
    return false;
}

} // namespace

int cmd_mcp(std::span<const std::string> args) {
    std::optional<fs::path> data;
    std::optional<fs::path> clips;
    std::optional<fs::path> fx;
    std::optional<fs::path> sfx;
    int port = 8000;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& flag = args[i];
        if (i + 1 >= args.size()) {
            return usage_error("a " + flag + " le falta el valor");
        }
        const std::string& value = args[++i];
        if (flag == "--data") {
            data = gc::core::path_from_utf8(value);
        } else if (flag == "--clips") {
            clips = gc::core::path_from_utf8(value);
        } else if (flag == "--fx") {
            fx = gc::core::path_from_utf8(value);
        } else if (flag == "--sfx") {
            sfx = gc::core::path_from_utf8(value);
        } else if (flag == "--port") {
            const auto [end, ec] = std::from_chars(value.data(), value.data() + value.size(), port);
            if (ec != std::errc() || end != value.data() + value.size() || port < 0 || port > 65535) {
                return usage_error("puerto no válido: " + value);
            }
        } else {
            return usage_error("opción desconocida: " + flag);
        }
    }
    if (!data || !clips) {
        return usage_error("faltan --data y --clips");
    }
    if (fx) {
        auto catalog = gc::core::FxCatalog::load(*fx);
        if (!catalog) {
            std::cerr << "error: " << catalog.error().to_string() << '\n';
            return 1;
        }
        gc::core::set_fx_catalog(std::make_shared<const gc::core::FxCatalog>(std::move(*catalog)));
    }

    gc::core::ProjectsStore store(*data / "projects.json", *clips);
    gc::core::RandomIdSource ids;
    mcp::Audit audit(*data);
    mcp::RuntimeEnv runtime;
    runtime.ffmpeg = in_path("ffmpeg");
    runtime.ffprobe = in_path("ffprobe");
    runtime.data_dir = gc::core::path_to_utf8(*data);
    const gc::Json settings = gc::core::SettingsFile(*data / "settings.json").load();
    runtime.transcribe_model = gc::core::normalize_transcribe_settings(settings.value("transcribe", gc::Json()))["model"].get<std::string>();

    mcp::EditToolsEnv edit{.store = store, .data_dir = *data, .ids = ids, .sfx_base = sfx};
#ifdef GC_WITH_RENDER
    // Lo que en video-yt pregunta a ffprobe y a OpenCV: la duración de un SFX (add_sfx) y las medidas de un
    // vídeo (replace_media_version), las de su primer fotograma con el autogiro.
    gc::media::silence_ffmpeg_log();
    edit.probe_duration = [](const std::filesystem::path& p) { // round(…, 3) or 3.0
        const double d = gc::media::probe_duration(p);
        return d != 0.0 ? d : 3.0;
    };
    edit.video_dims = [](const std::filesystem::path& p) -> std::optional<std::pair<std::int64_t, std::int64_t>> {
        const auto first = gc::media::frame_at(p, 0.0);
        if (!first || !first->has_value()) {
            return std::nullopt;
        }
        return std::pair<std::int64_t, std::int64_t>((*first)->width, (*first)->height);
    };
#endif
    mcp::McpServer server(edit, runtime, &audit);
    gc::agent::McpHttpServer http(server);
    const auto bound = http.bind("127.0.0.1", port);
    if (!bound) {
        std::cerr << "error: " << bound.error().to_string() << '\n';
        return 1;
    }
    std::cout << "MCP en http://127.0.0.1:" << *bound << "/mcp (" << mcp::tool_names().size() << " tools)" << std::endl; // NOLINT(performance-avoid-endl): quien lo lanza lee el puerto
    http.serve();
    return 0;
}

} // namespace genclip
