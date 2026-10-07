#pragma once

// Biblioteca de efectos de sonido y emparejamiento por palabras clave (espejo de
// sfx.search/resolve y de norm, score_sfx y match_sounds de sound_design.py).
//
// La app carga la biblioteca (lista de sonidos y la duración de cada archivo) con
// set_sfx_library; sin biblioteca, no hay sonidos. Las recetas la usan para añadir
// sonido sin leer el disco desde la lógica de edición.

#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "gc/core/json.hpp"

namespace gc::core {

struct SfxLibrary {
    std::vector<Json> items;                 // [{id, name, category?, uso?, folder?, url?}]
    std::map<std::string, double> durations; // id → s (los que se pueden abrir; sonda de ffprobe)
};

void set_sfx_library(std::shared_ptr<const SfxLibrary> library);
[[nodiscard]] std::shared_ptr<const SfxLibrary> sfx_library(); // nunca nulo

// Minúsculas, sin tildes y solo letras y números separados por un espacio (NFKD + ASCII;
// la tabla NFKD → ASCII se genera desde unicodedata, Unicode 16).
[[nodiscard]] std::string sfx_norm(const Json& text);

// Parecido de un sonido con las palabras clave: frase entera en el nombre > palabra
// suelta en el nombre > en su categoría, uso o carpeta.
[[nodiscard]] double sfx_score(const Json& item, const std::vector<Json>& keywords);

// (emparejados, sin sonido en la biblioteca), sin repetir un mismo sonido. Cada
// emparejado es el sonido pedido con {sfx: {id, name, url, category}, score, alternatives}.
[[nodiscard]] std::pair<std::vector<Json>, std::vector<Json>> match_sounds(const std::vector<Json>& sounds,
                                                                           const std::vector<Json>& items);

} // namespace gc::core
