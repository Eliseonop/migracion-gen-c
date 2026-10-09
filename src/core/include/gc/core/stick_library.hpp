#pragma once

// Los personajes stick (espejo de backend/app/stick_library.py): una colección de material
// con un manifiesto `stick.json` es un personaje, y cada uno de sus vídeos e imágenes (y GIF)
// lleva una expresión (feliz, triste…). La expresión sale, por este orden, de su entrada en
// `items` del manifiesto, de la subcarpeta donde está (comparada con el id y la etiqueta de
// cada expresión, sin tildes ni mayúsculas; una subcarpeta que no es ninguna es una categoría
// propia) o es «otros». Cambiar la expresión no mueve el archivo: se apunta en el manifiesto.
//
// `base` es la raíz de las colecciones (collections_root). Los errores que ve el usuario
// llevan el texto de Python: «Stick no encontrado.» y «Recurso no encontrado en el stick.»
// (NotFound, el LookupError), «Colección no válida.», «No hay carpeta raíz de material. Elige
// una en la Biblioteca.», «Ponle un nombre al stick.», «Expresión desconocida: <id>» y «Solo se
// categorizan vídeos o imágenes.» (InvalidArgument, el ValueError). Un manifiesto con valores
// con los que Python falla (un `order` que no es entero, un nombre que no es texto, un id de
// expresión que es una lista…) da InvalidArgument con otro texto.
//
// Divergencia: un `order` de más de 64 bits es un error.

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "gc/core/collections.hpp"
#include "gc/core/error.hpp"
#include "gc/core/json.hpp"

namespace gc::core {

// list_sticks: {available, sticks}; cada uno {id, name, emoji, order, chroma_color, kinds,
// count, cover_url}, por `order` (999 si no lo tiene) y luego por nombre sin mayúsculas.
[[nodiscard]] Result<Json> list_sticks(const std::optional<std::filesystem::path>& base);

// stick_detail: {id, name, emoji, chroma_color, kinds, video, image, expressions, groups}.
// `video` e `image` son los grupos no vacíos de cada tipo; `groups`, todas las expresiones en
// orden (también las vacías) con vídeos e imágenes juntos.
[[nodiscard]] Result<Json> stick_detail(const std::optional<std::filesystem::path>& base, std::string_view cid);

// create_stick: carpeta con el nombre en minúsculas y sin tildes («Mi Stick» → mi_stick, y
// mi_stick_2… si ya existe), una subcarpeta por expresión y el manifiesto. `name`, `emoji` y
// `chroma_color` llegan como en la API (cualquier valor). Devuelve su detalle.
[[nodiscard]] Result<Json> create_stick(const std::optional<std::filesystem::path>& base, const Json& name,
                                        const Json& emoji = Json(""), const Json& chroma_color = Json("#00FF00"));

// add_files: guarda los vídeos e imágenes en la subcarpeta de `expression` (la que la
// represente o una nueva con su etiqueta) con su nombre saneado y, si ya existe,
// «<stem>_2<ext>»… Devuelve {saved: [rutas relativas al stick], errors: [{file, error}], detail}.
[[nodiscard]] Result<Json> add_stick_files(const std::optional<std::filesystem::path>& base, std::string_view cid,
                                           const std::vector<IncomingFile>& files,
                                           const Json& expression = Json("otros"));

// set_expression: apunta la expresión de un archivo del stick en `items` del manifiesto (una
// categoría propia pasa también a `expressions`). Devuelve el detalle.
[[nodiscard]] Result<Json> set_stick_expression(const std::optional<std::filesystem::path>& base,
                                                std::string_view cid, const Json& rel, const Json& expression);

// Utilidades de nombres (para los tests): _norm, _slug y _pretty_title.
[[nodiscard]] std::string stick_norm(const Json& text);
[[nodiscard]] std::string stick_slug(const Json& text, std::string_view fallback = "stick");
[[nodiscard]] std::string stick_pretty_title(std::string_view stem);

} // namespace gc::core
