#include "gc/core/bg_ui.hpp"

#include <array>
#include <string>

#include "gc/core/bg.hpp"

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
    // bgClipDescriptor: el material y su tramo, no el id (el clip puede no estar guardado aún).
    Json body = {{"clip_id", clip.id},
                 {"kind", clip.kind},
                 {"asset_kind", clip.asset_kind},
                 {"asset_id", clip.asset_id},
                 {"filename", clip.filename},
                 {"asset_scope", clip.asset_scope.empty() ? std::string("project") : clip.asset_scope},
                 {"in_point", clip.in_point},
                 {"out_point", clip.out_point},
                 {"source_duration", clip.source_duration},
                 {"auto", bg.at("auto")}};
    return BgAutoStart{std::move(body),
                       bg_patch_auto(clip, {{"enabled", true}, {"status", "running"}, {"error", nullptr}})};
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
