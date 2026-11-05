#include "gc/export/audio.hpp"

#include <map>
#include <string>
#include <utility>

#include "gc/core/audio_export.hpp"
#include "gc/core/export_input.hpp"
#include "gc/core/io.hpp"
#include "gc/core/timeline_rules.hpp"
#include "gc/media/audio.hpp"
#include "gc/media/probe.hpp"
#include "prepare.hpp"

namespace gc::exporter {

namespace detail {

Result<std::vector<float>> mix_audio(const core::Timeline& full, const std::map<std::string, std::filesystem::path>& paths,
                                     int fps, double total) {
    using core::TimelineClip;
    // Si el material de cada clip tiene audio (_has_audio).
    std::map<std::string, bool> audio_of;
    const auto exists = [&](const TimelineClip& c) { return paths.contains(c.id); };
    const auto has_audio = [&](const TimelineClip& c) {
        auto [it, fresh] = audio_of.try_emplace(c.id, false);
        if (fresh && paths.contains(c.id)) {
            it->second = media::probe_media(paths.at(c.id)).has_audio;
        }
        return it->second;
    };
    auto plan = core::export_inputs(full, exists, has_audio);
    auto graph = core::export_audio_graph(full, exists, has_audio);
    if (!plan || !graph) {
        return std::unexpected(plan ? graph.error() : plan.error());
    }
    if (graph->empty()) {
        return std::vector<float>{};
    }
    std::vector<std::vector<std::string>> inputs;
    for (const TimelineClip* c : plan->files) {
        inputs.push_back(core::ffmpeg_input_args(*c, core::path_to_utf8(paths.at(c->id)), fps));
    }
    return media::mix_audio(inputs, *graph, total);
}

} // namespace detail

Result<std::vector<float>> mix_timeline_audio(const LoadedProject& project) {
    const core::Timeline& full = project.timeline;
    const int fps = full.fps > 0 ? static_cast<int>(full.fps) : 30;
    const double total = core::export_total_duration(full);
    if (total <= 0) {
        return fail(ErrorCode::InvalidArgument, "la timeline está vacía: añade al menos un clip");
    }
    return detail::mix_audio(full, detail::material_paths(project, full), fps, total);
}

} // namespace gc::exporter
