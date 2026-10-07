// Semántica de Python que la edición reproduce (src/core/src/py.hpp). Los valores
// esperados se midieron con el Python de video-yt.

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <utility>

#include "json_reader.hpp"
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

// Valores medidos con Python 3.14 en Windows (repr, Path.stem y la clase de palabra de re).
TEST_CASE("py_repr de valores JSON, con las comillas y escapes de Python", "[py]") {
    using gc::Json;
    CHECK(py_repr(Json::parse(R"({"a": 1, "b": [1.0, null, true]})")) == "{'a': 1, 'b': [1.0, None, True]}");
    CHECK(py_repr(Json("a'b")) == R"("a'b")");
    CHECK(py_repr(Json(R"(a"b)")) == R"('a"b')");
    CHECK(py_repr(Json(R"(a'b"c)")) == R"('a\'b"c')");
    CHECK(py_repr(Json("tab\there")) == R"('tab\there')");
    CHECK(py_repr(Json("nbsp x")) == R"('nbsp\xa0x')");
    CHECK(py_repr(Json("shy­x")) == R"('shy\xadx')");
    CHECK(py_repr(Json("zw\u200bx")) == "'zw\\u200bx'");
    CHECK(py_repr(Json("ñandú \U0001F3AC")) == "'ñandú \U0001F3AC'");
    CHECK(py_repr(Json("\x7f")) == R"('\x7f')");
    CHECK(py_repr(Json("\u2028")) == "'\\u2028'");
    CHECK(py_repr(Json("\ue000")) == "'\\ue000'");
    CHECK(py_repr(Json(R"(back\slash)")) == R"('back\\slash')");
    CHECK(py_repr(Json(std::string("\0", 1))) == R"('\x00')");
    CHECK(py_repr(Json("\U000E0001")) == R"('\U000e0001')");
    CHECK(py_str(Json(1e16)) == "1e+16");
    CHECK(py_str(Json(-0.0)) == "-0.0");
    CHECK(py_str(Json::array({"x", 1})) == "['x', 1]");
}

TEST_CASE("py_path_stem es Path(x).stem de Python 3.14 en Windows", "[py]") {
    const std::pair<const char*, const char*> cases[] = {
        {"x.", "x"},           {".hidden", ".hidden"},    {"x..y", "x."},       {"C:foo.wav", "foo"},
        {"a/b.wav", "b"},      {R"(a\b.mp3)", "b"},       {R"(\\srv\share\f.ogg)", "f"},
        {"..", ".."},          {".", ""},                 {"", ""},             {"1:z.wav", "z"},
        {"dir/", "dir"},       {"whoosh.tar.gz", "whoosh.tar"},                 {"{'a': 1}", "{'a': 1}"},
        {"a/./b.c", "b"},      {"sonido final", "sonido final"},                {R"(C:\x\)", "x"},
        {R"(\\srv\share)", ""}, {"//srv/share/k.wav", "k"}, {"a:b:c.wav", "b:c"}, {"x/..", ".."},
        {"a.b/", "a"},         {"  .wav", "  "},          {"a. b", "a"},        {"/:a", ":a"},
        {"\xC3\xA9:foo.wav", "foo"}, {"\xE6\x97\xA5:a.b", "a"}};
    for (const auto& [in, want] : cases) {
        INFO(in);
        CHECK(py_path_stem(in) == want);
    }
}

TEST_CASE("py_word_char sigue a la clase de palabra de Python (str.isalnum() o _)", "[py]") {
    CHECK(py_word_char(U'a'));
    CHECK(py_word_char(U'_'));
    CHECK(py_word_char(U'7'));
    CHECK_FALSE(py_word_char(U' '));
    CHECK_FALSE(py_word_char(U'-'));
    CHECK(py_word_char(U'é'));
    CHECK(py_word_char(U'ñ'));
    CHECK(py_word_char(U'²'));
    CHECK(py_word_char(U'½'));
    CHECK(py_word_char(U'Ω'));
    CHECK_FALSE(py_word_char(U'\u00a0'));
    CHECK_FALSE(py_word_char(U'—'));
    CHECK_FALSE(py_word_char(U'\u0301')); // marca combinante
    CHECK_FALSE(py_word_char(U'🎵'));
}
