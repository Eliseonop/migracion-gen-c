#include "studio_config.hpp"

#include <system_error>
#include <utility>

#include "gc/core/io.hpp"
#include "gc/core/json.hpp"

namespace gc::studio::studio_config {

namespace fs = std::filesystem;

std::optional<std::string> get(const fs::path& config, std::string_view key) {
    std::error_code ec;
    if (!fs::exists(config, ec)) {
        return std::nullopt;
    }
    const auto doc = core::read_json_file(config);
    if (!doc || !doc->is_object()) {
        return std::nullopt;
    }
    const auto it = doc->find(std::string(key));
    if (it == doc->end() || !it->is_string() || it->get<std::string>().empty()) {
        return std::nullopt;
    }
    return it->get<std::string>();
}

Result<void> set(const fs::path& config, std::string_view key, const std::optional<std::string>& value) {
    Json doc = Json::object();
    std::error_code ec;
    if (fs::exists(config, ec)) {
        if (auto old = core::read_json_file(config); old && old->is_object()) {
            doc = std::move(*old);
        }
    }
    if (value) {
        doc[std::string(key)] = *value;
    } else {
        doc.erase(std::string(key));
    }
    fs::create_directories(config.parent_path(), ec);
    return core::write_json_file(config, doc);
}

} // namespace gc::studio::studio_config
