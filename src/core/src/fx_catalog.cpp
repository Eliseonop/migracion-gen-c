#include "gc/core/fx_catalog.hpp"

#include <algorithm>
#include <cmath>
#include <mutex>

#include "gc/core/io.hpp"
#include "json_reader.hpp"
#include "py.hpp"

namespace gc::core {

using detail::py_float;
using detail::py_round;
using detail::py_str;
using detail::py_truthy;

namespace {

const Json& field(const Json& obj, std::string_view key) {
    static const Json kNull;
    if (!obj.is_object()) {
        return kNull;
    }
    const auto it = obj.find(key);
    return it == obj.end() ? kNull : *it;
}

const Json* find_by_id(const Json& list, const Json& id) {
    // fx_catalog.effect(): str(id or "")
    const std::string wanted = py_truthy(id) ? py_str(id) : std::string();
    if (!list.is_array()) {
        return nullptr;
    }
    for (const Json& e : list) {
        if (field(e, "id") == wanted) {
            return &e;
        }
    }
    return nullptr;
}

// _param_value de fx_catalog.py (= paramValue de fx/core/params.js). Un `range` sin `min` o
// `max` numéricos es un catálogo roto: Python lanza KeyError o ValueError y aquí es un error.
Result<Json> param_value(const Json& p, const Json& v) {
    const Json& def = field(p, "def");
    if (v.is_null()) {
        return def;
    }
    const Json& type = field(p, "type");
    if (type == "range") {
        const auto n = py_float(v);
        if (!n) {
            return def;
        }
        const auto lo = py_float(field(p, "min"));
        const auto hi = py_float(field(p, "max"));
        if (!lo || !hi) {
            return fail(ErrorCode::Internal,
                        "catálogo de efectos: el parámetro " + py_str(field(p, "id")) + " no tiene min y max numéricos");
        }
        return std::min(*hi, std::max(*lo, *n));
    }
    if (type == "toggle") {
        return (v.is_boolean() && v.get<bool>()) || ((v.is_number()) && v.get<double>() == 1.0) || v == "true";
    }
    if (type == "color") {
        std::string s = py_str(v);
        detail::ascii_lower_inplace(s);
        const bool ok = s.size() == 7 && s[0] == '#' && std::all_of(s.begin() + 1, s.end(), [](char c) {
                            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
                        });
        return ok ? Json(s) : def;
    }
    if (type == "select") {
        std::string s = py_str(v);
        const Json& options = field(p, "options");
        if (options.is_array()) {
            for (const Json& o : options) {
                if (field(o, "id") == s) {
                    return s;
                }
            }
        }
        return def;
    }
    if (type == "seed") {
        const auto n = py_float(v);
        if (!n || !std::isfinite(*n)) {
            return def;
        }
        return static_cast<std::int64_t>(py_round(*n, 0)); // int(round(x)): empates al par
    }
    return def;
}

std::mutex g_mutex;
std::shared_ptr<const FxCatalog> g_catalog = std::make_shared<const FxCatalog>();

} // namespace

FxCatalog::FxCatalog(Json data) : data_(data.is_object() ? std::move(data) : Json::object()) {}

Result<FxCatalog> FxCatalog::load(const std::filesystem::path& path) {
    auto json = read_json_file(path);
    if (!json) {
        return std::unexpected(json.error());
    }
    return FxCatalog(std::move(*json));
}

const Json* FxCatalog::effect(const Json& id) const { return find_by_id(field(data_, "effects"), id); }

const Json* FxCatalog::anim(const Json& id) const { return find_by_id(field(data_, "anims"), id); }

Result<Json> FxCatalog::resolve_params(const Json& manifest, const Json& raw) {
    Json out = Json::object();
    const Json& params = field(manifest, "params");
    if (!params.is_array()) {
        return out;
    }
    for (const Json& p : params) {
        const Json& id = field(p, "id");
        if (!id.is_string()) { // p["id"] de Python: KeyError (y con otro tipo, un JSON que no se puede escribir)
            return fail(ErrorCode::Internal, "catálogo de efectos: un parámetro sin id de texto");
        }
        auto value = param_value(p, field(raw, id.get_ref<const std::string&>()));
        if (!value) {
            return std::unexpected(std::move(value.error()));
        }
        out[id.get_ref<const std::string&>()] = std::move(*value);
    }
    return out;
}

void set_fx_catalog(std::shared_ptr<const FxCatalog> catalog) {
    const std::scoped_lock lock(g_mutex);
    g_catalog = catalog ? std::move(catalog) : std::make_shared<const FxCatalog>();
}

std::shared_ptr<const FxCatalog> fx_catalog() {
    const std::scoped_lock lock(g_mutex);
    return g_catalog;
}

} // namespace gc::core
