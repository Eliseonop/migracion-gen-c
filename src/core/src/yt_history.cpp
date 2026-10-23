#include "gc/core/yt_history.hpp"

#include <cstdint>
#include <string>
#include <utility>

#include "edit_util.hpp"
#include "json_reader.hpp"
#include "py.hpp"

namespace gc::core {

using detail::field;
using detail::invalid;
using detail::py_int;
using detail::py_str;
using detail::py_strip;
using detail::py_truthy;

namespace {

// str(v or ""): el texto de Python de un valor, o vacío si es falso.
std::string text_or_empty(const Json& v) { return py_truthy(v) ? py_str(v) : std::string(); }

// _as_items: solo las entradas que son objetos y tienen url, con sus campos como texto.
Result<Json> as_items(const Json& raw) {
    Json out = Json::array();
    if (!raw.is_array()) {
        return out;
    }
    for (const Json& x : raw) {
        if (!x.is_object()) {
            continue;
        }
        std::string url = py_strip(text_or_empty(field(x, "url")));
        if (url.empty()) {
            continue;
        }
        const Json& title = field(x, "title");
        const Json& at = field(x, "at");
        std::int64_t at_s = 0;
        if (py_truthy(at)) {
            const auto v = py_int(at); // int(x.get("at") or 0)
            if (!v) {
                return invalid("yt_history: «at» no es un entero");
            }
            at_s = *v;
        }
        Json item = Json::object();
        item["url"] = url;
        item["title"] = py_truthy(title) ? py_str(title) : url;
        item["video_id"] = text_or_empty(field(x, "video_id"));
        item["at"] = at_s;
        out.push_back(std::move(item));
    }
    return out;
}

// _key: el vídeo o, si no lo hay, la url.
std::string key_of(const Json& item) {
    std::string id = py_strip(item["video_id"].get_ref<const std::string&>());
    return id.empty() ? py_strip(item["url"].get_ref<const std::string&>()) : id;
}

std::string url_of(const Json& item) { return py_strip(item["url"].get_ref<const std::string&>()); }

Json without(const Json& items, const std::string& key, const std::string& url) {
    Json out = Json::array();
    for (const Json& x : items) {
        if (key_of(x) != key && url_of(x) != url) {
            out.push_back(x);
        }
    }
    return out;
}

Result<Json> stored_items(const SettingsFile& settings) { return as_items(field(settings.load(), "yt_history")); }

} // namespace

Result<Json> yt_history_record(SettingsFile& settings, std::string_view url, const Json& video, std::int64_t limit,
                               const UnixClock& now) {
    const std::string u = py_strip(url);
    auto items = stored_items(settings);
    if (!items || u.empty()) {
        return items;
    }
    // _video_fields: un objeto aporta id y title; null u otra cosa, nada.
    std::string video_id;
    std::string title;
    if (video.is_object()) {
        video_id = text_or_empty(field(video, "id"));
        title = text_or_empty(field(video, "title"));
    }
    const std::string key = video_id.empty() ? u : video_id;
    Json out = without(*items, key, u);
    Json entry = Json::object();
    entry["url"] = u;
    entry["title"] = title.empty() ? u : title;
    entry["video_id"] = video_id;
    entry["at"] = now();
    out.insert(out.begin(), std::move(entry));
    const std::int64_t cap = limit == 0 ? kYtHistoryLimit : (limit > 1 ? limit : 1); // max(1, int(limit or 40))
    if (std::cmp_greater(out.size(), cap)) {
        out.erase(out.begin() + cap, out.end());
    }
    if (auto saved = settings.save(Json{{"yt_history", out}}); !saved) {
        return std::unexpected(saved.error());
    }
    return out;
}

Result<Json> yt_history_remove(SettingsFile& settings, std::string_view url_or_id) {
    const std::string needle = py_strip(url_or_id);
    auto items = stored_items(settings);
    if (!items) {
        return items;
    }
    Json out = needle.empty() ? std::move(*items) : without(*items, needle, needle);
    if (auto saved = settings.save(Json{{"yt_history", out}}); !saved) {
        return std::unexpected(saved.error());
    }
    return out;
}

} // namespace gc::core
