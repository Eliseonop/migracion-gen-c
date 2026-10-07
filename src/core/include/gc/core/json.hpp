#pragma once

#include <nlohmann/json.hpp>

namespace gc {

// JSON con orden de inserción: las claves se leen y se escriben en el mismo orden,
// igual que Python (dict + json.dumps). Así el JSON que escribe GenClipPro se
// puede comparar línea a línea con el de video-yt.
using Json = nlohmann::ordered_json;

} // namespace gc
