#include "gc/core/bg.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <limits>

#include "json_reader.hpp"
#include "py.hpp"

namespace gc::core {

using detail::py_iter;
using detail::py_num_finite;
using detail::py_round;
using detail::py_str;
using detail::py_truthy;

namespace {

constexpr std::array<std::string_view, 8> kProviders{"rvm_mobilenetv3", "rvm_resnet50",  "birefnet_lite",
                                                     "u2net",           "u2netp",        "sam21_tiny",
                                                     "sam21_base_plus", "sam21_large"};
constexpr std::array<std::string_view, 4> kStatus{"idle", "running", "ready", "error"};

const Json& field(const Json& obj, std::string_view key) {
    static const Json kNull;
    if (!obj.is_object()) {
        return kNull;
    }
    const auto it = obj.find(key);
    return it == obj.end() ? kNull : *it;
}

template <std::size_t N>
const Json& one_of(const Json& v, const std::array<std::string_view, N>& options, const Json& fallback) {
    if (v.is_string() && std::ranges::find(options, v.get<std::string>()) != options.end()) {
        return v;
    }
    return fallback;
}

double clamp(double v, double lo, double hi) { return std::min(hi, std::max(lo, v)); }

// str(x or "")
std::string text_or_empty(const Json& v) { return py_truthy(v) ? py_str(v) : std::string(); }

Json normalize_edit(const Json& raw) {
    const Json e = raw.is_object() ? raw : Json::object();
    const Json op = one_of(field(e, "op"), std::array<std::string_view, 2>{"keep", "erase"}, Json("erase"));
    Json pts = Json::array();
    const Json& raw_points = field(e, "points");
    if (py_truthy(raw_points)) {
        constexpr double nan = std::numeric_limits<double>::quiet_NaN();
        for (const Json& p : py_iter(raw_points).value_or(std::vector<Json>{})) {
            if (!p.is_object()) {
                continue;
            }
            const double x = py_num_finite(field(p, "x"), nan);
            const double y = py_num_finite(field(p, "y"), nan);
            if (std::isnan(x) || std::isnan(y)) {
                continue;
            }
            Json pt = {{"x", x}, {"y", y}};
            if (py_truthy(field(p, "m"))) {
                pt["m"] = 1;
            }
            pts.push_back(std::move(pt));
        }
    }
    if (pts.empty()) {
        return nullptr;
    }
    Json out = {{"op", op}, {"size", clamp(py_num_finite(field(e, "size"), 0.08), 0.002, 1.0)}, {"points", pts}};
    const double t = py_num_finite(field(e, "t"), std::numeric_limits<double>::quiet_NaN());
    if (!std::isnan(t) && t >= 0) {
        out["t"] = py_round(t, 3);
    }
    const Json& tool = field(e, "tool");
    if (tool == "smart" || tool == "manual") {
        out["tool"] = tool;
    }
    return out;
}

Json normalize_auto(const Json& raw) {
    const Json a = raw.is_object() ? raw : Json::object();
    Json edits = Json::array();
    const Json& raw_edits = field(a, "edits");
    if (py_truthy(raw_edits)) {
        for (const Json& x : py_iter(raw_edits).value_or(std::vector<Json>{})) {
            Json e = normalize_edit(x);
            if (!e.is_null()) {
                edits.push_back(std::move(e));
            }
        }
    }
    const std::string error = text_or_empty(field(a, "error"));
    Json out = Json::object();
    out["enabled"] = py_truthy(field(a, "enabled"));
    out["provider"] = one_of(field(a, "provider"), kProviders, Json("u2net"));
    out["model_version"] = text_or_empty(field(a, "model_version"));
    out["base_key"] = text_or_empty(field(a, "base_key"));
    out["status"] = one_of(field(a, "status"), kStatus, Json("idle"));
    out["error"] = error.empty() ? Json(nullptr) : Json(error);
    out["mask_fps"] = static_cast<std::int64_t>(clamp(py_num_finite(field(a, "mask_fps"), 15), 1, 60));
    out["mask_height"] = static_cast<std::int64_t>(clamp(py_num_finite(field(a, "mask_height"), 512), 128, 1080));
    out["stabilize"] = clamp(py_num_finite(field(a, "stabilize"), 0.0), 0.0, 1.0);
    out["threshold"] = clamp(py_num_finite(field(a, "threshold"), 0.5), 0.0, 1.0);
    out["softness"] = clamp(py_num_finite(field(a, "softness"), 0.25), 0.0, 1.0);
    out["feather"] = clamp(py_num_finite(field(a, "feather"), 0.0), 0.0, 0.15);
    out["expansion"] = clamp(py_num_finite(field(a, "expansion"), 0.0), -1.0, 1.0);
    out["opacity"] = clamp(py_num_finite(field(a, "opacity"), 1.0), 0.0, 1.0);
    out["invert"] = py_truthy(field(a, "invert"));
    out["edits"] = std::move(edits);
    return out;
}

Json normalize_chroma(const Json& raw) {
    const Json c = raw.is_object() ? raw : Json::object();
    return {{"enabled", py_truthy(field(c, "enabled"))},
            {"color", normalize_hex(field(c, "color"))},
            {"similarity", clamp(py_num_finite(field(c, "similarity"), 0.20), 1e-5, 1.0)},
            {"blend", clamp(py_num_finite(field(c, "blend"), 0.10), 0.0, 1.0)},
            {"spill", clamp(py_num_finite(field(c, "spill"), 0.0), 0.0, 1.0)},
            {"edge", clamp(py_num_finite(field(c, "edge"), 0.0), 0.0, 1.0)},
            {"shrink", clamp(py_num_finite(field(c, "shrink"), 0.0), -1.0, 1.0)}};
}

Json normalize_outline(const Json& raw) {
    const Json o = raw.is_object() ? raw : Json::object();
    return {{"enabled", py_truthy(field(o, "enabled"))},
            {"color", normalize_hex(field(o, "color"), "#FFFFFF")},
            {"width", clamp(py_num_finite(field(o, "width"), 0.3), 0.0, 1.0)},
            {"soft", clamp(py_num_finite(field(o, "soft"), 0.0), 0.0, 1.0)},
            {"opacity", clamp(py_num_finite(field(o, "opacity"), 1.0), 0.0, 1.0)}};
}

} // namespace

std::string normalize_hex(const Json& raw, const std::string& fallback) {
    std::string text = detail::py_strip(text_or_empty(raw));
    if (text.starts_with("0x") || text.starts_with("0X")) {
        text = "#" + text.substr(2);
    }
    if (!text.starts_with("#")) {
        text = "#" + text;
    }
    auto chars = py_iter(Json(text.substr(1))).value_or(std::vector<Json>{});
    if (chars.size() == 3) {
        std::vector<Json> doubled;
        for (const auto& c : chars) {
            doubled.push_back(c);
            doubled.push_back(c);
        }
        chars = std::move(doubled);
    }
    std::string body;
    for (const auto& c : chars) {
        const std::string& s = c.get_ref<const std::string&>();
        if (s.size() != 1 || std::isxdigit(static_cast<unsigned char>(s[0])) == 0) {
            return fallback;
        }
        body += static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
    }
    if (body.size() != 6) {
        return fallback;
    }
    return "#" + body;
}

Json normalize_bg(const Json& raw) {
    if (!raw.is_object() || raw.empty()) {
        return nullptr;
    }
    const Json& mode = field(raw, "mode");
    const Json& enabled = field(raw, "enabled");
    Json out = Json::object();
    out["enabled"] = !(enabled.is_boolean() && !enabled.get<bool>());
    out["mode"] = mode == "auto" || mode == "chroma" ? mode : Json("auto");
    out["auto"] = normalize_auto(field(raw, "auto"));
    out["chroma"] = normalize_chroma(field(raw, "chroma"));
    out["outline"] = normalize_outline(field(raw, "outline"));
    return out;
}

bool bg_capable(const TimelineClip& clip) { return clip.kind == "video" || clip.kind == "image"; }

std::string recommended_bg_provider(const TimelineClip& clip) {
    if (clip.kind == "video") {
        return "rvm_mobilenetv3";
    }
    std::string name = clip.filename.substr(0, clip.filename.find('?'));
    name = name.substr(0, name.find('#'));
    std::ranges::transform(name, name.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return name.ends_with(".gif") ? "u2net" : "birefnet_lite";
}

std::int64_t bg_provider_mask_height(std::string_view provider) {
    if (provider == "rvm_mobilenetv3" || provider == "rvm_resnet50") {
        return 720;
    }
    return provider == "birefnet_lite" ? 1080 : 512;
}

} // namespace gc::core
