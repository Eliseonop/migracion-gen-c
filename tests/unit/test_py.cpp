// Semántica de Python que la edición reproduce (src/core/src/py.hpp). Los valores
// esperados se midieron con el Python de video-yt.

#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "py.hpp"

using namespace gc::core::detail;

TEST_CASE("py_round redondea como round() de Python", "[py]") {
    CHECK(py_round(2.675, 2) == 2.67);   // 2.675 es 2.67499999… en binario
    CHECK(py_round(0.0625, 3) == 0.062); // empate exacto: al par
    CHECK(py_round(0.0005, 3) == 0.001); // 0.000500000000000000010…
    CHECK(py_round(1.0005, 3) == 1.0);   // 1.000499999…
    CHECK(py_round(2.5, 0) == 2.0);
    CHECK(py_round(3.5, 0) == 4.0);
    CHECK(py_round(13.3333 - 10, 3) == 3.333);
    CHECK(py_round(0.1 + 0.2, 3) == 0.3);
    CHECK(py_round(123456.78951, 4) == 123456.7895);
    CHECK(py_round(1e13 + 0.12345, 3) == 10000000000000.123);
    CHECK(py_round(-7.0005, 3) == -7.0);
    CHECK(py_round(10.33335, 3) == 10.333);

    const double neg_zero = py_round(-0.0004, 3);
    CHECK(neg_zero == 0.0);
    CHECK(std::signbit(neg_zero)); // Python devuelve -0.0
    CHECK(std::isinf(py_round(INFINITY, 3)));
}

TEST_CASE("py_repr escribe los reales como Python", "[py]") {
    CHECK(py_repr(1.0) == "1.0");
    CHECK(py_repr(0.1) == "0.1");
    CHECK(py_repr(1e-05) == "1e-05");
    CHECK(py_repr(-0.0) == "-0.0");
    CHECK(py_repr(INFINITY) == "inf");
}

TEST_CASE("py_strip y py_slice trabajan con caracteres, no con bytes", "[py]") {
    CHECK(py_strip("  Principal  ") == "Principal");
    CHECK(py_strip("\x1c a \x1f") == "a");
    CHECK(py_strip(" \t ").empty());
    CHECK(py_slice("Pista de diálogo principal — versión ñandú 🎬 final", 32) == "Pista de diálogo principal — ver");
    CHECK(py_slice("🎬🎬", 1) == "🎬");
    CHECK(py_slice("abc", 10) == "abc");
}

TEST_CASE("float() e int() de Python sobre valores JSON", "[py]") {
    CHECK(py_float(gc::Json("1_000.5")) == 1000.5);
    CHECK(py_float(gc::Json(true)) == 1.0);
    CHECK_FALSE(py_float(gc::Json(nullptr)));
    CHECK(py_int(gc::Json(29.97)) == 29);
    CHECK(py_int(gc::Json("720")) == 720);
    CHECK_FALSE(py_int(gc::Json("7.0"))); // int("7.0") falla en Python
    CHECK_FALSE(py_number(gc::Json("5")));
}
