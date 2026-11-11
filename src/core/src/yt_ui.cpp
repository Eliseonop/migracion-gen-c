#include "gc/core/yt_ui.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <vector>

#include "gc/core/timeline_ui.hpp"

namespace gc::core {

namespace {

// String(x) de un número de JS: entero sin decimales.
std::string js_num_text(double v) {
    if (std::isfinite(v) && v == std::floor(v) && std::abs(v) < 1e21) {
        return std::format("{}", static_cast<long long>(v));
    }
    return std::format("{}", v);
}

// x.trim() de un texto de JS (lo que no es texto pasa por String; null/undefined, «»).
std::string trimmed(const Json& v) {
    std::string s = v.is_string() ? v.get<std::string>() : v.is_null() ? std::string() : v.dump();
    const auto first = s.find_first_not_of(" \t\r\n\f\v");
    if (first == std::string::npos) {
        return {};
    }
    return s.substr(first, s.find_last_not_of(" \t\r\n\f\v") - first + 1);
}

// (x || '') de JS sobre un campo de un objeto.
Json field_or_empty(const Json& item, const char* key) {
    const Json v = item.is_object() ? item.value(key, Json()) : Json();
    const bool falsy = v.is_null() || (v.is_string() && v.get_ref<const std::string&>().empty()) ||
                       (v.is_boolean() && !v.get<bool>()) || (v.is_number() && (v.get<double>() == 0 || std::isnan(v.get<double>())));
    return falsy ? Json("") : v;
}

} // namespace

Json yt_video_qualities(const Json& result) {
    const Json list = result.is_object() && result.value("video", Json()).is_object()
                          ? result["video"].value("qualities", Json())
                          : Json();
    std::vector<Json> out;
    if (list.is_array()) {
        for (const Json& x : list) {
            if (x.is_object() && js_number(x.value("quality", Json())) > 0) {
                out.push_back(x);
            }
        }
    }
    std::ranges::stable_sort(out, [](const Json& a, const Json& b) {
        return js_number(a.value("quality", Json())) > js_number(b.value("quality", Json()));
    });
    return out;
}

std::string yt_quality_short(const Json& quality) {
    const double q = js_number(quality);
    if (q >= 4320) {
        return "8K";
    }
    if (q >= 2160) {
        return "4K";
    }
    return (quality.is_string() ? quality.get<std::string>() : js_num_text(q)) + "p";
}

std::string yt_quality_tag(const Json& quality) {
    // TAGS[q]: la clave es el texto del número.
    const std::string key = quality.is_string() ? quality.get<std::string>() : quality.is_number() ? js_num_text(quality.get<double>()) : std::string();
    if (key == "4320") {
        return "8K";
    }
    if (key == "2160") {
        return "4K";
    }
    if (key == "1440") {
        return "2K";
    }
    if (key == "1080") {
        return "Full HD";
    }
    return key == "720" ? "HD" : "";
}

std::string yt_fmt_bytes(const Json& bytes) {
    const double raw = js_number(bytes);
    const double b = std::isnan(raw) ? 0 : raw;
    if (b <= 0) {
        return {};
    }
    constexpr double kGb = 1024.0 * 1024.0 * 1024.0;
    if (b >= kGb) {
        return js_to_fixed(b / kGb, 1) + " GB";
    }
    return js_num_text(std::max(1.0, std::floor((b / (1024.0 * 1024.0)) + 0.5))) + " MB";
}

std::string yt_quality_meta(const Json& item) {
    std::vector<std::string> parts;
    const Json fps = item.is_object() ? item.value("fps", Json()) : Json();
    if (fps.is_number() && fps.get<double>() != 0 && fps.get<double>() > 30) {
        parts.push_back(js_num_text(fps.get<double>()) + " fps");
    }
    if (const std::string size = yt_fmt_bytes(item.is_object() ? item.value("size", Json()) : Json()); !size.empty()) {
        parts.push_back("≈ " + size);
    }
    std::string out;
    for (const std::string& p : parts) {
        out += (out.empty() ? "" : " · ") + p;
    }
    return out;
}

Json yt_normalize_history(const Json& raw) {
    Json out = Json::array();
    if (raw.is_array()) {
        for (const Json& x : raw) {
            if (x.is_object() && !trimmed(field_or_empty(x, "url")).empty()) {
                out.push_back(x);
            }
        }
    }
    return out;
}

Json yt_history_without(const Json& history, const Json& item) {
    const auto key_of = [](const Json& x) {
        const Json id = field_or_empty(x, "video_id");
        return trimmed(id.is_string() && id.get_ref<const std::string&>().empty() ? field_or_empty(x, "url") : id);
    };
    const std::string needle = key_of(item);
    const std::string url = trimmed(field_or_empty(item, "url"));
    Json out = Json::array();
    for (const Json& x : history.is_array() ? history : Json::array()) {
        if (key_of(x) != needle && trimmed(field_or_empty(x, "url")) != url) {
            out.push_back(x);
        }
    }
    return out;
}

} // namespace gc::core
