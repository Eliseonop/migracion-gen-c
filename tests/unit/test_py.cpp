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

TEST_CASE("py_lower es str.lower() de Python (valores medidos en Python)", "[py]") {
    // Bytes escapados: no dependen de la codificación del fuente.
    CHECK(py_lower("\xC3\x80\xC3\x89\xC3\x8E\xC3\xB5\xC3\x9C") == "\xC3\xA0\xC3\xA9\xC3\xAE\xC3\xB5\xC3\xBC"); // \xc0\xc9\xce\xf5\xdc
    CHECK(py_lower("\xC4\xB0\x73\x74\x61\x6E\x62\x75\x6C") == "\x69\xCC\x87\x73\x74\x61\x6E\x62\x75\x6C"); // \u0130stanbul
    CHECK(py_lower("\xCE\x9F\xCE\x94\xCE\x9F\xCE\xA3") == "\xCE\xBF\xCE\xB4\xCE\xBF\xCF\x82"); // \u039f\u0394\u039f\u03a3
    CHECK(py_lower("\xCE\xA3\xCE\x91\xCE\xA3\x20\xCE\x9F\xCE\x94\xCE\x9F\xCE\xA3\x2E") == "\xCF\x83\xCE\xB1\xCF\x82\x20\xCE\xBF\xCE\xB4\xCE\xBF\xCF\x82\x2E"); // \u03a3\u0391\u03a3 \u039f\u0394\u039f\u03a3.
    CHECK(py_lower("\xCE\xA3") == "\xCF\x83"); // \u03a3
    CHECK(py_lower("\x41\xCE\xA3") == "\x61\xCF\x82"); // A\u03a3
    CHECK(py_lower("\x41\xCE\xA3\x27") == "\x61\xCF\x82\x27"); // A\u03a3'
    CHECK(py_lower("\x41\xCE\xA3\x27\x42") == "\x61\xCF\x83\x27\x62"); // A\u03a3'B
    CHECK(py_lower("\x41\x27\xCE\xA3") == "\x61\x27\xCF\x82"); // A'\u03a3
    CHECK(py_lower("\xCE\x91\xCE\xA3\xCC\x81") == "\xCE\xB1\xCF\x82\xCC\x81"); // \u0391\u03a3\u0301
    CHECK(py_lower("\xE1\xBA\x9E") == "\xC3\x9F"); // \u1e9e
    CHECK(py_lower("\xE2\x85\xAB") == "\xE2\x85\xBB"); // \u216b
    CHECK(py_lower("\xC7\x85") == "\xC7\x86"); // \u01c5
    CHECK(py_lower("\x4D\xC4\xB0\x58\x45\x44\x20\xC3\x9F") == "\x6D\x69\xCC\x87\x78\x65\x64\x20\xC3\x9F"); // M\u0130XED \xdf
    CHECK(py_lower("\xEF\xBC\xA1\xEF\xBC\xA2\xEF\xBC\xA3") == "\xEF\xBD\x81\xEF\xBD\x82\xEF\xBD\x83"); // \uff21\uff22\uff23
    CHECK(py_lower("\xE1\x8E\xA0") == "\xEA\xAD\xB0"); // \u13a0
    CHECK(py_lower("\xE1\xB2\x90") == "\xE1\x83\x90"); // \u1c90
    CHECK(py_lower("\xCE\xA3\xCE\xA3") == "\xCF\x83\xCF\x82"); // \u03a3\u03a3
    CHECK(py_lower("\x41\xC2\xAD\xCE\xA3") == "\x61\xC2\xAD\xCF\x82"); // A\xad\u03a3
    CHECK(py_lower("\x41\xC2\xAD\xCE\xA3\xC2\xAD\x42") == "\x61\xC2\xAD\xCF\x83\xC2\xAD\x62"); // A\xad\u03a3\xadB
    CHECK(py_lower("\x31\xCE\xA3") == "\x31\xCF\x83"); // 1\u03a3
    CHECK(py_lower("\xCE\xA3\x31") == "\xCF\x83\x31"); // \u03a31
    CHECK(py_lower("\xF0\x90\x90\x80") == "\xF0\x90\x90\xA8"); // \U00010400
    CHECK(py_lower("\x61\x62\x63") == "\x61\x62\x63"); // abc
    CHECK(py_lower("").empty()); // 
    // Bytes que no son UTF-8 válido: se copian.
    CHECK(py_lower("A\xFF\xFE") == "a\xFF\xFE");
}

TEST_CASE("py_path_name, py_path_suffix y py_quote son los de Python 3.14 en Windows", "[py]") {
    // Valores medidos con el Python de video-yt (bytes escapados).
    CHECK(py_path_name("\x78\x2E\x6D\x70\x33") == "\x78\x2E\x6D\x70\x33");
    CHECK(py_path_suffix("\x78\x2E\x6D\x70\x33") == "\x2E\x6D\x70\x33");
    CHECK(py_path_stem("\x78\x2E\x6D\x70\x33") == "\x78");
    CHECK(py_path_name("\x78\x2E") == "\x78\x2E");
    CHECK(py_path_suffix("\x78\x2E") == "\x2E");
    CHECK(py_path_stem("\x78\x2E") == "\x78");
    CHECK(py_path_name("\x2E\x6D\x70\x33") == "\x2E\x6D\x70\x33");
    CHECK(py_path_suffix("\x2E\x6D\x70\x33").empty());
    CHECK(py_path_stem("\x2E\x6D\x70\x33") == "\x2E\x6D\x70\x33");
    CHECK(py_path_name("\x78\x2E\x4D\x50\x33") == "\x78\x2E\x4D\x50\x33");
    CHECK(py_path_suffix("\x78\x2E\x4D\x50\x33") == "\x2E\x4D\x50\x33");
    CHECK(py_path_stem("\x78\x2E\x4D\x50\x33") == "\x78");
    CHECK(py_path_name("\x61\x2E\x62\x2E\x77\x61\x76") == "\x61\x2E\x62\x2E\x77\x61\x76");
    CHECK(py_path_suffix("\x61\x2E\x62\x2E\x77\x61\x76") == "\x2E\x77\x61\x76");
    CHECK(py_path_stem("\x61\x2E\x62\x2E\x77\x61\x76") == "\x61\x2E\x62");
    CHECK(py_path_name("\x78") == "\x78");
    CHECK(py_path_suffix("\x78").empty());
    CHECK(py_path_stem("\x78") == "\x78");
    CHECK(py_path_name("\x2E\x2E") == "\x2E\x2E");
    CHECK(py_path_suffix("\x2E\x2E").empty());
    CHECK(py_path_stem("\x2E\x2E") == "\x2E\x2E");
    CHECK(py_path_name("\x78\x2E\x2E") == "\x78\x2E\x2E");
    CHECK(py_path_suffix("\x78\x2E\x2E") == "\x2E");
    CHECK(py_path_stem("\x78\x2E\x2E") == "\x78\x2E");
    CHECK(py_path_name("\x78\x2E\x74\x61\x72\x2E\x67\x7A") == "\x78\x2E\x74\x61\x72\x2E\x67\x7A");
    CHECK(py_path_suffix("\x78\x2E\x74\x61\x72\x2E\x67\x7A") == "\x2E\x67\x7A");
    CHECK(py_path_stem("\x78\x2E\x74\x61\x72\x2E\x67\x7A") == "\x78\x2E\x74\x61\x72");
    CHECK(py_path_name("\x78\x2E\x20\x6D\x70\x33") == "\x78\x2E\x20\x6D\x70\x33");
    CHECK(py_path_suffix("\x78\x2E\x20\x6D\x70\x33") == "\x2E\x20\x6D\x70\x33");
    CHECK(py_path_stem("\x78\x2E\x20\x6D\x70\x33") == "\x78");
    CHECK(py_path_name("\xC3\xA9\x2E\x4D\x70\x33") == "\xC3\xA9\x2E\x4D\x70\x33");
    CHECK(py_path_suffix("\xC3\xA9\x2E\x4D\x70\x33") == "\x2E\x4D\x70\x33");
    CHECK(py_path_stem("\xC3\xA9\x2E\x4D\x70\x33") == "\xC3\xA9");
    CHECK(py_path_name("\x61\x2F\x62\x2E\x77\x61\x76") == "\x62\x2E\x77\x61\x76");
    CHECK(py_path_suffix("\x61\x2F\x62\x2E\x77\x61\x76") == "\x2E\x77\x61\x76");
    CHECK(py_path_stem("\x61\x2F\x62\x2E\x77\x61\x76") == "\x62");
    CHECK(py_path_name("\x61\x5C\x62\x2E\x6F\x67\x67") == "\x62\x2E\x6F\x67\x67");
    CHECK(py_path_suffix("\x61\x5C\x62\x2E\x6F\x67\x67") == "\x2E\x6F\x67\x67");
    CHECK(py_path_stem("\x61\x5C\x62\x2E\x6F\x67\x67") == "\x62");
    CHECK(py_path_name("\x43\x3A\x78\x2E\x66\x6C\x61\x63") == "\x78\x2E\x66\x6C\x61\x63");
    CHECK(py_path_suffix("\x43\x3A\x78\x2E\x66\x6C\x61\x63") == "\x2E\x66\x6C\x61\x63");
    CHECK(py_path_stem("\x43\x3A\x78\x2E\x66\x6C\x61\x63") == "\x78");
    CHECK(py_path_name("\x61\x62\x3A\x63\x2E\x6D\x34\x61") == "\x61\x62\x3A\x63\x2E\x6D\x34\x61");
    CHECK(py_path_suffix("\x61\x62\x3A\x63\x2E\x6D\x34\x61") == "\x2E\x6D\x34\x61");
    CHECK(py_path_stem("\x61\x62\x3A\x63\x2E\x6D\x34\x61") == "\x61\x62\x3A\x63");
    CHECK(py_path_name("\x2E").empty());
    CHECK(py_path_suffix("\x2E").empty());
    CHECK(py_path_stem("\x2E").empty());
    CHECK(py_path_name("\x2E\x2E\x2E") == "\x2E\x2E\x2E");
    CHECK(py_path_suffix("\x2E\x2E\x2E").empty());
    CHECK(py_path_stem("\x2E\x2E\x2E") == "\x2E\x2E\x2E");
    CHECK(py_path_name("\x61\x2F") == "\x61");
    CHECK(py_path_suffix("\x61\x2F").empty());
    CHECK(py_path_stem("\x61\x2F") == "\x61");
    CHECK(py_path_name("\x20\x61\x20") == "\x20\x61\x20");
    CHECK(py_path_suffix("\x20\x61\x20").empty());
    CHECK(py_path_stem("\x20\x61\x20") == "\x20\x61\x20");
    CHECK(py_path_name("\x78\x3A").empty());
    CHECK(py_path_suffix("\x78\x3A").empty());
    CHECK(py_path_stem("\x78\x3A").empty());
    CHECK(py_path_name("\xC3\xA9\x3A\x66\x6F\x6F\x2E\x77\x61\x76") == "\x66\x6F\x6F\x2E\x77\x61\x76");
    CHECK(py_path_suffix("\xC3\xA9\x3A\x66\x6F\x6F\x2E\x77\x61\x76") == "\x2E\x77\x61\x76");
    CHECK(py_path_stem("\xC3\xA9\x3A\x66\x6F\x6F\x2E\x77\x61\x76") == "\x66\x6F\x6F");
    CHECK(py_path_name("\x5C\x5C\x73\x72\x76\x5C\x73\x68\x61\x72\x65").empty());
    CHECK(py_path_suffix("\x5C\x5C\x73\x72\x76\x5C\x73\x68\x61\x72\x65").empty());
    CHECK(py_path_stem("\x5C\x5C\x73\x72\x76\x5C\x73\x68\x61\x72\x65").empty());
    CHECK(py_path_name("\x5C\x5C\x73\x72\x76\x5C\x73\x68\x61\x72\x65\x5C\x78\x2E\x61\x61\x63") == "\x78\x2E\x61\x61\x63");
    CHECK(py_path_suffix("\x5C\x5C\x73\x72\x76\x5C\x73\x68\x61\x72\x65\x5C\x78\x2E\x61\x61\x63") == "\x2E\x61\x61\x63");
    CHECK(py_path_stem("\x5C\x5C\x73\x72\x76\x5C\x73\x68\x61\x72\x65\x5C\x78\x2E\x61\x61\x63") == "\x78");
    CHECK(py_path_name("\x61\x2F\x2E\x2F\x62\x2E\x6D\x70\x33") == "\x62\x2E\x6D\x70\x33");
    CHECK(py_path_suffix("\x61\x2F\x2E\x2F\x62\x2E\x6D\x70\x33") == "\x2E\x6D\x70\x33");
    CHECK(py_path_stem("\x61\x2F\x2E\x2F\x62\x2E\x6D\x70\x33") == "\x62");
    CHECK(py_path_name("\x61\x2F\x2E\x2E") == "\x2E\x2E");
    CHECK(py_path_suffix("\x61\x2F\x2E\x2E").empty());
    CHECK(py_path_stem("\x61\x2F\x2E\x2E") == "\x2E\x2E");
    CHECK(py_path_name("").empty());
    CHECK(py_path_suffix("").empty());
    CHECK(py_path_stem("").empty());
    CHECK(py_path_name("\x2E\x2E\x61\x2E\x62") == "\x2E\x2E\x61\x2E\x62");
    CHECK(py_path_suffix("\x2E\x2E\x61\x2E\x62") == "\x2E\x62");
    CHECK(py_path_stem("\x2E\x2E\x61\x2E\x62") == "\x2E\x2E\x61");
    CHECK(py_path_name("\x61\x2E\x2E\x62") == "\x61\x2E\x2E\x62");
    CHECK(py_path_suffix("\x61\x2E\x2E\x62") == "\x2E\x62");
    CHECK(py_path_stem("\x61\x2E\x2E\x62") == "\x61\x2E");
    CHECK(py_quote("\x61\x20\x62\x2F\x63") == "\x61\x25\x32\x30\x62\x2F\x63");
    CHECK(py_quote("\xC3\xB1\x25\x2B\x3F\x23\x26\x3D") == "\x25\x43\x33\x25\x42\x31\x25\x32\x35\x25\x32\x42\x25\x33\x46\x25\x32\x33\x25\x32\x36\x25\x33\x44");
    CHECK(py_quote("\x7E\x5F\x2E\x2D\x2F\x41\x5A\x61\x7A\x30\x39") == "\x7E\x5F\x2E\x2D\x2F\x41\x5A\x61\x7A\x30\x39");
    CHECK(py_quote("\x28\x20\xCD\xA0\xC2\xB0\x20\xCD\x9F\xCA\x96\x20\xCD\xA1\xC2\xB0\x29") == "\x25\x32\x38\x25\x32\x30\x25\x43\x44\x25\x41\x30\x25\x43\x32\x25\x42\x30\x25\x32\x30\x25\x43\x44\x25\x39\x46\x25\x43\x41\x25\x39\x36\x25\x32\x30\x25\x43\x44\x25\x41\x31\x25\x43\x32\x25\x42\x30\x25\x32\x39");
    CHECK(py_quote("\x27\x78\x27") == "\x25\x32\x37\x78\x25\x32\x37");
    CHECK(py_quote("").empty());
}

TEST_CASE("py_path_str y py_path_name con unidades, UNC y raíces son los de Python 3.14", "[py]") {
    // Valores medidos con el Python de video-yt (bytes escapados).
    CHECK(py_path_str("") == "\x2E");
    CHECK(py_path_name("").empty());
    CHECK(py_path_str("\x2E") == "\x2E");
    CHECK(py_path_name("\x2E").empty());
    CHECK(py_path_str("\x2E\x2F") == "\x2E");
    CHECK(py_path_name("\x2E\x2F").empty());
    CHECK(py_path_str("\x61") == "\x61");
    CHECK(py_path_name("\x61") == "\x61");
    CHECK(py_path_str("\x61\x2F\x62") == "\x61\x5C\x62");
    CHECK(py_path_name("\x61\x2F\x62") == "\x62");
    CHECK(py_path_str("\x61\x2F\x2F\x62\x2F\x2E\x2F\x63\x2F") == "\x61\x5C\x62\x5C\x63");
    CHECK(py_path_name("\x61\x2F\x2F\x62\x2F\x2E\x2F\x63\x2F") == "\x63");
    CHECK(py_path_str("\x43\x3A\x2F\x61\x2F\x2F\x62\x2F\x2E\x2F\x63\x2F") == "\x43\x3A\x5C\x61\x5C\x62\x5C\x63");
    CHECK(py_path_name("\x43\x3A\x2F\x61\x2F\x2F\x62\x2F\x2E\x2F\x63\x2F") == "\x63");
    CHECK(py_path_str("\x43\x3A") == "\x43\x3A");
    CHECK(py_path_name("\x43\x3A").empty());
    CHECK(py_path_str("\x43\x3A\x78") == "\x43\x3A\x78");
    CHECK(py_path_name("\x43\x3A\x78") == "\x78");
    CHECK(py_path_str("\x43\x3A\x2F") == "\x43\x3A\x5C");
    CHECK(py_path_name("\x43\x3A\x2F").empty());
    CHECK(py_path_str("\x63\x3A\x5C\x78\x5C\x2E\x2E\x5C\x79") == "\x63\x3A\x5C\x78\x5C\x2E\x2E\x5C\x79");
    CHECK(py_path_name("\x63\x3A\x5C\x78\x5C\x2E\x2E\x5C\x79") == "\x79");
    CHECK(py_path_str("\x2F\x78") == "\x5C\x78");
    CHECK(py_path_name("\x2F\x78") == "\x78");
    CHECK(py_path_str("\x5C\x78\x5C") == "\x5C\x78");
    CHECK(py_path_name("\x5C\x78\x5C") == "\x78");
    CHECK(py_path_str("\x2F\x2F\x73\x72\x76\x2F\x73\x68\x61\x72\x65") == "\x5C\x5C\x73\x72\x76\x5C\x73\x68\x61\x72\x65\x5C");
    CHECK(py_path_name("\x2F\x2F\x73\x72\x76\x2F\x73\x68\x61\x72\x65").empty());
    CHECK(py_path_str("\x2F\x2F\x73\x72\x76\x2F\x73\x68\x61\x72\x65\x2F") == "\x5C\x5C\x73\x72\x76\x5C\x73\x68\x61\x72\x65\x5C");
    CHECK(py_path_name("\x2F\x2F\x73\x72\x76\x2F\x73\x68\x61\x72\x65\x2F").empty());
    CHECK(py_path_str("\x2F\x2F\x73\x72\x76\x2F\x73\x68\x61\x72\x65\x2F\x61\x2F\x62") == "\x5C\x5C\x73\x72\x76\x5C\x73\x68\x61\x72\x65\x5C\x61\x5C\x62");
    CHECK(py_path_name("\x2F\x2F\x73\x72\x76\x2F\x73\x68\x61\x72\x65\x2F\x61\x2F\x62") == "\x62");
    CHECK(py_path_str("\x2F\x2F\x73\x72\x76") == "\x5C\x5C\x73\x72\x76");
    CHECK(py_path_name("\x2F\x2F\x73\x72\x76").empty());
    CHECK(py_path_str("\x2F\x2F\x73\x72\x76\x2F") == "\x5C\x5C\x73\x72\x76\x5C");
    CHECK(py_path_name("\x2F\x2F\x73\x72\x76\x2F").empty());
    CHECK(py_path_str("\x2F\x2F\x2F\x78") == "\x5C\x5C\x5C\x78");
    CHECK(py_path_name("\x2F\x2F\x2F\x78").empty());
    CHECK(py_path_str("\x2F\x2F\x2E\x2F\x63\x3A\x2F\x78") == "\x5C\x5C\x2E\x5C\x63\x3A\x5C\x78");
    CHECK(py_path_name("\x2F\x2F\x2E\x2F\x63\x3A\x2F\x78") == "\x78");
    CHECK(py_path_str("\x2F\x2F\x3F\x2F\x63\x3A\x2F\x78") == "\x5C\x5C\x3F\x5C\x63\x3A\x5C\x78");
    CHECK(py_path_name("\x2F\x2F\x3F\x2F\x63\x3A\x2F\x78") == "\x78");
    CHECK(py_path_str("\x2F\x2F\x3F\x2F\x55\x4E\x43\x2F\x73\x72\x76\x2F\x73\x68\x61\x72\x65") == "\x5C\x5C\x3F\x5C\x55\x4E\x43\x5C\x73\x72\x76\x5C\x73\x68\x61\x72\x65\x5C");
    CHECK(py_path_name("\x2F\x2F\x3F\x2F\x55\x4E\x43\x2F\x73\x72\x76\x2F\x73\x68\x61\x72\x65").empty());
    CHECK(py_path_str("\x2F\x2F\x3F\x2F\x75\x6E\x63\x2F\x73\x72\x76\x2F\x73\x68\x61\x72\x65\x2F\x78") == "\x5C\x5C\x3F\x5C\x75\x6E\x63\x5C\x73\x72\x76\x5C\x73\x68\x61\x72\x65\x5C\x78");
    CHECK(py_path_name("\x2F\x2F\x3F\x2F\x75\x6E\x63\x2F\x73\x72\x76\x2F\x73\x68\x61\x72\x65\x2F\x78") == "\x78");
    CHECK(py_path_str("\x2F\x2F\x3F\x2F\x55\x4E\x43\x2F\x73\x72\x76") == "\x5C\x5C\x3F\x5C\x55\x4E\x43\x5C\x73\x72\x76");
    CHECK(py_path_name("\x2F\x2F\x3F\x2F\x55\x4E\x43\x2F\x73\x72\x76").empty());
    CHECK(py_path_str("\x2F\x2F\x2E\x2F\x64\x65\x76") == "\x5C\x5C\x2E\x5C\x64\x65\x76");
    CHECK(py_path_name("\x2F\x2F\x2E\x2F\x64\x65\x76").empty());
    CHECK(py_path_str("\x2F\x2F\x3F\x2E\x2F\x78") == "\x5C\x5C\x3F\x2E\x5C\x78");
    CHECK(py_path_name("\x2F\x2F\x3F\x2E\x2F\x78").empty());
    CHECK(py_path_str("\x2E\x2F\x63\x3A\x78") == "\x2E\x5C\x63\x3A\x78");
    CHECK(py_path_name("\x2E\x2F\x63\x3A\x78") == "\x63\x3A\x78");
    CHECK(py_path_str("\x61\x2F\x63\x3A\x78") == "\x61\x5C\x63\x3A\x78");
    CHECK(py_path_name("\x61\x2F\x63\x3A\x78") == "\x63\x3A\x78");
    CHECK(py_path_str("\xC3\xA9\x3A\x66\x6F\x6F") == "\xC3\xA9\x3A\x66\x6F\x6F");
    CHECK(py_path_name("\xC3\xA9\x3A\x66\x6F\x6F") == "\x66\x6F\x6F");
    CHECK(py_path_str("\xF0\x9F\x98\x80\x3A\x78") == "\xF0\x9F\x98\x80\x3A\x78");
    CHECK(py_path_name("\xF0\x9F\x98\x80\x3A\x78") == "\xF0\x9F\x98\x80\x3A\x78");
    CHECK(py_path_str("\xF0\x9F\x98\x80\x3A\x78\x2F\x79") == "\xF0\x9F\x98\x80\x3A\x78\x5C\x79");
    CHECK(py_path_name("\xF0\x9F\x98\x80\x3A\x78\x2F\x79") == "\x79");
    CHECK(py_path_str("\x78\x2E") == "\x78\x2E");
    CHECK(py_path_name("\x78\x2E") == "\x78\x2E");
    CHECK(py_path_str("\x20\x61\x20") == "\x20\x61\x20");
    CHECK(py_path_name("\x20\x61\x20") == "\x20\x61\x20");
    CHECK(py_path_str("\x45\x3A\x5C\x70\x72\x6F\x79\x65\x63\x74\x6F\x73\x5C\x76\x69\x64\x65\x6F\x2D\x79\x74\x2F\x61\x73\x73\x65\x74\x73\x2F\x73\x66\x78\x5F\x6C\x69\x62\x72\x61\x72\x79") == "\x45\x3A\x5C\x70\x72\x6F\x79\x65\x63\x74\x6F\x73\x5C\x76\x69\x64\x65\x6F\x2D\x79\x74\x5C\x61\x73\x73\x65\x74\x73\x5C\x73\x66\x78\x5F\x6C\x69\x62\x72\x61\x72\x79");
    CHECK(py_path_name("\x45\x3A\x5C\x70\x72\x6F\x79\x65\x63\x74\x6F\x73\x5C\x76\x69\x64\x65\x6F\x2D\x79\x74\x2F\x61\x73\x73\x65\x74\x73\x2F\x73\x66\x78\x5F\x6C\x69\x62\x72\x61\x72\x79") == "\x73\x66\x78\x5F\x6C\x69\x62\x72\x61\x72\x79");
    CHECK(py_path_str("\x61\x5C\x2E\x2E\x5C\x62") == "\x61\x5C\x2E\x2E\x5C\x62");
    CHECK(py_path_name("\x61\x5C\x2E\x2E\x5C\x62") == "\x62");
    CHECK(py_path_str("\x2E\x2E") == "\x2E\x2E");
    CHECK(py_path_name("\x2E\x2E") == "\x2E\x2E");
    CHECK(py_path_str("\x2E\x2E\x2F\x2E\x2E") == "\x2E\x2E\x5C\x2E\x2E");
    CHECK(py_path_name("\x2E\x2E\x2F\x2E\x2E") == "\x2E\x2E");
    CHECK(py_path_str("\x43\x3A\x2E\x2E") == "\x43\x3A\x2E\x2E");
    CHECK(py_path_name("\x43\x3A\x2E\x2E") == "\x2E\x2E");
    CHECK(py_path_str("\x3A\x78") == "\x3A\x78");
    CHECK(py_path_name("\x3A\x78") == "\x3A\x78");
    CHECK(py_path_str("\x3A\x3A\x78") == "\x3A\x3A\x78");
    CHECK(py_path_name("\x3A\x3A\x78") == "\x78");
    CHECK(py_path_str("\x61\x3A") == "\x61\x3A");
    CHECK(py_path_name("\x61\x3A").empty());
    CHECK(py_path_str("\x31\x3A\x2F\x78") == "\x31\x3A\x5C\x78");
    CHECK(py_path_name("\x31\x3A\x2F\x78") == "\x78");
}
