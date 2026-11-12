#pragma once

// La pestaña YouTube del material (EdMaterial.jsx y ytQuality.js): las calidades del vídeo analizado para
// «Agregar vídeo completo», sus textos, y el historial de enlaces tal como lo pinta y lo edita el editor.
// Y «Subir calidad» («Usar esta calidad en la timeline» del menú del material): cuándo se ofrece, el texto
// de la pregunta y los avisos de checkUpgrade y runUpgrade.

#include <string>
#include <string_view>

#include "gc/core/json.hpp"

namespace gc::core {

// videoQualities: las de /api/analyze (video.qualities) con calidad > 0, de mejor a peor.
[[nodiscard]] Json yt_video_qualities(const Json& result);
// qualityShort: «8K», «4K» o «1080p»; qualityTag: «8K», «4K», «2K», «Full HD», «HD» o «».
[[nodiscard]] std::string yt_quality_short(const Json& quality);
[[nodiscard]] std::string yt_quality_tag(const Json& quality);
// fmtBytes: «1.3 GB» o «12 MB» («» si no hay tamaño); qualityMeta: «60 fps · ≈ 1.3 GB» con lo que se sepa.
[[nodiscard]] std::string yt_fmt_bytes(const Json& bytes);
[[nodiscard]] std::string yt_quality_meta(const Json& item);

// normalizeHistory: las entradas con un enlace. removeHistory: la lista sin `item` (por id del vídeo o enlace).
[[nodiscard]] Json yt_normalize_history(const Json& raw);
[[nodiscard]] Json yt_history_without(const Json& history, const Json& item);

// isYtUrl de EdMaterial.jsx: el host de `new URL(s)` (con «https://» delante si no trae esquema), sin «www.»,
// es youtube.com, youtu.be o un subdominio de youtube.com; si `new URL` lanza, que el texto contenga
// «youtu.be» o «youtube.com». El host sale de un analizador de URL como el de WHATWG para lo que importa
// aquí (esquemas especiales, «\», usuario, puerto, %XX, IPv4 e IPv6 y los caracteres prohibidos); un host
// no ASCII no pasa por UTS 46 (solo se baja a minúsculas lo ASCII).
[[nodiscard]] bool yt_is_url(std::string_view u);
// canUpgradeFrom: el menú de un vídeo del proyecto (no de la biblioteca ni un segmento) que viene de YouTube.
[[nodiscard]] bool yt_can_upgrade_from(const Json& item);
// materialIdent('clips', item): String(item.index ?? item.id).
[[nodiscard]] std::string yt_material_ident(const Json& item);
// usedUpgrades: las versiones compatibles (compatible !== false) que la timeline usa.
[[nodiscard]] Json yt_used_upgrades(const Json& list);
// upgradeMessage: la pregunta del modal «Subir calidad en la timeline».
[[nodiscard]] std::string yt_upgrade_message(const Json& versions, const Json& new_quality);
// checkUpgrade con la respuesta de GET …/versions: {ask: {index, quality, versions} | null,
// toast: {type, message} | null}.
[[nodiscard]] Json yt_check_upgrade(const Json& item, const Json& info);
// runUpgrade: los old_idents que se mandan (String(v.index) de usedUpgrades) y el aviso con la respuesta
// de …/replace-versions ({replaced, warnings}).
[[nodiscard]] Json yt_upgrade_olds(const Json& ask);
[[nodiscard]] Json yt_upgrade_done_toast(const Json& ask, const Json& res);

} // namespace gc::core
