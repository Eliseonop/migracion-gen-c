// genclip: herramienta de línea de órdenes de GenClipPro.
//
//   genclip version
//   genclip projects <projects.json>                     valida todos los proyectos
//   genclip normalize <projects.json> <salida> [--preserve]
//
// Los argumentos y la salida van en UTF-8 en todos los SO (rutas con tildes).

#include <format>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "gc/core/io.hpp"
#include "gc/core/migrations.hpp"
#include "gc/core/serialize.hpp"
#include "gc/core/version.hpp"

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

std::vector<std::string> utf8_args(int argc, char** argv) {
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
    LocalFree(wargs);
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
        while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) {
            --cut;
        }
        s.resize(cut);
        s += "~";
    }
    return s;
}

int cmd_projects(Args args) {
    if (args.size() != 1) {
        usage();
        return 2;
    }
    auto raw = gc::core::read_json_file(gc::core::path_from_utf8(args[0]));
    if (!raw) {
        std::cerr << raw.error().to_string() << "\n";
        return 1;
    }
    const auto it = raw->find("projects");
    if (it == raw->end() || !it->is_array()) {
        std::cerr << "el archivo no tiene una lista \"projects\"\n";
        return 1;
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
        std::cerr << raw.error().to_string() << "\n";
        return 1;
    }
    auto file = gc::core::projects_file_from_json(*raw);
    if (!file) {
        std::cerr << file.error().to_string() << "\n";
        return 1;
    }
    if (auto ok = gc::core::write_json_file(gc::core::path_from_utf8(args[1]), gc::core::to_json(*file, mode)); !ok) {
        std::cerr << ok.error().to_string() << "\n";
        return 1;
    }
    std::cout << file->projects.size() << " proyectos escritos en " << args[1] << "\n";
    return 0;
}

} // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    const std::vector<std::string> all = utf8_args(argc, argv);
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
    usage();
    return cmd == "help" || cmd == "--help" ? 0 : 2;
}
