#include "gc/core/clip_fx.hpp"

#include <algorithm>

namespace gc::core {

namespace {

// `val or default` de Python: un texto vacío vale "none".
const std::string& field_or_none(const std::string& value) {
    static const std::string kNone = "none";
    return value.empty() ? kNone : value;
}

// min(1.0, max(0.0, v)) de Python (con NaN da 0).
double clamp01(double v) { return std::min(1.0, std::max(0.0, v)); }

double lerp(double a, double b, double t) { return a + (b - a) * t; }

bool fades(const std::string& kind) { return kind == "fade" || kind == "pop" || kind == "dissolve"; }

} // namespace

std::pair<double, double> fx_windows(double duration) {
    const double d = std::min(kFxDur, std::max(0.0, duration) / 2.0);
    return {d, d};
}

ClipFx clip_fx_at(const TimelineClip& clip, double local_t, double duration) {
    const std::string& appear = field_or_none(clip.appear);
    const std::string& exit = field_or_none(clip.exit);
    const auto [ad, ed] = fx_windows(duration);
    const double ap = ad > 0 ? clamp01(local_t / ad) : 1.0;
    const double ep = ed > 0 ? clamp01((duration - local_t) / ed) : 1.0;

    ClipFx fx;
    fx.look = field_or_none(clip.look);
    if (fades(appear)) {
        fx.opacity *= ap;
    }
    if (fades(exit)) {
        fx.opacity *= ep;
    }

    if (appear == "zoom") {
        fx.scale *= lerp(kFxZoomFrom, 1.0, ap);
    }
    if (exit == "zoom") {
        fx.scale *= lerp(1.0, kFxZoomFrom, 1.0 - ep);
    }
    if (appear == "pop") {
        fx.scale *= lerp(kFxPopFrom, 1.0, ap);
    }
    if (exit == "pop") {
        fx.scale *= lerp(1.0, kFxPopFrom, 1.0 - ep);
    }

    if (appear == "slide_left") {
        fx.tx += lerp(1.0, 0.0, ap);
    }
    if (appear == "slide_up") {
        fx.ty += lerp(1.0, 0.0, ap);
    }
    if (exit == "slide_right") {
        fx.tx += lerp(0.0, 1.0, 1.0 - ep);
    }
    if (exit == "slide_down") {
        fx.ty += lerp(0.0, 1.0, 1.0 - ep);
    }

    if (appear == "wipe") {
        fx.wipe = std::min(fx.wipe, ap);
    }
    if (exit == "wipe") {
        fx.wipe = std::min(fx.wipe, ep);
    }
    return fx;
}

} // namespace gc::core
