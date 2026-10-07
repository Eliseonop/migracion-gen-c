#pragma once

// Catálogo de efectos de vídeo animados y animaciones de clip
// (espejo de backend/app/fx_catalog.py).
//
// Es un RECURSO de la app (resources/fx/catalog.json): ids, versión, parámetros
// con su tipo/rango/defecto y las ranuras de las animaciones. De momento lo
// genera el frontend de video-yt (`npm run fx:build`) y
// tools/fixtures/export_fixtures.py lo copia aquí; cuando los efectos se porten
// al render nativo (F4), pasará a definirse en GenClipPro.
//
// La app carga el catálogo una vez al arrancar (set_fx_catalog); sin catálogo,
// ningún efecto existe (como en video-yt si falta el archivo).

#include <filesystem>
#include <memory>
#include <string_view>

#include "gc/core/error.hpp"
#include "gc/core/json.hpp"

namespace gc::core {

class FxCatalog {
public:
    FxCatalog() = default;
    explicit FxCatalog(Json data);

    [[nodiscard]] static Result<FxCatalog> load(const std::filesystem::path& path);

    // Manifiesto del efecto / la animación, o nullptr si no existe.
    [[nodiscard]] const Json* effect(const Json& id) const;
    [[nodiscard]] const Json* anim(const Json& id) const;
    [[nodiscard]] const Json& data() const noexcept { return data_; }

    // Parámetros completos y saneados (paramValue de fx/core/params.js): lo que
    // falta o no vale toma el defecto; los rangos se acotan.
    [[nodiscard]] static Json resolve_params(const Json& manifest, const Json& raw);

private:
    Json data_ = Json::object();
};

// Catálogo activo de la app (vacío hasta que se fija).
void set_fx_catalog(std::shared_ptr<const FxCatalog> catalog);
[[nodiscard]] std::shared_ptr<const FxCatalog> fx_catalog();

} // namespace gc::core
