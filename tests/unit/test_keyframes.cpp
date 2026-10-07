// Casos de keyframes que las fixtures no pueden expresar (JSON no tiene NaN).

#include <catch2/catch_test_macros.hpp>

#include <limits>

#include "gc/core/keyframes.hpp"

using gc::Json;
using namespace gc::core;

TEST_CASE("interp_items: un NaN del respaldo vale lo de un clip vacío (_merge de Python)", "[keyframes]") {
    // Python: interp_items([], 0, {..., "x": nan, "zoom": nan, "volume": nan, "y": 0.25})
    //         → x 0.5, y 0.25, zoom 1.0, volume 1.0
    constexpr double nan = std::numeric_limits<double>::quiet_NaN();
    KfProps base = static_props(TimelineClip{});
    base.set("x", nan);
    base.set("zoom", nan);
    base.set("volume", nan);
    base.set("y", 0.25);

    const KfProps p = interp_items({}, 0.0, base);
    CHECK(p.get("x") == 0.5);
    CHECK(p.get("y") == 0.25);
    CHECK(p.get("zoom") == 1.0);
    CHECK(p.get("volume") == 1.0);

    // Lo animado manda; el resto sigue saliendo del respaldo corregido.
    const KfProps q = interp_items({Json{{"t", 0}, {"props", {{"x", 0.9}}}}}, 0.0, base);
    CHECK(q.get("x") == 0.9);
    CHECK(q.get("zoom") == 1.0);
}
