#pragma once

// La auditoría de lo que hace la IA por el servidor MCP (espejo de
// backend/app/mcp_server/audit.py de video-yt): un registro en JSONL que solo crece,
// <data>/mcp_audit.jsonl, con qué tool se llamó, su nivel de acceso, sobre qué proyecto y un
// `meta` con valores SEGUROS (modelo, clip_id, archivo…), nunca textos largos (guion, TTS,
// segmentos). Y, en memoria, las tools y los jobs en curso para el panel del editor.
//
// El mismo archivo que escribe video-yt: los dos programas leen lo que deja el otro.

#include <cstdint>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "gc/core/error.hpp"
#include "gc/core/ids.hpp"
#include "gc/core/json.hpp"

namespace gc::core::mcp {

// audit.extract_meta: de `params`, solo las claves seguras (_SAFE_META_KEYS) con un valor que
// no es None ni «»: los bool y los enteros tal cual, los reales con round(x, 4) y los textos
// cortados a 80 caracteres (lo demás, fuera). En las tools de Whisper, `model` pasa a ser el
// modelo resuelto (el pedido si es válido; si no, `saved_model`, el de los ajustes) y, si no se
// pidió ninguno, `model_source` dice «ajustes».
[[nodiscard]] Json extract_meta(std::string_view tool, const Json& params, std::string_view saved_model);

// El texto de str.splitlines() de Python sobre UTF-8: corta en \n, \r, \r\n, \v, \f, \x1c, \x1d,
// \x1e, U+0085, U+2028 y U+2029, sin los saltos.
[[nodiscard]] std::vector<std::string> py_splitlines(std::string_view s);
// El error de bytes.decode("utf-8") de Python, o nada si el texto es UTF-8 válido («'utf-8'
// codec can't decode byte 0xff in position 7: invalid start byte»).
[[nodiscard]] std::optional<std::string> utf8_decode_error(std::string_view bytes);

// El estado de un job para `active` (lo que da jobs.get_job): su estado como texto, su progreso y su mensaje.
struct AuditJob {
    std::string status;
    double progress = 0.0;
    Json message = "En cola…";
};

struct AuditDeps {
    // El reloj: µs desde 1970 en UTC (datetime.now(timezone.utc)). Sin él, el del sistema.
    std::function<std::int64_t()> now_us;
    // Los tokens de las tools en curso (uuid4().hex[:12]). Sin él, aleatorios.
    IdSource* ids = nullptr;
    // jobs.get_job: nada si el job no existe.
    std::function<std::optional<AuditJob>(std::string_view job_id)> job;
};

struct FinishArgs {
    std::string status = "ok";
    std::optional<double> ms;
    std::optional<std::string> error;
    std::optional<std::vector<std::string>> param_keys;
    Json extra_meta = nullptr;
};

struct LogArgs {
    Json project_id = nullptr; // texto o None
    std::optional<std::vector<std::string>> param_keys;
    std::string status = "ok";
    std::optional<double> ms;
    std::optional<std::string> error;
    Json meta = nullptr;
};

class Audit {
public:
    // El registro en <data_dir>/mcp_audit.jsonl (data_dir es config.DATA_DIR).
    explicit Audit(std::filesystem::path data_dir, AuditDeps deps = {});

    [[nodiscard]] std::filesystem::path path() const { return data_dir_ / "mcp_audit.jsonl"; }

    // El origen de las acciones auditadas (el contextvar de `audit.source`, p. ej. «ai_chat»): por
    // hilo, como el contexto que copia Python al hilo de cada tool. nullopt o «»: sin origen.
    static void set_source(std::optional<std::string> source);
    [[nodiscard]] static std::optional<std::string> source();

    // begin: marca una tool en curso (solo en memoria) y da el token para finish.
    [[nodiscard]] std::string begin(std::string_view tool, std::string_view access, const Json& project_id,
                                    const Json& meta);
    // finish: cierra la tool en curso y escribe su entrada (sin token conocido, la de «unknown»).
    [[nodiscard]] Result<Json> finish(std::string_view token, const FinishArgs& args = {});
    // track_job: sigue un job lanzado por una tool hasta que termina (sin id, nada).
    void track_job(std::string_view job_id, std::string_view tool, std::string_view access, const Json& project_id,
                   const Json& meta, const std::optional<std::string>& source_name = std::nullopt);
    // active: las tools y los jobs aún en curso, del proyecto (con los de ningún proyecto) o de
    // todos; los jobs que ya acabaron dejan de seguirse.
    [[nodiscard]] Json active(const Json& project_id);
    void reset_runtime();

    // log: añade una entrada al registro y la devuelve.
    [[nodiscard]] Result<Json> log(std::string_view tool, std::string_view access, const LogArgs& args = {});
    // read_all: todas las entradas (sin archivo, ninguna). Error con el de Python si no es UTF-8 o
    // si una línea no es JSON.
    [[nodiscard]] Result<Json> read_all() const;
    // read_recent: las últimas int(limit) (de 1 a 200; si no es un número, 80), del proyecto y
    // las que no tienen proyecto si se pide uno.
    [[nodiscard]] Result<Json> read_recent(const Json& limit = 80, const Json& project_id = nullptr) const;

private:
    [[nodiscard]] std::string now_iso() const;

    std::filesystem::path data_dir_;
    AuditDeps deps_;
    mutable std::mutex mutex_;      // live_ y jobs_
    mutable std::mutex file_mutex_; // el archivo
    Json live_ = Json::object();    // token → entrada, en orden
    Json jobs_ = Json::object();    // job_id → entrada, en orden
};

} // namespace gc::core::mcp
