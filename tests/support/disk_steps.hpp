#pragma once

// Utilidades de los tests de paridad que repiten pasos sobre una carpeta temporal (SFX en
// disco, colecciones): escribir archivos, «{TMP}» y la ruta que devuelve un resolve.

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "gc/core/io.hpp"
#include "gc/core/json.hpp"
#include "py.hpp"

namespace gc::test {

inline std::string replace_all(std::string s, std::string_view from, std::string_view to) {
    for (auto pos = s.find(from); pos != std::string::npos; pos = s.find(from, pos + to.size())) {
        s.replace(pos, from.size(), to);
    }
    return s;
}

// `value` con `from` cambiado por `to` en todos sus textos (valores y claves). Sin pasar por el
// texto del JSON, que perdería los NaN y los infinitos.
inline Json replace_in_strings(const Json& value, std::string_view from, std::string_view to) {
    if (value.is_string()) {
        return replace_all(value.get<std::string>(), from, to);
    }
    if (value.is_array()) {
        Json out = Json::array();
        for (const Json& v : value) {
            out.push_back(replace_in_strings(v, from, to));
        }
        return out;
    }
    if (value.is_object()) {
        Json out = Json::object();
        for (auto it = value.begin(); it != value.end(); ++it) {
            out[replace_all(it.key(), from, to)] = replace_in_strings(*it, from, to);
        }
        return out;
    }
    return value;
}

// El contenido de un paso: el texto tal cual o el JSON escrito.
inline std::string text_of(const Json& content) {
    return content.is_string() ? content.get<std::string>() : content.dump();
}

// Escribe `bytes` en `file` (y crea sus carpetas). false si no pudo.
inline bool write_file(const std::filesystem::path& file, const std::string& bytes) {
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << bytes;
    out.close();
    return static_cast<bool>(out);
}

inline std::string read_file(const std::filesystem::path& file) {
    const std::ifstream in(file, std::ios::binary);
    std::stringstream text;
    text << in.rdbuf();
    return text.str();
}

// La ruta de `target` relativa a la carpeta `base` (ya resuelta), con «/» y en minúsculas:
// Python da las mayúsculas del disco y std::filesystem las pedidas. «.» si es la carpeta.
inline std::string lowered_relative(const std::filesystem::path& target, const std::filesystem::path& base) {
    const auto parts = [](const std::filesystem::path& p) {
        std::vector<std::string> out;
        for (const auto& part : p) {
            if (!part.empty()) {
                out.push_back(core::path_to_utf8(part));
            }
        }
        return out;
    };
    const auto t = parts(target);
    std::string rel;
    for (std::size_t k = parts(std::filesystem::weakly_canonical(base)).size(); k < t.size(); ++k) {
        rel += (rel.empty() ? "" : "/") + t[k];
    }
    return core::detail::py_lower(rel.empty() ? "." : rel);
}

} // namespace gc::test
