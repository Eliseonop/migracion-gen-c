#include "gc/log/log.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <memory>
#include <string>
#include <utility>

#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace gc::log {

namespace {

constexpr std::array<std::pair<std::string_view, Level>, 8> kNames{{
    {"trace", Level::trace},
    {"debug", Level::debug},
    {"info", Level::info},
    {"warn", Level::warn},
    {"warning", Level::warn},
    {"error", Level::err},
    {"critical", Level::critical},
    {"off", Level::off},
}};

bool is_space(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

} // namespace

std::optional<Level> parse_level(std::string_view text) {
    while (!text.empty() && is_space(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && is_space(text.back())) {
        text.remove_suffix(1);
    }
    std::string lower(text);
    std::ranges::transform(lower, lower.begin(), [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; });
    for (const auto& [name, level] : kNames) {
        if (lower == name) {
            return level;
        }
    }
    return std::nullopt;
}

Level level_from_env(Level fallback) {
    const char* value = std::getenv(std::string(kEnvVar).c_str()); // NOLINT(concurrency-mt-unsafe): al arrancar
    if (value == nullptr) {
        return fallback;
    }
    return parse_level(value).value_or(fallback);
}

void init(Level level, std::vector<spdlog::sink_ptr> extra) {
    auto console = std::make_shared<spdlog::sinks::stderr_color_sink_mt>();
    console->set_pattern("%^%l%$: %v");
    extra.insert(extra.begin(), std::move(console));
    auto logger = std::make_shared<spdlog::logger>("genclip", extra.begin(), extra.end());
    logger->set_level(level);
    spdlog::set_default_logger(std::move(logger));
}

} // namespace gc::log
