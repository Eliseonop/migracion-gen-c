#include "gc/core/mcp_capabilities.hpp"

#include <algorithm>
#include <utility>

#include "gc/core/audio_export.hpp"
#include "gc/core/clip_filters.hpp"
#include "gc/core/mcp_dto.hpp"
#include "gc/core/mcp_help.hpp"
#include "gc/core/settings.hpp"

#include "json_reader.hpp"
#include "py.hpp"

namespace gc::core::mcp {

namespace {

template <std::size_t N>
Json list(const std::array<std::string_view, N>& values) {
    Json out = Json::array();
    for (const std::string_view v : values) {
        out.push_back(v);
    }
    return out;
}

Json list(const std::vector<std::string>& values) {
    return Json(values);
}

// Los motores de TTS disponibles: los que tienen `available` verdadero (al estilo de Python).
Json available_engines(const RuntimeEnv& env) {
    Json out = Json::array();
    if (env.tts_engines.is_array()) {
        for (const Json& e : env.tts_engines) {
            if (e.is_object() && e.contains("available") && detail::py_truthy(e["available"])) {
                out.push_back(e.contains("id") ? e["id"] : Json(nullptr));
            }
        }
    }
    return out;
}

// _DOMAIN_VALUES: los valores válidos de los dominios que los tienen.
Json domain_values(std::string_view d, const RuntimeEnv& env) {
    if (d == "transcription") {
        return {{"models", transcribe_models(env)}, {"languages", "código ISO (es, en, …) o None = autodetección"}};
    }
    if (d == "audio") {
        return {{"tts_engines", env.tts_engines},
                {"audio_fx", list(audio_fx_ids())},
                {"sfx_categories", list(env.sfx_categories)},
                {"volume", "0–2 (1 = 100%)"},
                {"fade", Json::array({"in", "out"})}};
    }
    if (d == "clips") {
        Json looks = list(look_ids());
        return {{"motions", list(kMotions)},
                {"transitions", list(kTransitions)},
                {"visual_fx", list(kVisualFx)},
                {"mask_types", list(kMaskTypes)},
                {"shape_dash", list(kDashStyles)},
                {"looks", looks},
                {"filters", looks},
                {"positions", list(kPositions)},
                {"reframe_modes", list(kReframeModes)},
                {"speed", "0.1–10 (keep_pitch, reverse)"},
                {"opacity", "0–1"}};
    }
    if (d == "media" || d == "workflow") {
        return {{"crop_modes", list(kCropModes)}};
    }
    if (d == "project") {
        return {{"aspects", list(kAspects)}};
    }
    if (d == "text") {
        return {{"roles", list(kTextRoles)}};
    }
    if (d == "timeline") {
        return {{"track_kinds", list(kTrackKinds)}};
    }
    return nullptr;
}

} // namespace

std::vector<std::string> audio_fx_ids() {
    std::vector<std::string> out;
    for (const AudioFxDef& fx : audio_fx_catalog()) {
        out.emplace_back(fx.id);
    }
    return out;
}

std::vector<std::string> look_ids() {
    std::vector<std::string> out;
    for (const ColorFilterDef& f : color_filters()) {
        out.emplace_back(f.id);
    }
    return out;
}

Json verbs_by_domain() {
    Json out = Json::object();
    for (const ToolDomain& t : tool_domains()) {
        Json& verbs = out[std::string(t.domain)];
        if (verbs.is_null()) {
            verbs = Json::array();
        }
        verbs.push_back(t.tool);
    }
    for (Json& verbs : out) {
        auto sorted = verbs.get<std::vector<std::string>>();
        std::ranges::sort(sorted);
        verbs = sorted;
    }
    return out;
}

Json transcribe_models(const RuntimeEnv& env) {
    Json out = transcribe_model_info();
    for (Json& m : out) {
        m["default"] = m["id"] == env.transcribe_model;
    }
    return out;
}

Json overview(const RuntimeEnv& env) {
    const Json vb = verbs_by_domain();
    std::vector<std::string> names;
    for (const auto& [domain, verbs] : vb.items()) {
        names.push_back(domain);
    }
    std::ranges::sort(names);
    Json domains = Json::array();
    for (const std::string& d : names) {
        domains.push_back({{"domain", d}, {"verbs", vb[d]}});
    }
    return {{"domains", std::move(domains)},
            {"defaults",
             {{"aspect", "9:16"},
              {"transcribe_model", env.transcribe_model},
              {"tts_engines_available", available_engines(env)}}},
            {"hint", "describe_capabilities(domain) para valores válidos y la guía del dominio; "
                     "get_project_context(project_id) para el estado del proyecto; "
                     "list_projects/resolve_project para elegir proyecto."}};
}

Result<Json> describe(std::string_view domain, const RuntimeEnv& env) {
    const std::string d = detail::py_lower(detail::py_strip(domain));
    const Json vb = verbs_by_domain();
    const auto guide_text = guide(d);
    if (!vb.contains(d) && !guide_text) {
        std::vector<std::string> valid;
        for (const auto& [name, verbs] : vb.items()) {
            valid.push_back(name);
        }
        for (const DomainGuide& g : help_guides()) {
            valid.emplace_back(g.domain);
        }
        std::ranges::sort(valid);
        const auto [first, last] = std::ranges::unique(valid);
        valid.erase(first, last);
        return fail(ErrorCode::InvalidArgument,
                    "Dominio desconocido: " + std::string(domain) + ". Válidos: " + detail::py_repr(Json(valid)));
    }
    Json out = {{"domain", d},
                {"verbs", vb.contains(d) ? vb[d] : Json::array()},
                {"guide", guide_text ? Json(*guide_text) : Json(nullptr)}};
    if (Json values = domain_values(d, env); !values.is_null()) {
        out["values"] = std::move(values);
    }
    return out;
}

Json runtime_info(const RuntimeEnv& env) {
    return {{"ffmpeg", env.ffmpeg},
            {"ffprobe", env.ffprobe},
            {"transcribe_default", env.transcribe_model},
            {"tts_engines", env.tts_engines},
            {"data_dir", env.data_dir},
            {"gpu", env.gpu},
            {"ai_provider", env.ai_provider}};
}

Json project_summary(const Project& project) {
    const Timeline* tl = project.timeline ? &*project.timeline : nullptr;
    const std::int64_t w = tl != nullptr ? tl->width : 720;
    const std::int64_t h = tl != nullptr ? tl->height : 1280;
    const Json tld = timeline_dto(tl);
    return {{"project_id", project.id},
            {"name", project.name},
            {"aspect", aspect_ratio(w, h)},
            {"duration", tld["duration"]},
            {"clips", tld["clip_count"]},
            {"created_at", project.created_at},
            {"folder", project.folder ? Json(*project.folder) : Json(nullptr)}};
}

} // namespace gc::core::mcp
