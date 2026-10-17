// genclip: herramienta de línea de órdenes de GenClipPro.
//
//   genclip version
//   genclip projects <projects.json>                     valida todos los proyectos
//   genclip normalize <projects.json> <salida> [--preserve]
//   genclip frame <raíz de video-yt> <proyecto> --at <s> -o <salida.png> [--omitir]
//   genclip render <raíz de video-yt> <proyecto> -o <salida.mp4> [--progress] [--omitir]
//                  [--codificador <nombre>] [--hilos <n>] [--tiempos]
//                                                        (frame y render, solo con FFmpeg y Skia)
//
// Los argumentos y la salida van en UTF-8 en todos los SO (rutas con tildes). Los errores van
// siempre a stderr («error: …»), con cualquier GENCLIP_LOG; GENCLIP_LOG=debug añade la traza.

#include <format>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <spdlog/spdlog.h>

#include "gc/core/io.hpp"
#include "gc/core/migrations.hpp"
#include "gc/core/serialize.hpp"
#include "gc/core/version.hpp"
#include "gc/log/log.hpp"

#ifdef GC_WITH_RENDER
#include "frame_cmd.hpp"
#include "render_cmd.hpp"
#endif

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
// clang-format off: shellapi.h necesita windows.h antes
#include <windows.h>
#include <shellapi.h>
// clang-format on
#endif

namespace {

using gc::Json;
using Args = std::span<const std::string>;

std::vector<std::string> utf8_args(int argc, char* const* argv) {
#ifdef _WIN32
    (void)argc;
    (void)argv;
    std::vector<std::string> out;
    int n = 0;
    LPWSTR* wargs = CommandLineToArgvW(GetCommandLineW(), &n);
    for (int i = 0; i < n; ++i) {
        const int len = WideCharToMultiByte(CP_UTF8, 0, wargs[i], -1, nullptr, 0, nullptr, nullptr);
        std::string s(static_cast<std::size_t>(len > 0 ? len - 1 : 0), '\0');
        if (len > 1) {
            WideCharToMultiByte(CP_UTF8, 0, wargs[i], -1, s.data(), len, nullptr, nullptr);
        }
        out.push_back(std::move(s));
    }
    LocalFree(static_cast<HLOCAL>(wargs));
    return out;
#else
    return {argv, argv + argc};
#endif
}

void usage() {
    std::cout << "genclip " << gc::kVersion << " - motor de GenClipPro\n\n"
              << "Uso:\n"
              << "  genclip version\n"
              << "  genclip projects <projects.json>\n"
              << "      Carga y valida todos los proyectos (mismas reglas que video-yt).\n"
              << "  genclip normalize <projects.json> <salida.json> [--preserve]\n"
              << "      Escribe el archivo normalizado como lo haría model_dump(); con\n"
              << "      --preserve conserva también las claves que el modelo no conoce.\n";
}

// Recorta a `n` bytes sin partir un carácter UTF-8.
std::string truncate(std::string s, std::size_t n) {
    if (s.size() > n) {
        std::size_t cut = n - 1;
        while (cut > 0 && (static_cast<unsigned>(static_cast<unsigned char>(s[cut])) & 0xC0U) == 0x80U) {
            --cut;
        }
        s.resize(cut);
        s += '~';
    }
    return s;
}

// Los errores salen siempre por stderr, sea cual sea GENCLIP_LOG: quien lanza genclip (video-yt
// en F3) los lee. El registro (spdlog) queda para la traza.
int report_error(std::string_view message) {
    std::cerr << "error: " << message << '\n';
    return 1;
}

int cmd_projects(Args args) {
    if (args.size() != 1) {
        usage();
        return 2;
    }
    auto raw = gc::core::read_json_file(gc::core::path_from_utf8(args[0]));
    if (!raw) {
        return report_error(raw.error().to_string());
    }
    const auto it = raw->find("projects");
    if (it == raw->end() || !it->is_array()) {
        return report_error("el archivo no tiene una lista \"projects\"");
    }
    int failed = 0;
    std::size_t total_clips = 0;
    for (const Json& p : *it) {
        const int version = p.is_object() && p.contains("timeline") ? gc::core::timeline_version(p["timeline"]) : 0;
        auto project = gc::core::project_from_json(p);
        const std::string id = p.is_object() && p.contains("id") ? p["id"].dump() : "?";
        if (!project) {
            ++failed;
            std::cout << "  ERROR " << id << ": " << project.error().to_string() << "\n";
            continue;
        }
        const std::size_t clips = project->timeline ? project->timeline->clips.size() : 0;
        const auto schema = project->timeline ? project->timeline->schema_version : 0;
        total_clips += clips;
        std::cout << std::format("  ok    {:<10} v{}->v{}  {:>4} clips  {}\n", project->id, version, schema, clips,
                                 truncate(project->name, 48));
    }
    std::cout << "\n" << it->size() << " proyectos, " << total_clips << " clips en timelines, " << failed
              << " con errores\n";
    return failed == 0 ? 0 : 1;
}

int cmd_normalize(Args args) {
    if (args.size() < 2 || args.size() > 3 || (args.size() == 3 && args[2] != "--preserve")) {
        usage();
        return 2;
    }
    const auto mode = args.size() == 3 ? gc::core::DumpMode::Preserve : gc::core::DumpMode::Model;
    auto raw = gc::core::read_json_file(gc::core::path_from_utf8(args[0]));
    if (!raw) {
        return report_error(raw.error().to_string());
    }
    auto file = gc::core::projects_file_from_json(*raw);
    if (!file) {
        return report_error(file.error().to_string());
    }
    if (auto ok = gc::core::write_json_file(gc::core::path_from_utf8(args[1]), gc::core::to_json(*file, mode)); !ok) {
        return report_error(ok.error().to_string());
    }
    std::cout << file->projects.size() << " proyectos escritos en " << args[1] << "\n";
    return 0;
}

int run(int argc, char* const* argv) {
    gc::log::init(gc::log::level_from_env(gc::log::Level::warn));
    const std::vector<std::string> all = utf8_args(argc, argv);
    spdlog::debug("genclip {}: {} argumentos", gc::kVersion, all.size());
    if (all.size() < 2) {
        usage();
        return 2;
    }
    const std::string_view cmd = all[1];
    const Args rest = Args(all).subspan(2);
    if (cmd == "version" || cmd == "--version") {
        std::cout << "genclip " << gc::kVersion << "\n";
        return 0;
    }
    if (cmd == "projects") {
        return cmd_projects(rest);
    }
    if (cmd == "normalize") {
        return cmd_normalize(rest);
    }
#ifdef GC_WITH_RENDER
    if (cmd == "frame") {
        return genclip::cmd_frame(rest);
    }
    if (cmd == "render") {
        return genclip::cmd_render(rest);
    }
#endif
    usage();
    return cmd == "help" || cmd == "--help" ? 0 : 2;
}

} // namespace

int main(int argc, char** argv) { // NOLINT(misc-const-correctness): la firma estándar de main
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    try {
        return run(argc, argv);
    } catch (const std::exception& e) { // un fallo inesperado (memoria, formato): sin cierre brusco
        std::cerr << "error inesperado: " << e.what() << "\n";
        return 1;
    }
}
