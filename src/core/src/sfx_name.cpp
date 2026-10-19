#include "gc/core/sfx_name.hpp"

#include <cmath>

#include "py.hpp"

namespace gc::core {

namespace {

constexpr std::string_view kFavorites = "favoritos";

bool truthy(const Json& v) {
    if (v.is_null()) {
        return false;
    }
    if (v.is_boolean()) {
        return v.get<bool>();
    }
    if (v.is_number()) {
        return v.get<double>() != 0 && !std::isnan(v.get<double>());
    }
    return !v.is_string() || !v.get_ref<const std::string&>().empty();
}

// String(x || '')
std::string text(const Json& v) {
    if (!truthy(v)) {
        return {};
    }
    if (v.is_string()) {
        return v.get<std::string>();
    }
    if (v.is_boolean()) {
        return "true";
    }
    if (v.is_number_float()) {
        const double d = v.get<double>();
        return d == std::trunc(d) && std::abs(d) < 1e15 ? std::to_string(static_cast<long long>(d)) : v.dump();
    }
    return v.is_number() ? v.dump() : v.is_array() ? "" : "[object Object]";
}

const Json& field(const Json& obj, const char* key) {
    static const Json kNull;
    return obj.is_object() && obj.contains(key) ? obj.at(key) : kNull;
}

std::string trim(const Json& v) { return detail::py_strip(text(v)); }

} // namespace

std::string sfx_name_from_file(const Json& filename) {
    std::string base = text(filename);
    const std::string_view first_line = std::string_view(base).substr(0, base.find_first_of("\r\n")); // /^.*[\\/]/
    if (const auto slash = first_line.find_last_of("/\\"); slash != std::string_view::npos) {
        base = base.substr(slash + 1);
    }
    const auto dot = base.rfind('.');
    return detail::py_strip(dot != std::string::npos && dot > 0 ? base.substr(0, dot) : base);
}

std::vector<Json> real_sfx_categories(const Json& categories) {
    std::vector<Json> out;
    for (const Json& c : categories.is_array() ? categories : Json::array()) {
        const Json& id = field(c, "id");
        if (truthy(c) && truthy(id) && !(id.is_string() && id.get_ref<const std::string&>() == kFavorites)) {
            out.push_back(c);
        }
    }
    return out;
}

std::vector<Json> upsert_sfx_category(const Json& categories, const Json& cat) {
    const Json& id = field(cat, "id");
    auto out = real_sfx_categories(categories);
    if (!truthy(id) || (id.is_string() && id.get_ref<const std::string&>() == kFavorites)) {
        return out;
    }
    std::erase_if(out, [&](const Json& c) { return field(c, "id") == id; });
    const Json& label = field(cat, "label");
    const Json& count = field(cat, "count");
    out.push_back(Json{{"id", id}, {"label", truthy(label) ? label : id}, {"count", count.is_null() ? Json(0) : count}});
    return out;
}

Json match_sfx_category(const Json& value, const Json& categories) {
    const std::string t = trim(value);
    if (t.empty()) {
        return Json{{"categoryId", ""}, {"newCategory", ""}};
    }
    const std::string want = detail::py_lower(t);
    for (const Json& c : categories.is_array() ? categories : Json::array()) {
        const Json& label = field(c, "label");
        const std::string shown = detail::py_lower(text(truthy(label) ? label : field(c, "id")));
        if (shown == want || detail::py_lower(text(field(c, "id"))) == want) {
            return Json{{"categoryId", field(c, "id")}, {"newCategory", ""}};
        }
    }
    return Json{{"categoryId", ""}, {"newCategory", t}};
}

Json sfx_draft_from_saved(const Json& sfx) {
    const auto or_else = [&](const char* key, Json fallback) {
        const Json& v = field(sfx, key);
        return truthy(v) ? v : fallback;
    };
    return Json{{"file", nullptr},
                {"sfxId", or_else("id", nullptr)},
                {"previewUrl", or_else("url", "")},
                {"name", or_else("name", "")},
                {"categoryId", or_else("folder", "")},
                {"categoryLabel", or_else("category", "")},
                {"uso", or_else("uso", "")},
                {"saved", false}};
}

Json sfx_save_payload(const Json& row, const Json& categories) {
    const std::string name = trim(field(row, "name"));
    const std::string uso = trim(field(row, "uso"));
    if (const std::string id = trim(field(row, "categoryId")); !id.empty()) {
        return Json{{"name", name}, {"categoryId", id}, {"newCategory", ""}, {"uso", uso}};
    }
    const Json pick = match_sfx_category(field(row, "categoryLabel"), categories);
    return Json{{"name", name}, {"categoryId", pick.at("categoryId")}, {"newCategory", pick.at("newCategory")},
                {"uso", uso}};
}

} // namespace gc::core
