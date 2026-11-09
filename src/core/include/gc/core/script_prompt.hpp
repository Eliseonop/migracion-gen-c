#pragma once

// «Prompt para el guion» del narrador (features/audio/scriptPrompt.js): el texto que se copia para pedirle a una
// IA el guion del vídeo, pensado para que un TTS en español lo pronuncie bien. Con clips, además, la lista del
// material y las reglas del montaje.

#include <string>
#include <string_view>

#include "gc/core/json.hpp"

namespace gc::core {

// buildScriptPrompt(topic, clips): `clips` es la lista del proyecto, como la da la API. De cada clip, su
// descripción, etiqueta o archivo (el primero que no esté vacío; texto, número o booleano) y su duración.
[[nodiscard]] std::string build_script_prompt(std::string_view topic, const Json& clips);

} // namespace gc::core
