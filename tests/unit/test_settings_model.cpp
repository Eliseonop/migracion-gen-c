// El ViewModel de los ajustes (apps/studio/settings_model.hpp): lo que EdSettings.jsx enseña y guarda.

#include <QCoreApplication>
#include <filesystem>
#include <string>

#include <catch2/catch_test_macros.hpp>

#include "settings_model.hpp"

using namespace gc;

namespace {

struct Fixture {
    std::filesystem::path dir = std::filesystem::temp_directory_path() /
                                ("gc_settings_model_" + std::to_string(QCoreApplication::applicationPid()));
    core::SettingsFile file{dir / "settings.json"};

    Fixture() { std::filesystem::remove_all(dir); }
    ~Fixture() { std::filesystem::remove_all(dir); }
};

QVariantMap provider(const studio::SettingsModel& m, const QString& id) {
    for (const QVariant& v : m.providers()) {
        if (v.toMap()["id"] == id) {
            return v.toMap();
        }
    }
    return {};
}

} // namespace

TEST_CASE("ajustes: valores de fábrica y los 19 proveedores de la lista", "[studio]") {
    Fixture f;
    studio::SettingsModel m(f.file);
    CHECK(m.fps() == 30);
    CHECK(m.quality() == "standard");
    CHECK(m.transcribeModel() == "base");
    CHECK(m.providers().size() == 19);
    CHECK(provider(m, "pexels")["count"] == 0);
    CHECK(provider(m, "youtube")["wired"] == false);
}

TEST_CASE("ajustes: exportación y transcripción se guardan y se releen", "[studio]") {
    Fixture f;
    {
        studio::SettingsModel m(f.file);
        m.setExport(60, "high");
        m.setTranscribeModel("small");
        CHECK(m.error().isEmpty());
    }
    studio::SettingsModel again(f.file);
    CHECK(again.fps() == 60);
    CHECK(again.quality() == "high");
    CHECK(again.transcribeModel() == "small");
}

TEST_CASE("ajustes: las claves solo se cuentan, nunca se enseñan", "[studio]") {
    Fixture f;
    studio::SettingsModel m(f.file);
    m.addKey("pexels", "  abc  ");
    m.addKey("pexels", "def");
    m.addKey("pexels", "   ");
    CHECK(provider(m, "pexels")["count"] == 2);
    CHECK(f.file.api_key("pexels") == "abc");
    for (const QVariant& v : m.providers()) {
        for (const QVariant& field : v.toMap()) {
            CHECK_FALSE(field.toString().contains("abc"));
        }
    }
    m.setKey("pexels", 1, "xyz");
    CHECK(f.file.keys_for("pexels").value() == std::vector<std::string>{"abc", "xyz"});
    m.removeKey("pexels", 0);
    CHECK(provider(m, "pexels")["count"] == 1);
    CHECK(f.file.api_key("pexels") == "xyz");
}
