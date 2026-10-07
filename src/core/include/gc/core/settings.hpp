#pragma once

// Ajustes de la app en data/settings.json (espejo de settings.py, export_settings.py y
// transcribe_settings.py de video-yt): narrador, favoritos, historial de YouTube, API
// keys, export, transcripción, «Eliminar fondo» y modelos de IA.
//
// Las API keys solo salen por api_key() y keys_for(), para usarlas. public_view() es lo
// que ve la interfaz: qué proveedores tienen clave y cuántas, nunca sus valores.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "gc/core/error.hpp"
#include "gc/core/json.hpp"

namespace gc::core {

// {fps, quality}: fps de 24/25/30/50/60 y calidad draft/standard/high (si no, 30 y standard).
[[nodiscard]] Result<Json> normalize_export_settings(const Json& raw);
// {model}: tiny/base/small/medium/large-v3 (si no, base).
[[nodiscard]] Json normalize_transcribe_settings(const Json& raw);

class SettingsFile {
public:
    explicit SettingsFile(std::filesystem::path file) : file_(std::move(file)) {}

    // Los ajustes con sus valores por defecto. Sin archivo, o con uno ilegible, los de
    // fábrica: nunca falla.
    [[nodiscard]] Json load() const;
    // Fusiona `patch` con lo guardado y lo escribe. Las API keys se fusionan aparte: una
    // máscara («********», true…) no cambia la clave, y un texto vacío la borra.
    [[nodiscard]] Result<Json> save(const Json& patch);
    // Para la interfaz: sin secretos (api_keys = {proveedor: true}) y con el número de
    // claves de cada proveedor.
    [[nodiscard]] Result<Json> public_view() const;

    // La clave principal de un proveedor (Pexels, GIPHY, Gemini…); vacía si no hay.
    [[nodiscard]] std::string api_key(std::string_view provider) const;
    // Varias claves por proveedor, para turnarlas: la 0 es la principal (api_keys) y las
    // demás van en api_keys_extra.
    [[nodiscard]] Result<std::vector<std::string>> keys_for(std::string_view provider) const;
    [[nodiscard]] Result<std::size_t> add_key(std::string_view provider, std::string_view value);
    [[nodiscard]] Result<std::size_t> set_key(std::string_view provider, std::int64_t index, std::string_view value);
    [[nodiscard]] Result<std::size_t> remove_key(std::string_view provider, std::int64_t index);

private:
    [[nodiscard]] Json load_unlocked() const;
    [[nodiscard]] Json read_raw() const;

    std::filesystem::path file_;
    mutable std::mutex mutex_;
};

} // namespace gc::core
