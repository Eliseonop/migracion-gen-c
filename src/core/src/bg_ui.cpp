#include "gc/core/bg_ui.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <string>

#include "gc/core/bg.hpp"
#include "gc/core/timeline_ui.hpp"

namespace gc::core {

namespace {

constexpr std::array<BgProviderInfo, 8> kCatalog{{
    {"rvm_mobilenetv3", "RVM · personas en vídeo",
     "Para personas en vídeo: recuerda los fotogramas anteriores, así que el borde no parpadea ni baila. Rápido "
     "(15 MB). Solo recorta personas.",
     true, false},
    {"rvm_resnet50", "RVM · personas (más detalle)",
     "Como RVM pero con una red más grande (103 MB): algo más de detalle en pelo y ropa, unas 2 veces más lento. "
     "Solo recorta personas.",
     true, false},
    {"birefnet_lite", "BiRefNet · cualquier sujeto",
     "Objetos, animales, productos o personas, con el mejor borde. Pesado (224 MB, varios segundos por fotograma "
     "sin GPU): ideal para imágenes.",
     false, false},
    {"u2net", "U²-Net · rápido", "El motor anterior: cualquier sujeto y rápido, pero con un borde más tosco.", false,
     false},
    {"u2netp", "U²-Net lite", "Modelo de 4,7 MB: el más rápido y el más basto.", false, false},
    {"sam21_base_plus", "SAM 2.1 (calidad)", "Buen equilibrio entre calidad y velocidad (259 MB).", false, true},
    {"sam21_large", "SAM 2.1 large (máxima)", "Máxima calidad, el más lento (768 MB).", false, true},
    {"sam21_tiny", "SAM 2.1 tiny (rápido)", "El más rápido y ligero (111 MB).", false, true},
}};

constexpr std::string_view kDefaultProvider = "u2net"; // DEFAULT_PROVIDER

// {...base, ...patch} de JS sobre objetos.
Json spread(Json base, const Json& patch) {
    if (!base.is_object()) {
        base = Json::object();
    }
    if (patch.is_object()) {
        for (const auto& [k, v] : patch.items()) {
            base[k] = v;
        }
    }
    return base;
}

// providerMaskHeight sobre un id cualquiera (lo que no es un texto no es ningún motor: 512).
std::int64_t mask_height_of(const Json& provider) {
    return bg_provider_mask_height(provider.is_string() ? provider.get_ref<const std::string&>() : std::string_view());
}

// bgClipDescriptor: el material y su tramo, no el id (el clip puede no estar guardado aún).
Json descriptor(const TimelineClip& clip) {
    return {{"clip_id", clip.id},
            {"kind", clip.kind},
            {"asset_kind", clip.asset_kind},
            {"asset_id", clip.asset_id},
            {"filename", clip.filename},
            {"asset_scope", clip.asset_scope.empty() ? std::string("project") : clip.asset_scope},
            {"in_point", clip.in_point},
            {"out_point", clip.out_point},
            {"source_duration", clip.source_duration}};
}

} // namespace

std::span<const BgProviderInfo> bg_provider_catalog() { return kCatalog; }

bool is_interactive_bg_provider(const Json& provider) {
    return provider.is_string() && provider.get_ref<const std::string&>().starts_with("sam");
}

Json clip_bg_or_default(const TimelineClip& clip) {
    Json bg = normalize_bg(clip.bg_removal.value_or(Json()));
    if (bg.is_null()) { // defaultBg()
        bg = normalize_bg(
            {{"enabled", true}, {"mode", "auto"}, {"auto", Json::object()}, {"chroma", Json::object()}});
    }
    return bg;
}

Json bg_patch_auto(const TimelineClip& clip, const Json& patch) {
    Json bg = clip_bg_or_default(clip);
    bg["auto"] = spread(bg["auto"], patch);
    return normalize_bg(bg);
}

Json bg_patch_chroma(const TimelineClip& clip, const Json& patch) {
    Json bg = clip_bg_or_default(clip);
    bg["chroma"] = spread(bg["chroma"], patch);
    return normalize_bg(bg);
}

Json bg_patch_outline(const TimelineClip& clip, const Json& patch) {
    Json bg = clip_bg_or_default(clip);
    bg["outline"] = spread(bg["outline"], patch);
    return normalize_bg(bg);
}

Json bg_reset_chroma(const TimelineClip& clip) {
    Json bg = clip_bg_or_default(clip);
    bg["chroma"] = Json{{"enabled", bg["chroma"].value("enabled", false)}};
    return normalize_bg(bg);
}

Json bg_toggle_auto(const TimelineClip& clip, bool on) {
    const Json bg = clip_bg_or_default(clip);
    const Json& a = bg.at("auto");
    const bool from_sam = on && is_interactive_bg_provider(a.at("provider"));
    const bool pristine = on && a.at("base_key").get_ref<const std::string&>().empty() && a.at("status") == "idle" &&
                          a.at("provider") == std::string(kDefaultProvider);
    Json patch = {{"enabled", on}};
    if (from_sam || pristine) {
        const std::string provider = recommended_bg_provider(clip);
        patch["provider"] = provider;
        patch["mask_height"] = bg_provider_mask_height(provider);
    }
    if (from_sam) {
        patch["edits"] = Json::array();
        patch["base_key"] = "";
        patch["status"] = "idle";
        patch["error"] = nullptr;
    }
    return bg_patch_auto(clip, patch);
}

Json bg_change_auto(const TimelineClip& clip, const Json& patch) {
    if (!patch.is_object()) {
        return bg_patch_auto(clip, Json::object());
    }
    Json next = patch;
    // Con `value` y `contains`: un iterador de nlohmann da un falso -Wnull-dereference en GCC 15.
    const bool new_provider = patch.contains("provider");
    const Json provider = patch.value("provider", Json());
    if (new_provider && !patch.contains("mask_height") && !is_interactive_bg_provider(provider)) {
        next["mask_height"] = mask_height_of(provider);
    }
    if (new_provider || patch.contains("stabilize") || patch.contains("mask_height")) {
        next["base_key"] = "";
        next["status"] = "idle";
        next["error"] = nullptr;
    }
    return bg_patch_auto(clip, next);
}

std::optional<BgAutoStart> bg_auto_start(const TimelineClip& clip) {
    if (!bg_capable(clip)) {
        return std::nullopt;
    }
    const Json bg = clip_bg_or_default(clip);
    Json body = descriptor(clip);
    body["auto"] = bg.at("auto");
    return BgAutoStart{std::move(body),
                       bg_patch_auto(clip, {{"enabled", true}, {"status", "running"}, {"error", nullptr}})};
}

std::string preferred_sam_provider(const Json& current, const Json& catalog) {
    if (is_interactive_bg_provider(current)) {
        return current.get<std::string>();
    }
    for (const char* id : {"sam21_base_plus", "sam21_large", "sam21_tiny"}) {
        if (catalog.is_array() && std::ranges::any_of(catalog, [&](const Json& p) {
                return p.is_object() && p.value("id", Json()) == id && p.value("interactive", false) &&
                       p.value("downloaded", false);
            })) {
            return id;
        }
    }
    return "sam21_base_plus";
}

Json bg_toggle_custom(const TimelineClip& clip, bool on, const Json& catalog) {
    if (!on) {
        return bg_patch_auto(clip, {{"enabled", false}});
    }
    const Json bg = clip_bg_or_default(clip);
    const Json current = bg.at("auto").value("provider", Json());
    Json patch = {{"enabled", true}, {"provider", preferred_sam_provider(current, catalog)}};
    if (!is_interactive_bg_provider(current)) {
        patch["edits"] = Json::array();
        patch["base_key"] = "";
        patch["status"] = "idle";
        patch["error"] = nullptr;
        patch["mask_height"] = 512;
    }
    return bg_patch_auto(clip, patch);
}

namespace {

double fixed(double v, int digits) { return std::stod(js_to_fixed(v, digits)); }

std::int64_t fps_of(const Json& mask_fps) {
    const double n = mask_fps.is_number() ? mask_fps.get<double>() : 0.0;
    return std::max<std::int64_t>(1, n != 0.0 ? static_cast<std::int64_t>(std::floor(n + 0.5)) : 15);
}

std::optional<std::int64_t> mark_frame(const Json& edit, const Json& mask_fps) {
    const Json t = edit.is_object() ? edit.value("t", Json()) : Json();
    if (!t.is_number() || !std::isfinite(t.get<double>())) {
        return std::nullopt;
    }
    return bg_matte_frame_index(t.get<double>(), mask_fps);
}

} // namespace

Json bg_new_stroke(const TimelineClip& clip, std::string_view op, std::string_view tool, double size, double head,
                   Point first) {
    Json stroke = {{"op", op == "keep" ? "keep" : "erase"}, {"size", size != 0.0 ? size : 0.08}};
    const Json bg = normalize_bg(clip.bg_removal.value_or(Json()));
    if (bg.is_object() && is_interactive_bg_provider(bg["auto"].value("provider", Json()))) {
        stroke["t"] = fixed(source_time(clip, head, clip.start), 3);
        stroke["tool"] = tool == "manual" ? "manual" : "smart";
    }
    stroke["points"] = Json::array({{{"x", fixed(first.x, 4)}, {"y", fixed(first.y, 4)}, {"m", 1}}});
    return stroke;
}

std::optional<Json> bg_stroke_point(Point last, Point next) {
    if (std::hypot(next.x - last.x, next.y - last.y) < 0.003) {
        return std::nullopt;
    }
    return Json{{"x", fixed(next.x, 4)}, {"y", fixed(next.y, 4)}};
}

Json bg_add_stroke(const TimelineClip& clip, const Json& stroke) {
    Json bg = clip_bg_or_default(clip);
    bg["auto"]["edits"].push_back(stroke);
    return normalize_bg(bg);
}

std::optional<Json> bg_extend_stroke(const TimelineClip& clip, const Json& point) {
    Json bg = normalize_bg(clip.bg_removal.value_or(Json()));
    if (bg.is_null() || bg["auto"]["edits"].empty()) {
        return std::nullopt;
    }
    bg["auto"]["edits"].back()["points"].push_back(point);
    return normalize_bg(bg);
}

Json bg_undo_edit(const TimelineClip& clip) {
    Json bg = clip_bg_or_default(clip);
    if (!bg["auto"]["edits"].empty()) {
        bg["auto"]["edits"].erase(bg["auto"]["edits"].size() - 1);
    }
    return normalize_bg(bg);
}

Json bg_clear_edits(const TimelineClip& clip) { return bg_patch_auto(clip, {{"edits", Json::array()}}); }

std::int64_t bg_matte_frame_index(double src_t, const Json& mask_fps) {
    const double t = std::isfinite(src_t) ? std::max(0.0, src_t) : 0.0;
    return std::max<std::int64_t>(0, static_cast<std::int64_t>(std::floor((t * static_cast<double>(fps_of(mask_fps))) + 0.5)));
}

double bg_matte_frame_time(std::int64_t index, const Json& mask_fps) {
    return static_cast<double>(std::max<std::int64_t>(0, index)) / static_cast<double>(fps_of(mask_fps));
}

Json bg_edits_at_frame(const Json& edits, std::int64_t index, const Json& mask_fps) {
    Json out = Json::array();
    if (edits.is_array()) {
        for (const Json& e : edits) {
            const auto k = mark_frame(e, mask_fps);
            if (!k || *k == index) {
                out.push_back(e);
            }
        }
    }
    return out;
}

Json bg_mark_frames(const Json& edits, const Json& mask_fps) {
    std::map<std::int64_t, std::int64_t> by;
    if (edits.is_array()) {
        for (const Json& e : edits) {
            if (const auto k = mark_frame(e, mask_fps)) {
                ++by[*k];
            }
        }
    }
    Json out = Json::array();
    for (const auto& [idx, count] : by) {
        out.push_back({{"idx", idx}, {"t", bg_matte_frame_time(idx, mask_fps)}, {"count", count}});
    }
    return out;
}

bool bg_manual_mark(const Json& edit) { return edit.is_object() && edit.value("tool", Json()) == "manual"; }

std::optional<Point> bg_frame_to_source(const FrameLayer& layer, double x, double y, double src_w, double src_h) {
    if (!(src_w > 0) || !(src_h > 0)) {
        return std::nullopt;
    }
    for (const Blit& b : layer.blits) {
        const double det = (b.m.a * b.m.d) - (b.m.b * b.m.c);
        if (det == 0.0) {
            continue;
        }
        const double dx = x - b.m.e;
        const double dy = y - b.m.f;
        const double sx = ((b.m.d * dx) - (b.m.c * dy)) / det;
        const double sy = ((b.m.a * dy) - (b.m.b * dx)) / det;
        if (sx >= b.src.sx && sx <= b.src.sx + b.src.sw && sy >= b.src.sy && sy <= b.src.sy + b.src.sh) {
            return Point{sx / src_w, sy / src_h};
        }
    }
    return std::nullopt;
}

double bg_brush_radius(const FrameLayer& layer, double size, double src_h) {
    if (layer.blits.empty()) {
        return 0.0;
    }
    const Affine& m = layer.blits.front().m;
    return size * src_h * std::hypot(m.c, m.d) / 2;
}

std::optional<Json> bg_cutout_body(const TimelineClip& clip) {
    if (!bg_capable(clip)) {
        return std::nullopt;
    }
    const Json bg = normalize_bg(clip.bg_removal.value_or(Json()));
    if (bg.is_null() || !(bg_auto_active(bg) || bg["chroma"].value("enabled", false))) {
        return std::nullopt;
    }
    Json body = descriptor(clip);
    if (clip.name && !clip.name->empty()) { // label: clip.name || undefined
        body["label"] = *clip.name;
    }
    body["bg_removal"] = *clip.bg_removal;
    return body;
}

std::optional<Json> bg_auto_finished(const TimelineClip& clip, const Json& job) {
    if (!job.is_object()) {
        return std::nullopt;
    }
    const Json& status = job.value("status", Json());
    const auto result = job.find("bg_removal");
    if (status == "done" && result != job.end() && result->is_object()) {
        Json patch = {{"enabled", true}, {"status", "ready"}, {"error", nullptr}};
        Json bg = clip_bg_or_default(clip);
        Json a = spread(bg["auto"], patch);
        // r.base_key, r.provider…: lo que no trae el resultado queda undefined (el valor por defecto).
        for (const char* key : {"base_key", "provider", "model_version", "mask_fps", "mask_height"}) {
            const auto it = result->find(key);
            if (it != result->end()) {
                a[key] = *it;
            } else {
                a.erase(key);
            }
        }
        bg["auto"] = std::move(a);
        return normalize_bg(bg);
    }
    if (status == "error") {
        const auto error = job.find("error");
        const bool given = error != job.end() && error->is_string() && !error->get_ref<const std::string&>().empty();
        return bg_patch_auto(clip, {{"status", "error"}, {"error", given ? *error : Json("Error")}});
    }
    return std::nullopt;
}

} // namespace gc::core
