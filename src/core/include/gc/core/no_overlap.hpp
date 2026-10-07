#pragma once

// Una pista NUNCA tiene dos clips en el mismo instante (espejo de no_overlap.py).
//
// Tras cada operación, settle_timeline resuelve lo que esa operación haya montado:
//   - un clip que CRECE por la derecha empuja a los de detrás en su pista;
//   - un clip nuevo o movido que cae encima de otro sube a la pista vecina libre
//     de su tipo (encima en la pila, debajo en el audio) o a una nueva;
//   - las pistas magnéticas se empaquetan: sin huecos desde 0.
// Los solapes que ya traía el proyecto no se tocan si esos clips no cambian.

#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#include "gc/core/ids.hpp"
#include "gc/core/model.hpp"

namespace gc::core {

// Solape mínimo que cuenta (s): los redondeos de duración no mueven nada.
inline constexpr double kSettleEps = 0.05;

struct Settled {
    Timeline timeline;
    std::vector<std::string> created_tracks;
    std::vector<std::string> notes;
};

// Clips nuevos o cuyo hueco (pista, inicio o fin) cambió entre `before` y `after`.
[[nodiscard]] std::unordered_set<std::string> touched_clip_ids(const Timeline& before, const Timeline& after);

// Empaqueta la pista en el sitio: orden por inicio y sin huecos desde 0. En un empate
// van delante los de `priority` (recién soltados). true si cambió algo.
bool pack_magnetic(Timeline& timeline, std::string_view track_id, const std::unordered_set<std::string>& priority);

// nullopt = no había nada que resolver (`after` vale tal cual).
[[nodiscard]] std::optional<Settled> settle_timeline(const Timeline& before, const Timeline& after, IdSource& ids,
                                                     double eps = kSettleEps);

} // namespace gc::core
