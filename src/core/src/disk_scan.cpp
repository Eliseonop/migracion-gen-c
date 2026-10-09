#include "disk_scan.hpp"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <system_error>
#include <utility>

#include "gc/core/io.hpp"
#include "py.hpp"

namespace gc::core::detail {

namespace fs = std::filesystem;

namespace {

// Las partes de una ruta en minúsculas, para compararlas como WindowsPath (sin las vacías
// de una barra final).
std::vector<std::string> lowered_parts(const fs::path& p) {
    std::vector<std::string> out;
    for (const fs::path& part : p) {
        const std::string s = path_to_utf8(part);
        if (!s.empty()) {
            out.push_back(py_lower(s));
        }
    }
    return out;
}

// root == target (si no es `strict`) o root en target.parents, sin distinguir mayúsculas.
bool inside(const fs::path& root, const fs::path& target, bool strict) {
    const auto r = lowered_parts(root);
    const auto t = lowered_parts(target);
    return t.size() >= r.size() + (strict ? 1 : 0) && std::equal(r.begin(), r.end(), t.begin());
}

} // namespace

std::optional<fs::path> dir_from_settings(const Json& settings, std::string_view key, const fs::path& default_dir) {
    std::error_code ec;
    const auto it = settings.find(key); // end() si no es un objeto
    if (it != settings.end() && it->is_string()) {
        const auto& text = it->get_ref<const std::string&>();
        // Con un NUL, Path.exists() da False; Windows cortaría la ruta en él.
        if (!text.empty() && !text.contains('\0')) {
            fs::path p = path_from_utf8(text);
            if (fs::is_directory(p, ec)) {
                return p;
            }
        }
    }
    if (fs::is_directory(default_dir, ec)) {
        return default_dir;
    }
    return std::nullopt;
}

std::vector<std::vector<std::string>> sorted_files(const fs::path& base,
                                                   const std::function<bool(std::string_view)>& keep) {
    struct Found {
        std::vector<std::string> key; // partes en minúsculas: el orden de sorted() de WindowsPath
        std::vector<std::string> parts;
    };
    std::vector<Found> found;
    std::error_code ec;
    fs::recursive_directory_iterator it(base, fs::directory_options::skip_permission_denied, ec);
    for (const fs::recursive_directory_iterator end{}; !ec && it != end; it.increment(ec)) {
        std::error_code ec_file;
        if (!it->is_regular_file(ec_file)) { // como is_file(): sigue los enlaces
            continue;
        }
        try {
            std::vector<std::string> all;
            for (const fs::path& part : it->path()) {
                all.push_back(path_to_utf8(part));
            }
            // Las últimas depth() + 1 partes son la ruta relativa a la carpeta.
            const auto depth = static_cast<std::size_t>(it.depth()) + 1;
            if (all.size() < depth || !keep(all.back())) {
                continue;
            }
            Found f;
            f.parts.assign(all.end() - static_cast<std::ptrdiff_t>(depth), all.end());
            for (const auto& p : f.parts) {
                f.key.push_back(py_lower(p));
            }
            found.push_back(std::move(f));
        } catch (const std::exception&) {
            continue; // un nombre que no es UTF-16 válido
        }
    }
    std::ranges::stable_sort(found, {}, &Found::key);
    std::vector<std::vector<std::string>> out;
    out.reserve(found.size());
    for (auto& f : found) {
        out.push_back(std::move(f.parts));
    }
    return out;
}

std::string clean_name(std::string_view text) {
    constexpr std::string_view kBad = R"(<>:"/\|?*)";
    std::string out(text);
    for (char& c : out) {
        if (static_cast<unsigned char>(c) < 0x20 || kBad.contains(c)) {
            c = '_';
        }
    }
    const auto first = out.find_first_not_of(" .");
    if (first == std::string::npos) {
        return {};
    }
    return out.substr(first, out.find_last_not_of(" .") - first + 1);
}

std::vector<std::string> sorted_subdirs(const fs::path& base) {
    struct Sub {
        std::string key;
        std::string name;
    };
    std::vector<Sub> subs;
    std::error_code ec;
    for (fs::directory_iterator it(base, ec), end; !ec && it != end; it.increment(ec)) {
        std::error_code ec_dir;
        if (!it->is_directory(ec_dir)) { // como is_dir(): sigue los enlaces
            continue;
        }
        try {
            std::string name = path_to_utf8(it->path().filename());
            subs.push_back({py_lower(name), std::move(name)});
        } catch (const std::exception&) {
            continue; // un nombre que no es UTF-16 válido
        }
    }
    std::ranges::stable_sort(subs, {}, &Sub::key);
    std::vector<std::string> out;
    out.reserve(subs.size());
    for (auto& s : subs) {
        out.push_back(std::move(s.name));
    }
    return out;
}

fs::path pathlib_join(const fs::path& base, const fs::path& p) {
    if (p.has_root_name()) {
        const bool same_drive = py_lower(path_to_utf8(p.root_name())) == py_lower(path_to_utf8(base.root_name()));
        if (!same_drive || p.has_root_directory()) {
            return p;
        }
        return base / p.relative_path();
    }
    if (p.has_root_directory()) {
        return base.root_name() / p;
    }
    return base / p;
}

std::optional<fs::path> resolve_inside(const fs::path& base, std::initializer_list<std::string_view> parts,
                                       bool strict) {
    if (std::ranges::any_of(parts, [](std::string_view part) { return part.contains('\0'); })) {
        return std::nullopt;
    }
    std::error_code ec;
    const fs::path root = fs::weakly_canonical(fs::absolute(base, ec), ec);
    if (ec) {
        return std::nullopt;
    }
    fs::path joined = root;
    for (const std::string_view part : parts) {
        joined = pathlib_join(joined, path_from_utf8(part));
    }
    fs::path target = fs::weakly_canonical(joined, ec);
    if (ec || !inside(root, target, strict)) {
        return std::nullopt;
    }
    return target;
}

bool resolved_inside(const fs::path& dir, const fs::path& target, bool strict) {
    std::error_code ec;
    const fs::path root = fs::weakly_canonical(fs::absolute(dir, ec), ec);
    return !ec && inside(root, target, strict);
}

std::optional<fs::path> resolve_inside(const fs::path& base, std::string_view rel) {
    return resolve_inside(base, {rel}, false);
}

} // namespace gc::core::detail
