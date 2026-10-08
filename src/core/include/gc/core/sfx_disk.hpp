#pragma once

// La biblioteca de efectos de sonido en disco (espejo de la parte de lectura de
// backend/app/sfx.py): una carpeta con un sfx_library.json (nombre, categoría, carpeta y
// uso típico de cada sonido) o, si no lo hay o no se puede leer, los audios que contiene.
//
// La carpeta activa es la de `sfx_folder` en los ajustes o, si no existe, la de fábrica
// (sfx_base). De ella salen el índice (sfx_load_index), la búsqueda (sfx_search) y la
// biblioteca que usan las recetas (load_sfx_library → set_sfx_library). Dar de alta,
// editar y crear categorías (add_sound, update_sound, create_category) cambia archivos
// y llega con la pantalla de la biblioteca de SFX (F5).

#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include "gc/core/error.hpp"
#include "gc/core/json.hpp"
#include "gc/core/sfx_library.hpp"

namespace gc::core {

inline constexpr std::string_view kSfxFavorites = "favoritos"; // categoría de los marcados con estrella
inline constexpr std::string_view kSfxIndexFile = "sfx_library.json";

// ¿Es un audio de la biblioteca por su extensión (.mp3, .wav, .ogg, .m4a, .aac, .flac, sin
// distinguir mayúsculas)?
[[nodiscard]] bool is_sfx_audio(std::string_view filename);

// get_base: la carpeta de `sfx_folder` de los ajustes si es una carpeta que existe; si no,
// `default_dir` si existe; si no, ninguna. (Un `sfx_folder` que no es texto se ignora:
// Python fallaría.)
[[nodiscard]] std::optional<std::filesystem::path> sfx_base(const Json& settings,
                                                             const std::filesystem::path& default_dir);

// _entry: el sonido como lo da la API ({id, name, file, folder, category, uso, url}). Falla
// donde Python lanzaría (un nombre de archivo que no es texto y hace falta).
[[nodiscard]] Result<Json> sfx_entry(const Json& folder, const Json& filename, const Json& category = "",
                                     const Json& uso = "", const Json& name = nullptr);

// _scan: los audios de la carpeta y sus subcarpetas, en el orden de sorted() de rutas de
// Windows (por partes y sin distinguir mayúsculas, con el lower() de Python).
[[nodiscard]] std::vector<Json> sfx_scan(const std::filesystem::path& base);

// load_index: {available, base, categories, items}, con `base` como str(Path) de Python. Si
// sfx_library.json falla en cualquier punto, los sonidos salen del escaneo y las categorías
// se quedan con las que dio tiempo a leer, como en Python. (Python lee NaN, Infinity y
// 1e999; aquí ese índice falla y se escanea.)
[[nodiscard]] Json sfx_load_index(const std::optional<std::filesystem::path>& base);

// Ids marcados con estrella en los ajustes (favorites.sfx, cada uno como str()).
[[nodiscard]] std::set<std::string> sfx_favorite_ids(const Json& settings);

// filter_sfx_items: por categoría («favoritos» = los marcados) y por palabras (todas tienen
// que estar en el nombre, la categoría, el uso o la carpeta, sin distinguir mayúsculas).
[[nodiscard]] std::vector<Json> filter_sfx_items(const std::vector<Json>& items, std::string_view q,
                                                 std::string_view category, const std::set<std::string>& favorites);
// with_favorites_category: la categoría «Favoritos» (con su cuenta) delante de las demás.
[[nodiscard]] std::vector<Json> with_favorites_category(const std::vector<Json>& categories,
                                                        const std::vector<Json>& items,
                                                        const std::set<std::string>& favorites);

// search: {available, base, categories, total, items (como mucho `limit`)}.
[[nodiscard]] Json sfx_search(const std::optional<std::filesystem::path>& base, const Json& settings,
                              std::string_view q = "", std::string_view category = "", std::size_t limit = 300);

// resolve: la ruta del sonido `relpath` dentro de la carpeta, si existe y no se sale de
// ella (con «..» o una ruta absoluta).
[[nodiscard]] std::optional<std::filesystem::path> sfx_resolve(const std::optional<std::filesystem::path>& base,
                                                               std::string_view relpath);

// La biblioteca que usan las recetas (como sound_design.propose): todos los sonidos
// (search("", 100000)) y, de cada uno que se encuentra en disco, su duración redondeada a
// ms con `probe` (ffprobe en Python; sin duración, 0).
using SfxProbe = std::function<std::optional<double>(const std::filesystem::path&)>;
[[nodiscard]] SfxLibrary load_sfx_library(const std::optional<std::filesystem::path>& base, const Json& settings,
                                          const SfxProbe& probe);

} // namespace gc::core
