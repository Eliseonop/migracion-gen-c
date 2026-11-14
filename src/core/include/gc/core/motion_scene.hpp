#pragma once

// Lo de backend/app/motion/scene.py (Generar Escena) que ya usa el núcleo: los ritmos (PACES), que dan
// los segundos medios por beat de skeleton_beats en scene_direction_pack.

#include <array>
#include <optional>
#include <string_view>

namespace gc::core::motion {

struct Pace {
    std::string_view key;
    double seconds; // segundos medios por beat
};

// PACES, en su orden.
inline constexpr std::array<Pace, 3> kPaces{{{"calmo", 3.4}, {"medio", 2.5}, {"dinamico", 1.7}}};
inline constexpr std::string_view kDefaultPace = "medio";

// PACES.get(key): nullopt si no es uno de los ritmos.
[[nodiscard]] constexpr std::optional<double> pace_seconds(std::string_view key) {
    for (const Pace& p : kPaces) {
        if (p.key == key) {
            return p.seconds;
        }
    }
    return std::nullopt;
}

} // namespace gc::core::motion
