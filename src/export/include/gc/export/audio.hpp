#pragma once

// La mezcla de audio de una timeline, la misma que el export (render_project): el PCM que da el
// CLI con el grafo de compose.build_command, estéreo entrelazado en float a 48 kHz
// (media::kExportSampleRate) y de la duración del export. La usan el export y el reproductor del
// editor, que la hace sonar.

#include <vector>

#include "gc/core/error.hpp"
#include "gc/export/project.hpp"

namespace gc::exporter {

// Con la timeline de `project` (project.timeline). Vacío si no suena nada; error si la timeline
// está vacía o si falla el grafo.
[[nodiscard]] Result<std::vector<float>> mix_timeline_audio(const LoadedProject& project);

} // namespace gc::exporter
