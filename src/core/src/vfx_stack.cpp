#include "gc/core/vfx_stack.hpp"

#include <algorithm>
#include <cmath>

#include "gc/core/fx_catalog.hpp"
#include "gc/core/timeline_ui.hpp"

namespace gc::core {

namespace {

double num(const Json& v, double fallback) {
    const double n = js_number(v);
    return std::isfinite(n) && !v.is_null() ? n : fallback;
}

const Json& field(const Json& obj, const char* key) {
    static const Json kNull;
    return obj.is_object() && obj.contains(key) ? obj.at(key) : kNull;
}

} // namespace

std::optional<Json> normalize_vfx_entry(const Json& raw, IdSource& ids) {
    const auto catalog = fx_catalog();
    const Json* effect = catalog ? catalog->effect(field(raw, "effect")) : nullptr;
    if (!effect) {
        return std::nullopt;
    }
    auto params = FxCatalog::resolve_params(*effect, field(raw, "params"));
    if (!params) {
        return std::nullopt;
    }
    const double from = std::max(0.0, num(field(raw, "from"), 0));
    const Json& to = field(raw, "to");
    const Json& uid = field(raw, "uid");
    const bool has_uid = uid.is_string() ? !uid.get_ref<const std::string&>().empty() : !uid.is_null() && uid != false && uid != 0;
    return Json{{"uid", has_uid ? (uid.is_string() ? uid : Json(uid.dump())) : Json(make_uid(ids, "fx"))},
                {"effect", effect->at("id")},
                {"v", std::max(1.0, std::floor(num(field(raw, "v"), num(field(*effect, "v"), 1)) + 0.5))},
                {"enabled", field(raw, "enabled") != false},
                {"params", *params},
                {"from", from},
                {"to", to.is_null() ? Json() : Json(std::max(from, num(to, from)))}};
}

std::optional<Json> make_vfx_entry(std::string_view effect_id, IdSource& ids) {
    const auto catalog = fx_catalog();
    const Json* effect = catalog ? catalog->effect(Json(effect_id)) : nullptr;
    if (!effect) {
        return std::nullopt;
    }
    const Json& dur = field(*effect, "default_dur");
    return normalize_vfx_entry({{"effect", effect_id},
                                {"enabled", true},
                                {"from", 0},
                                {"to", dur.is_number() ? Json(dur.get<double>()) : Json()}},
                               ids);
}

} // namespace gc::core
