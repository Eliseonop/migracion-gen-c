#include "gc/core/font_metrics.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "gc/core/io.hpp"

namespace gc::core {

namespace {

struct FontFile {
    std::string_view name;
    std::string_view file;
};

constexpr std::array kFonts{
    FontFile{"Arial", "arial.ttf"},       FontFile{"Arial Black", "ariblk.ttf"},
    FontFile{"Impact", "impact.ttf"},     FontFile{"Georgia", "georgia.ttf"},
    FontFile{"Verdana", "verdana.ttf"},   FontFile{"Times New Roman", "times.ttf"},
    FontFile{"Courier New", "cour.ttf"},  FontFile{"Comic Sans MS", "comic.ttf"},
    FontFile{"Trebuchet MS", "trebuc.ttf"}, FontFile{"Segoe UI", "segoeui.ttf"},
    FontFile{"Segoe UI Black", "seguibl.ttf"}, FontFile{"Calibri", "calibri.ttf"},
    FontFile{"Bahnschrift", "bahnschrift.ttf"}, FontFile{"Tahoma", "tahoma.ttf"},
    FontFile{"Consolas", "consola.ttf"},
};
constexpr std::array kFontsBold{
    FontFile{"Arial", "arialbd.ttf"},     FontFile{"Georgia", "georgiab.ttf"},
    FontFile{"Verdana", "verdanab.ttf"},  FontFile{"Times New Roman", "timesbd.ttf"},
    FontFile{"Courier New", "courbd.ttf"}, FontFile{"Trebuchet MS", "trebucbd.ttf"},
    FontFile{"Segoe UI", "segoeuib.ttf"}, FontFile{"Calibri", "calibrib.ttf"},
    FontFile{"Tahoma", "tahomabd.ttf"},   FontFile{"Consolas", "consolab.ttf"},
};
constexpr std::array kBundledFonts{FontFile{"Anton", "Anton-Regular.ttf"}};

template <std::size_t N>
std::optional<std::string_view> lookup(const std::array<FontFile, N>& table, std::string_view name) {
    for (const auto& f : table) {
        if (f.name == name) {
            return f.file;
        }
    }
    return std::nullopt;
}

bool file_exists(const std::filesystem::path& p) {
    std::error_code ec;
    return std::filesystem::exists(p, ec);
}

std::uint32_t be(const std::vector<unsigned char>& d, std::size_t at, std::size_t bytes) {
    std::uint32_t v = 0;
    for (std::size_t i = 0; i < bytes; ++i) {
        v = (v << 8) | d[at + i];
    }
    return v;
}

// Trozo [begin, end) del archivo, recortado a su tamaño como data[off:off + length] de Python.
struct Span {
    std::size_t begin = 0;
    std::size_t end = 0;
    [[nodiscard]] std::size_t size() const { return end - begin; }
};

std::mutex g_cache_mutex;
std::map<std::filesystem::path, double> g_cache;

} // namespace

std::filesystem::path default_system_font_dir() {
    return std::filesystem::path("C:/Windows/Fonts");
}

std::filesystem::path resolve_font_path(std::string_view name, bool bold, const FontDirs& dirs) {
    if (const auto bundled = lookup(kBundledFonts, name)) {
        auto p = dirs.bundled / path_from_utf8(*bundled);
        if (file_exists(p)) {
            return p;
        }
    }
    std::optional<std::string_view> file = bold ? lookup(kFontsBold, name) : std::nullopt;
    if (!file) {
        file = lookup(kFonts, name);
    }
    auto p = dirs.system / path_from_utf8(file.value_or("arial.ttf"));
    if (!file_exists(p)) {
        p = dirs.system / "arial.ttf";
    }
    return p;
}

double font_line_height_em(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        return 1.0;
    }
    const std::vector<unsigned char> data{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    if (in.bad() || data.size() < 6) {
        return 1.0;
    }
    // Directorio de tablas: cada entrada pisa a una anterior con la misma etiqueta.
    std::map<std::string, Span, std::less<>> tables;
    const std::size_t num = be(data, 4, 2);
    for (std::size_t i = 0; i < num; ++i) {
        const std::size_t at = 12 + 16 * i;
        if (at + 16 > data.size()) {
            return 1.0;
        }
        const std::size_t off = be(data, at + 8, 4);
        const std::size_t len = be(data, at + 12, 4);
        tables[std::string(reinterpret_cast<const char*>(&data[at]), 4)] = {std::min(off, data.size()),
                                                                             std::min(off + len, data.size())};
    }
    const auto head = tables.find("head");
    if (head == tables.end() || head->second.size() < 20) {
        return 1.0;
    }
    const std::uint32_t upm = be(data, head->second.begin + 18, 2);
    double height = 0;
    const auto os2 = tables.find("OS/2");
    if (os2 != tables.end() && os2->second.size() >= 78) {
        height = static_cast<double>(be(data, os2->second.begin + 74, 2)) + be(data, os2->second.begin + 76, 2);
    } else {
        const auto hhea = tables.find("hhea");
        if (hhea == tables.end() || hhea->second.size() < 8) {
            return 1.0;
        }
        const auto asc = static_cast<std::int16_t>(be(data, hhea->second.begin + 4, 2));
        const auto desc = static_cast<std::int16_t>(be(data, hhea->second.begin + 6, 2));
        height = static_cast<double>(asc) - desc;
    }
    return upm != 0 && height > 0 ? height / upm : 1.0;
}

double ass_size_factor(std::string_view name, bool bold, const FontDirs& dirs) {
    const auto path = resolve_font_path(name.empty() ? "Arial" : name, bold, dirs);
    {
        const std::scoped_lock lock(g_cache_mutex);
        if (const auto it = g_cache.find(path); it != g_cache.end()) {
            return it->second;
        }
    }
    const double factor = font_line_height_em(path);
    const std::scoped_lock lock(g_cache_mutex);
    g_cache.emplace(path, factor);
    return factor;
}

} // namespace gc::core
