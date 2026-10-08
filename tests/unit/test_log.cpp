// Registro (gc_log sobre spdlog): niveles, variable de entorno y salida.

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <memory>
#include <sstream>
#include <string>

#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>

#include "gc/log/log.hpp"

namespace {

void set_env(const char* name, const char* value) {
#ifdef _WIN32
    (void)_putenv_s(name, value != nullptr ? value : "");
#else
    if (value != nullptr) {
        (void)setenv(name, value, 1);
    } else {
        (void)unsetenv(name);
    }
#endif
}

} // namespace

TEST_CASE("log: los nombres de nivel", "[log]") {
    using gc::log::Level;
    using gc::log::parse_level;
    CHECK(parse_level("debug") == Level::debug);
    CHECK(parse_level("  WARNING\n") == Level::warn);
    CHECK(parse_level("Warn") == Level::warn);
    CHECK(parse_level("error") == Level::err);
    CHECK(parse_level("off") == Level::off);
    CHECK(parse_level("trace") == Level::trace);
    CHECK_FALSE(parse_level(""));
    CHECK_FALSE(parse_level("err"));
    CHECK_FALSE(parse_level("verbose"));
}

TEST_CASE("log: GENCLIP_LOG manda si es válido", "[log]") {
    using gc::log::Level;
    set_env("GENCLIP_LOG", "debug");
    CHECK(gc::log::level_from_env(Level::warn) == Level::debug);
    set_env("GENCLIP_LOG", "mucho");
    CHECK(gc::log::level_from_env(Level::warn) == Level::warn);
    set_env("GENCLIP_LOG", nullptr); // en Windows, vacía = borrada
    CHECK(gc::log::level_from_env(Level::info) == Level::info);
}

TEST_CASE("log: init filtra por nivel y llega a los sinks extra", "[log]") {
    std::ostringstream out;
    auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(out);
    // Fin de línea fijo: el de spdlog en Windows es CRLF.
    sink->set_formatter(
        std::make_unique<spdlog::pattern_formatter>("%l|%v", spdlog::pattern_time_type::local, std::string("\n")));
    gc::log::init(gc::log::Level::info, {sink});
    spdlog::debug("oculto {}", 1);
    spdlog::info("visible {} ñ", 2);
    spdlog::error("aviso de prueba de test_log"); // también sale por stderr: no es un fallo
    spdlog::default_logger()->flush();
    CHECK(out.str() == "info|visible 2 ñ\n" + std::string("error|aviso de prueba de test_log\n"));
    gc::log::init(gc::log::Level::off); // sin el sink del test, que se destruye aquí
}
