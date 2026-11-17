#include "gc/core/project_format.hpp"

#include <algorithm>
#include <cmath>

namespace gc::core {

namespace {

double js_round(double v) { // Math.round
    return std::floor(v + 0.5);
}

const AspectChoice* find_aspect(std::string_view id) {
    for (const AspectChoice& a : kProjectAspects) {
        if (a.id == id) {
            return &a;
        }
    }
    return nullptr;
}

} // namespace

std::int64_t format_even_dim(double n) {
    const double x = std::isnan(n) ? 0.0 : js_round(n);
    const double c = std::min<double>(kFormatDimMax, std::max<double>(kFormatDimMin, x));
    return static_cast<std::int64_t>(js_round(c / 2.0) * 2.0);
}

FrameSize size_for_ratio(double rw, double rh, double short_side) {
    const double r = rw / rh;
    if (!(r > 0) || !std::isfinite(r)) {
        return {format_even_dim(short_side), format_even_dim(short_side)};
    }
    // Math.max(DIM_MIN, Number(shortSide) || 0): NaN cuenta como 0.
    double s = std::max<double>(kFormatDimMin, std::isnan(short_side) ? 0.0 : short_side);
    const double k = r >= 1 ? r : 1 / r;
    if (s * k > kFormatDimMax) {
        s = kFormatDimMax / k;
    }
    const double lng = s * k;
    return r >= 1 ? FrameSize{format_even_dim(lng), format_even_dim(s)}
                  : FrameSize{format_even_dim(s), format_even_dim(lng)};
}

std::string aspect_of(double w, double h) {
    if (!(w > 0 && h > 0)) {
        return "custom";
    }
    const double r = w / h;
    for (const AspectChoice& a : kProjectAspects) {
        const double target = static_cast<double>(a.rw) / a.rh;
        if (std::abs(r - target) / target < 0.01) {
            return std::string(a.id);
        }
    }
    return "custom";
}

int resolution_of(double w, double h) {
    const double s = std::min(w, h);
    for (const ResolutionChoice& r : kProjectResolutions) {
        if (r.id == s) {
            return r.id;
        }
    }
    return 0;
}

FrameSize with_aspect(std::string_view aspect_id, std::int64_t w, std::int64_t h) {
    const AspectChoice* a = find_aspect(aspect_id);
    if (a == nullptr) {
        return {w, h};
    }
    return size_for_ratio(a->rw, a->rh, static_cast<double>(std::min(w, h)));
}

FrameSize with_resolution(double short_side, std::int64_t w, std::int64_t h) {
    const auto dw = static_cast<double>(w);
    const auto dh = static_cast<double>(h);
    const AspectChoice* a = find_aspect(aspect_of(dw, dh));
    return a != nullptr ? size_for_ratio(a->rw, a->rh, short_side) : size_for_ratio(dw, dh, short_side);
}

FrameSize with_media_aspect(double mw, double mh, std::int64_t w, std::int64_t h) {
    if (!(mw > 0 && mh > 0)) {
        return {w, h};
    }
    return size_for_ratio(mw, mh, static_cast<double>(std::min(w, h)));
}

} // namespace gc::core
