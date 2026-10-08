#pragma once

// Carpetas de material en disco como las ve pathlib en Windows: la carpeta que dicen los
// ajustes, el recorrido de sorted(base.rglob("*")) y la resolución que no deja salir de la
// carpeta. Lo comparten la biblioteca de SFX (sfx_disk) y las colecciones (collections).

#include <filesystem>
#include <functional>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "gc/core/json.hpp"

namespace gc::core::detail {

// La carpeta de `settings[key]` si es una carpeta que existe; si no, `default_dir` si existe;
// si no, ninguna (get_base de sfx.py, get_root de collections.py). Un valor que no es texto
// se ignora: Python fallaría en Path(x).
[[nodiscard]] std::optional<std::filesystem::path> dir_from_settings(const Json& settings, std::string_view key,
                                                                     const std::filesystem::path& default_dir);

// Los archivos de sorted(base.rglob("*")) (is_file(): sigue los enlaces) cuyo nombre acepta
// `keep`, cada uno como sus partes relativas a `base`, en el orden de WindowsPath: por partes
// y con el lower() de Python. Se saltan los nombres que no son UTF-16 válido.
[[nodiscard]] std::vector<std::vector<std::string>> sorted_files(const std::filesystem::path& base,
                                                                 const std::function<bool(std::string_view)>& keep);

// base / p de pathlib en Windows: una ruta con otra unidad (sin distinguir mayúsculas) o
// absoluta sustituye a la base; una raíz sin unidad (\x) se queda la unidad de la base; la misma
// unidad sin raíz (C:x) se añade a la base.
[[nodiscard]] std::filesystem::path pathlib_join(const std::filesystem::path& base, const std::filesystem::path& p);

// (base.resolve() / rel).resolve() si queda dentro de la carpeta: el objetivo es la carpeta o
// está debajo, sin distinguir mayúsculas (WindowsPath). nullopt si sale (con «..» o una ruta
// absoluta), si `rel` lleva un NUL o si falla. No mira si existe.
[[nodiscard]] std::optional<std::filesystem::path> resolve_inside(const std::filesystem::path& base,
                                                                  std::string_view rel);
// Lo mismo con base / parts[0] / parts[1]… (cada parte se une como en pathlib). Con `strict`, la
// propia carpeta no vale (`base not in target.parents`).
[[nodiscard]] std::optional<std::filesystem::path> resolve_inside(const std::filesystem::path& base,
                                                                  std::initializer_list<std::string_view> parts,
                                                                  bool strict);

} // namespace gc::core::detail
