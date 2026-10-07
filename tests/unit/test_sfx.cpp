// sfx_norm (sound_design.norm de video-yt): valores medidos con Python 3.14.

#include <catch2/catch_test_macros.hpp>

#include <utility>

#include "gc/core/sfx_library.hpp"

using gc::Json;
using gc::core::sfx_norm;

TEST_CASE("sfx_norm quita tildes, pasa a minúsculas y deja solo letras y números", "[sfx]") {
    const std::pair<Json, const char*> cases[] = {
        {"C\xC3\xA1" "mara snap", "camara snap"},
        {"Fotograf\xC3\xAD" "a", "fotografia"},
        {"\xC3\x86\xC3\x98\xC3\x9F x", "x"},                       // Æ Ø ß: sin descomposición
        {"\xEF\xAC\x81le \xC2\xB2\xC2\xBD", "file 212"},          // ligadura fi, ² y ½ (1⁄2)
        {"\xC2\xA0 Pe\xC3\xB1" "a  -- \xC4\xB0stanbul", "pena istanbul"},
        {"\xC5\x81\xC3\xB3" "d\xC5\xBA \xC5\x92uvre", "odz uvre"}, // Ł y Œ se pierden
        {"\xEF\xBC\xA1\xEF\xBC\xA2\xEF\xBC\xA3 1", "abc 1"},      // ancho completo
        {"na\xC3\xAF" "ve\xE2\x80\x83" "caf\xC3\xA9", "naive cafe"}, // espacio em
        {Json(), ""},
        {Json(5), "5"},
        {"", ""},
        {"\xC4\xB3 \xC5\xBF", "ij s"},                             // ĳ y ſ
    };
    for (const auto& [in, want] : cases) {
        INFO(in.dump());
        CHECK(sfx_norm(in) == want);
    }
}
