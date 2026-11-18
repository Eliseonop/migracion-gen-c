#include "gc/core/clip_extract.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <limits>
#include <string>

#include "gc/core/segments.hpp"

namespace gc::core {

namespace {

constexpr double kMarkEps = 0.02; // un extremo puede quedar un poco fuera del clip

// Lo que quita String.prototype.trim: los espacios y finales de línea de ECMAScript, en UTF-8.
constexpr std::array<std::string_view, 25> kJsSpaces = {
    "\t",           "\n",           "\v",           "\f",           "\r",           " ",           "\xC2\xA0",
    "\xE1\x9A\x80", "\xE2\x80\x80", "\xE2\x80\x81", "\xE2\x80\x82", "\xE2\x80\x83", "\xE2\x80\x84", "\xE2\x80\x85",
    "\xE2\x80\x86", "\xE2\x80\x87", "\xE2\x80\x88", "\xE2\x80\x89", "\xE2\x80\x8A", "\xE2\x80\xA8", "\xE2\x80\xA9",
    "\xE2\x80\xAF", "\xE2\x81\x9F", "\xE3\x80\x80", "\xEF\xBB\xBF"};

std::string_view js_trim(std::string_view text) {
    for (bool again = true; again;) {
        again = false;
        for (const std::string_view space : kJsSpaces) {
            if (text.starts_with(space)) {
                text.remove_prefix(space.size());
                again = true;
            }
            if (text.ends_with(space)) {
                text.remove_suffix(space.size());
                again = true;
            }
        }
    }
    return text;
}

double js_round(double v) { // Math.round: el empate, hacia +∞
    const double r = std::floor(v);
    return v - r >= 0.5 ? r + 1 : r;
}

double r3(double v) { return js_round(v * 1000) / 1000; }

// Number(t) || 0 de un número: NaN cuenta como 0.
double or_zero(double t) { return std::isnan(t) ? 0.0 : t; }

// timelineToSource de editorModel.js: un instante de la timeline en segundos del archivo.
double timeline_to_source(const TimelineClip& c, double t) {
    const double local = (t - c.start) * ui_clip_speed(c);
    return c.reverse ? c.out_point - local : c.in_point + local;
}

// String(n) de JS para un entero ≥ 0 (o NaN o +∞) de mmss.
std::string js_integer(double n) {
    if (std::isnan(n)) {
        return "NaN";
    }
    if (std::isinf(n)) {
        return "Infinity";
    }
    return std::format("{:.0f}", n); // como String(n) mientras n < 1e21
}

std::string mmss(double s) {
    const double rounded = js_round(s);
    const double n = std::isnan(rounded) ? rounded : std::max(0.0, rounded); // Math.max(0, NaN) es NaN
    const double secs = std::isfinite(n) ? std::fmod(n, 60) : std::numeric_limits<double>::quiet_NaN();
    std::string tail = js_integer(secs);
    if (tail.size() < 2) {
        tail.insert(0, 2 - tail.size(), '0');
    }
    return js_integer(std::floor(n / 60)) + ":" + tail;
}

} // namespace

std::optional<double> mark_number(const Json& v) {
    if (v.is_null() || (v.is_string() && v.get_ref<const std::string&>().empty())) {
        return std::nullopt;
    }
    const double n = js_number(v);
    return std::isfinite(n) ? std::optional<double>(n) : std::nullopt;
}

ClipMark mark_from_json(const Json& mark) {
    if (!mark.is_object()) {
        return {};
    }
    const auto get = [&](const char* key) {
        const auto it = mark.find(key);
        return it != mark.end() ? mark_number(*it) : std::nullopt;
    };
    return {get("in"), get("out")};
}

Json mark_to_json(const ClipMark& mark) {
    return {{"in", mark.in ? Json(*mark.in) : Json()}, {"out", mark.out ? Json(*mark.out) : Json()}};
}

ClipMark set_mark(const ClipMark& mark, MarkEnd which, double t) {
    const double v = std::max(0.0, r3(or_zero(t)));
    ClipMark next = mark;
    (which == MarkEnd::In ? next.in : next.out) = v;
    if (next.in && next.out && *next.out <= *next.in) {
        (which == MarkEnd::In ? next.out : next.in).reset();
    }
    return next;
}

bool has_mark_range(const ClipMark& mark) { return mark.in && mark.out && *mark.out > *mark.in; }

ClipMark drag_mark(const ClipMark& mark, MarkEnd which, double t, double duration) {
    const double max = std::isfinite(duration) && duration > 0 ? duration : std::numeric_limits<double>::infinity();
    double v = std::min(max, std::max(0.0, or_zero(t)));
    if (which == MarkEnd::In && mark.out) {
        v = std::min(v, *mark.out - kMinSegment);
    }
    if (which == MarkEnd::Out && mark.in) {
        v = std::max(v, *mark.in + kMinSegment);
    }
    ClipMark next = mark;
    (which == MarkEnd::In ? next.in : next.out) = r3(std::max(0.0, v));
    return next;
}

std::expected<SourceRange, std::string> mark_to_source_range(const ClipList& clips, const ClipMark& mark) {
    if (!mark.in || !mark.out || *mark.out - *mark.in < kMinSegment) {
        return std::unexpected(std::string("Marca inicio (Z) y fin (X) para crear el clip."));
    }
    const double a = *mark.in;
    const double b = *mark.out;
    bool any_video = false;
    const TimelineClip* clip = nullptr;
    for (const auto& c : clips) {
        if (c->kind != "video") {
            continue;
        }
        any_video = true;
        const auto covers = [&](double t) { return t >= c->start - kMarkEps && t <= ui_clip_end(*c) + kMarkEps; };
        if (covers(a) && covers(b)) {
            clip = &*c;
            break;
        }
    }
    if (!any_video) {
        return std::unexpected(std::string("No hay vídeo en la timeline."));
    }
    if (clip == nullptr) {
        return std::unexpected(std::string("El rango cruza un corte de la timeline: ajústalo a un solo tramo."));
    }
    const double s1 = timeline_to_source(*clip, std::max(a, clip->start));
    const double s2 = timeline_to_source(*clip, std::min(b, ui_clip_end(*clip)));
    const double start = std::max(or_zero(clip->in_point), std::min(s1, s2)); // clip.in_point || 0
    const double end = std::min(clip->out_point, std::max(s1, s2));
    if (end - start < kMinSegment) {
        return std::unexpected(std::string("El rango es demasiado corto."));
    }
    return SourceRange{clip, r3(start), r3(end)};
}

std::string segment_label(std::string_view title, double start, double end) {
    std::string_view base = js_trim(title);
    if (base.empty()) {
        base = "Clip";
    }
    return std::string(base) + " \xC2\xB7 " + mmss(start) + "\xE2\x80\x93" + mmss(end);
}

std::string_view desc_mode_id(SegmentDescMode mode) {
    switch (mode) {
    case SegmentDescMode::Source:
        return "source";
    case SegmentDescMode::None:
        return "none";
    case SegmentDescMode::Manual:
        break;
    }
    return "manual";
}

SegmentDescMode desc_mode_from(std::string_view id) {
    if (id == "none") {
        return SegmentDescMode::None;
    }
    return id == "source" ? SegmentDescMode::Source : SegmentDescMode::Manual;
}

std::string segment_description(SegmentDescMode mode, std::string_view description,
                                std::string_view source_description) {
    switch (mode) {
    case SegmentDescMode::None:
        return {};
    case SegmentDescMode::Source:
        return std::string(js_trim(source_description));
    case SegmentDescMode::Manual:
        break;
    }
    return std::string(js_trim(description));
}

} // namespace gc::core
