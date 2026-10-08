#pragma once

// Las colecciones de material (espejo de backend/app/collections.py): una carpeta raíz
// (`material_root` en los ajustes o, si no existe, la de fábrica: <assets>/material) cuyas
// subcarpetas directas son colecciones. Los archivos de medio de cada una (también los de
// sus subcarpetas) se usan en su sitio, sin copiarlos al proyecto, con el `filename`
// «<colección>/<ruta>». Las preferencias de cada colección (activa, favorita y etiqueta)
// van en los ajustes, en `collections_meta`.
//
// Los errores que ve el usuario llevan el texto de Python: «Colección no válida.», «No hay
// carpeta raíz de material.», «Ponle un nombre a la carpeta.» (InvalidArgument, el
// ValueError de Python) y «Carpeta no encontrada.» (NotFound, el LookupError).

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "gc/core/error.hpp"
#include "gc/core/json.hpp"
#include "gc/core/settings.hpp"

namespace gc::core {

// kind_for: «video», «image», «gif» o «audio» por la extensión (sin distinguir mayúsculas);
// nullopt si no es un medio que se use.
[[nodiscard]] std::optional<std::string_view> collection_kind(std::string_view filename);

// get_root: la carpeta de `material_root` si existe; si no, `default_dir` si existe; si no,
// ninguna. (Un `material_root` que no es texto se ignora: Python fallaría.)
[[nodiscard]] std::optional<std::filesystem::path> collections_root(const Json& settings,
                                                                    const std::filesystem::path& default_dir);

// set_root: guarda `path` (sin espacios alrededor, como str(Path)) en `material_root` si es
// una carpeta que existe y lo devuelve; si no, nada (y no guarda).
[[nodiscard]] Result<std::optional<std::string>> set_collections_root(SettingsFile& settings, std::string_view path);

// _safe_collection_id: el id es el nombre de una subcarpeta directa de la raíz, sin
// espacios alrededor ni separadores, y no «.» ni «..». Si no, «Colección no válida.».
[[nodiscard]] Result<std::string> safe_collection_id(std::string_view raw);

// set_prefs: mezcla las preferencias de una colección (lo que no llega se queda; una
// etiqueta vacía se borra), las guarda en `collections_meta` y las devuelve.
[[nodiscard]] Result<Json> set_collection_prefs(SettingsFile& settings, std::string_view collection_id,
                                                std::optional<bool> enabled, std::optional<bool> favorite,
                                                std::optional<std::string_view> label);

// _entry: {id, collection, name, kind, filename, url} de la ruta `rel` (relativa a la
// colección, con «/» o «\»).
[[nodiscard]] Json collection_entry(std::string_view collection, std::string_view rel);

// _scan_dir: los medios de una colección y sus subcarpetas, en el orden de sorted() de
// rutas de Windows. Vacío si la carpeta no existe.
[[nodiscard]] std::vector<Json> collection_scan(const std::filesystem::path& base, std::string_view collection);

// list_collections: {available, root, collections}, con las favoritas primero y después
// por nombre (sin distinguir mayúsculas). Error si una etiqueta no es texto (Python falla
// al ordenarlas).
[[nodiscard]] Result<Json> list_collections(const std::optional<std::filesystem::path>& base, const Json& settings);

// _match: todas las palabras de `q` están en «<nombre> <colección>» (sin distinguir
// mayúsculas). Un `q` vacío vale para todo.
[[nodiscard]] bool collection_match(const Json& item, std::string_view q);

// search: {available, root, counts, total, items, collections} en las colecciones activas
// (o solo en `collection`). `counts` cuenta por tipo lo que casa con `q` antes de filtrar
// por `kind`; `items` lleva `favorite` y se corta como items[:limit] (negativo, desde el final).
[[nodiscard]] Result<Json> collections_search(const std::optional<std::filesystem::path>& base,
                                              const Json& settings, std::string_view q = "",
                                              std::string_view kind = "", std::string_view collection = "",
                                              std::int64_t limit = 500);

// create_collection: crea una carpeta vacía en la raíz con el nombre saneado (lo que
// Windows no admite pasa a «_», sin espacios ni puntos en los extremos) y, si ya existe,
// «<nombre> 2», «<nombre> 3»… Devuelve su id.
[[nodiscard]] Result<std::string> create_collection(const std::optional<std::filesystem::path>& base,
                                                    std::string_view name);

// Un archivo que llega a una colección: el nombre que trae (cualquier valor, como en la API)
// y quien escribe su contenido en `target` (un archivo que todavía no existe).
struct IncomingFile {
    Json name;
    std::function<Result<void>(const std::filesystem::path& target)> write;
};

// add_files: guarda cada archivo de medio en la carpeta de la colección con su nombre
// saneado y, si ya existe, «<stem>_2<ext>», «<stem>_3<ext>»… Devuelve {saved: [entradas],
// errors: [{file, error}]}; los que no son medio van a `errors`. Error si el id no vale o la
// carpeta no existe, y si falla una escritura (los anteriores se quedan guardados).
[[nodiscard]] Result<Json> add_collection_files(const std::optional<std::filesystem::path>& base,
                                                std::string_view collection, const std::vector<IncomingFile>& files);

// resolve: la ruta de un `filename` de colección («<colección>/<ruta>») si es un archivo
// dentro de la raíz; nada si sale de ella, no existe o es una carpeta.
[[nodiscard]] std::optional<std::filesystem::path> collection_resolve(const std::optional<std::filesystem::path>& base,
                                                                      std::string_view ref);

} // namespace gc::core
