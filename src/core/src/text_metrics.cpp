#include "gc/core/text_metrics.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <mutex>

#include "json_reader.hpp"
#include "py.hpp"

namespace gc::core {

using detail::py_float;
using detail::py_len;
using detail::py_num;
using detail::py_split;
using detail::py_str;
using detail::py_truthy;

namespace {

std::mutex g_mutex;
std::shared_ptr<const TextWidthFn> g_width;

const Json& field(const Json& obj, std::string_view key) {
    static const Json kNull;
    if (!obj.is_object()) {
        return kNull;
    }
    const auto it = obj.find(key);
    return it == obj.end() ? kNull : *it;
}

double estimate_width(double em, std::string_view text, double ls) {
    const auto n = static_cast<double>(py_len(text));
    return n * em * 0.55 + ls * n;
}

} // namespace

void set_text_width(TextWidthFn fn) {
    const std::lock_guard lock(g_mutex);
    g_width = fn ? std::make_shared<const TextWidthFn>(std::move(fn)) : nullptr;
}

double text_width(std::string_view font, bool bold, double em, std::string_view text, double letter_spacing) {
    std::shared_ptr<const TextWidthFn> fn;
    {
        const std::lock_guard lock(g_mutex);
        fn = g_width;
    }
    return fn ? (*fn)(font, bold, em, text, letter_spacing) : estimate_width(em, text, letter_spacing);
}

double text_style_scale(const Json& style) {
    // float(st.get("scale") or 1); lo que falla, ≤ 0 o NaN vale 1
    const Json& raw = field(style, "scale");
    const auto s = py_float(py_truthy(raw) ? raw : Json(1));
    if (!s || *s <= 0 || std::isnan(*s)) {
        return 1.0;
    }
    return *s;
}

double text_font_px(const Json& style, double height) {
    return std::max(0.0, py_num(field(style, "size"), 0.048)) * height * text_style_scale(style);
}

double text_letter_spacing_px(const Json& style, double em) {
    return std::min(1.0, std::max(-0.5, py_num(field(style, "letter_spacing"), 0.0))) * em;
}

double text_line_height(const Json& style) {
    return std::min(3.0, std::max(0.6, py_num(field(style, "line_height"), 1.22)));
}

std::vector<std::vector<std::string>> text_rows(const Json& style, double width, double height,
                                                std::string_view text) {
    const double em = text_font_px(style, height);
    const double ls = text_letter_spacing_px(style, em);
    const double box = std::max(0.1, std::min(1.0, py_num(field(style, "w"), 0.8))) * width * text_style_scale(style);
    const double max_w = (box - em * 0.4) * 1.02; // el navegador ya partió el texto: no volver a partir por un pelo
    const Json& font_raw = field(style, "font");
    const std::string font = py_truthy(font_raw) ? py_str(font_raw) : "Arial";
    const bool bold = !style.is_object() || !style.contains("bold") || py_truthy(style.at("bold"));

    std::vector<std::vector<std::string>> rows;
    std::size_t i = 0;
    while (i <= text.size()) {
        const auto j = std::min(text.find('\n', i), text.size());
        const auto words = py_split(text.substr(i, j - i));
        i = j + 1;
        if (words.empty()) {
            rows.emplace_back();
            continue;
        }
        std::vector<std::string> row{words.front()};
        std::string joined = words.front();
        for (std::size_t k = 1; k < words.size(); ++k) {
            const std::string candidate = joined + " " + words[k];
            if (text_width(font, bold, em, candidate, ls) <= max_w) {
                row.push_back(words[k]);
                joined = candidate;
            } else {
                rows.push_back(std::move(row));
                row = {words[k]};
                joined = words[k];
            }
        }
        rows.push_back(std::move(row));
    }
    if (rows.empty()) {
        rows.emplace_back();
    }
    return rows;
}

} // namespace gc::core
