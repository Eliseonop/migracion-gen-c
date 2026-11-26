// Traer las API keys de video-yt (es de GenClipPro): leer un .env (env_api_keys) y SettingsFile::import_keys, que
// copia las de otro settings.json sin pisar nada.

#include <chrono>
#include <filesystem>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "gc/core/io.hpp"
#include "gc/core/settings.hpp"

using namespace gc;
namespace fs = std::filesystem;

namespace {

const std::vector<std::string> kProviders{"gemini", "openai", "groq", "huggingface", "elevenlabs", "removebg"};

struct TempDir {
    // Única: Debug y Release pueden correr a la vez.
    fs::path dir = fs::temp_directory_path() /
                   ("gc_settings_import_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    TempDir() {
        fs::remove_all(dir);
        fs::create_directories(dir);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(dir, ec);
    }
};

} // namespace

TEST_CASE("env_api_keys: las <PROVEEDOR>_API_KEY de un .env, con comentarios, export y comillas", "[settings]") {
    const std::string text = "\xEF\xBB\xBF# claves de desarrollo\r\n"
                             "GEMINI_API_KEY=g1\r\n"
                             "export OPENAI_API_KEY = \"o 1\"\n"
                             "GROQ_API_KEY='q1' # la de Groq\n"
                             "HF_TOKEN=h1\n"
                             "ELEVENLABS_KEY=e1 # comentario\n"
                             "REMOVE_BG_API_KEY=r1\n"
                             "DATABASE_URL=postgres://nada\n"
                             "SECRET_KEY=no-es-un-proveedor\n"
                             "GEMINI_API_KEY=g2\n"
                             "SIN_IGUAL\n"
                             "PEXELS_API_KEY=fuera-de-la-lista\n"
                             "OPENAI_API_KEY=\n";
    const Json keys = core::env_api_keys(text, kProviders);
    CHECK(keys == Json{{"gemini", "g2"}, // la última gana
                       {"openai", "o 1"},
                       {"groq", "q1"},
                       {"huggingface", "h1"},
                       {"elevenlabs", "e1"},
                       {"removebg", "r1"}});
    CHECK(core::env_api_keys("GOOGLE_API_KEY=a\n", kProviders) == Json{{"gemini", "a"}});
    CHECK(core::env_api_keys("", kProviders) == Json::object());
}

TEST_CASE("import_keys: trae las de otro settings.json y del .env sin pisar las que hay", "[settings]") {
    TempDir t;
    core::SettingsFile mine(t.dir / "data" / "settings.json");
    REQUIRE(mine.add_key("gemini", "g-de-aqui"));
    REQUIRE(mine.save({{"export", {{"fps", 60}}}}));

    const Json videoyt = {{"gemini_api_key", "g-de-videoyt"}, // la de antes: no pisa la de aquí
                          {"api_keys", {{"openai", "o1"}, {"pexels", "p1"}, {"vacio", ""}}},
                          {"api_keys_extra", {{"openai", {"o2", "o3"}}, {"groq", {"q-extra"}}}},
                          {"azure_region", "westeurope"},
                          {"azure_vision_endpoint", "https://ejemplo.cognitiveservices.azure.com/"},
                          {"tts", {{"voice", "de-videoyt"}}}};
    const Json env = {{"elevenlabs", "e1"}, {"openai", "o-del-env"}};
    const auto arrived = mine.import_keys(videoyt, env);
    REQUIRE(arrived);
    CHECK(*arrived == std::vector<std::string>{"elevenlabs", "groq", "openai", "pexels"});
    CHECK(mine.api_key("gemini") == "g-de-aqui");
    CHECK(*mine.keys_for("openai") == std::vector<std::string>{"o1", "o2", "o3"}); // la del .env no la pisa
    CHECK(*mine.keys_for("groq") == std::vector<std::string>{"q-extra"});
    CHECK(mine.api_key("pexels") == "p1");
    CHECK(mine.api_key("elevenlabs") == "e1");
    CHECK(mine.api_key("vacio").empty());
    const Json saved = mine.load();
    CHECK(saved.value("azure_region", "") == "westeurope");
    CHECK(saved.value("azure_vision_endpoint", "") == "https://ejemplo.cognitiveservices.azure.com/");
    CHECK(saved["export"].value("fps", 0) == 60);                       // lo demás de aquí se queda
    CHECK(saved["tts"].value("voice", std::string()) != "de-videoyt"); // y de video-yt solo llegan las claves

    // Otra vez: ya no falta ninguna y nada cambia.
    const auto again = mine.import_keys(videoyt, env);
    REQUIRE(again);
    CHECK(again->empty());
    CHECK(*mine.keys_for("openai") == std::vector<std::string>{"o1", "o2", "o3"});

    // Sin archivo y sin nada que traer, no se crea.
    core::SettingsFile empty(t.dir / "otra" / "settings.json");
    REQUIRE(empty.import_keys(Json::object(), Json::object()));
    CHECK_FALSE(fs::exists(t.dir / "otra" / "settings.json"));

    // Aquí, solo el gemini_api_key de antes (sin api_keys): cuenta como clave y no se pisa.
    const fs::path old_file = t.dir / "antiguo" / "settings.json";
    REQUIRE(core::write_json_file(old_file, Json{{"gemini_api_key", "g-antigua"}}, 2));
    core::SettingsFile old(old_file);
    const auto from_old = old.import_keys(videoyt, Json{{"gemini", "g-del-env"}});
    REQUIRE(from_old);
    CHECK(*from_old == std::vector<std::string>{"groq", "openai", "pexels"});
    CHECK(old.api_key("gemini") == "g-antigua");
}
