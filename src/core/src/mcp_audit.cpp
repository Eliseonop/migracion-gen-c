#include "gc/core/mcp_audit.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <format>
#include <fstream>
#include <set>
#include <utility>

#include "gc/core/io.hpp"
#include "gc/core/settings.hpp"

#include "json_reader.hpp"
#include "py.hpp"

namespace gc::core::mcp {

namespace {

using detail::py_truthy;

constexpr std::array<std::string_view, 3> kJobDone{"done", "error", "cancelled"};
constexpr std::array<std::string_view, 4> kWhisperTools{"transcribe", "generate_subtitles",
                                                        "create_short_from_youtube", "make_short_from_library"};
// Valores cortos e identificadores: nada de textos, segmentos, URLs ni keyframes.
constexpr std::array<std::string_view, 33> kSafeMetaKeys{
    "model",      "language",    "clip_id", "source_clip_id", "clip_index", "track_id", "filename",
    "asset_kind", "asset_scope", "engine",  "voice",          "voice2",     "job_id",   "aspect",
    "fps",        "width",       "height",  "kind",           "speed",      "opacity",  "volume",
    "muted",      "query",       "category", "count",         "crop_mode",  "asset_id", "blend",
    "pause",      "name",        "position", "start",         "at_time"};
constexpr std::size_t kMetaStrMax = 80;
constexpr std::size_t kErrorMax = 300;

#ifdef _WIN32
constexpr std::string_view kNewline = "\r\n"; // el modo texto de Python en Windows
#else
constexpr std::string_view kNewline = "\n";
#endif

// El contextvar de audit.source: uno por hilo.
thread_local std::optional<std::string> t_source; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

std::optional<std::string> current_source() {
    return t_source && !t_source->empty() ? t_source : std::nullopt;
}

bool safe_key(std::string_view key) {
    return std::ranges::find(kSafeMetaKeys, key) != kSafeMetaKeys.end();
}

// El nombre de tipo de Python de un valor JSON, para «'list' object has no attribute 'get'».
const char* py_type_name(const Json& v) {
    switch (v.type()) {
    case Json::value_t::null: return "NoneType";
    case Json::value_t::boolean: return "bool";
    case Json::value_t::number_float: return "float";
    case Json::value_t::number_integer:
    case Json::value_t::number_unsigned: return "int";
    case Json::value_t::string: return "str";
    case Json::value_t::array: return "list";
    case Json::value_t::object: return "dict";
    default: return "object";
    }
}

// `x == ""` de Python: solo el texto vacío.
bool is_empty_str(const Json& v) {
    return v.is_string() && v.get_ref<const std::string&>().empty();
}

const Json& get(const Json& obj, std::string_view key) {
    static const Json null_value;
    if (!obj.is_object()) {
        return null_value;
    }
    const auto it = obj.find(key);
    return it == obj.end() ? null_value : *it;
}

// _matches_project: sin proyecto pedido, todas; si no, las de ese proyecto y las de ninguno.
bool matches_project(const Json& entry, const Json& project_id) {
    if (!py_truthy(project_id)) {
        return true;
    }
    const Json& pid = get(entry, "project_id");
    return pid.is_null() || is_empty_str(pid) || detail::py_equal(pid, project_id);
}

std::vector<std::string> sorted_keys(const std::optional<std::vector<std::string>>& keys) {
    std::vector<std::string> out = keys.value_or(std::vector<std::string>{});
    std::ranges::sort(out); // por bytes de UTF-8: el orden de los puntos de código de Python
    return out;
}

// Un trozo de UTF-8 bien formado que empieza en `i`: su longitud, o el error de CPython.
struct Utf8Step {
    std::size_t length = 0;      // bytes del carácter (0 si hay error)
    std::size_t bad = 0;         // bytes que abarca el error
    std::string_view reason;
};

Utf8Step utf8_step(std::string_view s, std::size_t i) {
    const auto at = [&](std::size_t k) { return static_cast<unsigned char>(s[k]); };
    const unsigned char b = at(i);
    if (b < 0x80) {
        return {1, 0, {}};
    }
    std::size_t need = 0;
    unsigned char lo = 0x80;
    unsigned char hi = 0xBF;
    if (b >= 0xC2 && b <= 0xDF) {
        need = 1;
    } else if (b >= 0xE0 && b <= 0xEF) {
        need = 2;
        lo = b == 0xE0 ? 0xA0 : 0x80;
        hi = b == 0xED ? 0x9F : 0xBF;
    } else if (b >= 0xF0 && b <= 0xF4) {
        need = 3;
        lo = b == 0xF0 ? 0x90 : 0x80;
        hi = b == 0xF4 ? 0x8F : 0xBF;
    } else {
        return {0, 1, "invalid start byte"};
    }
    for (std::size_t k = 1; k <= need; ++k) {
        if (i + k >= s.size()) {
            return {0, s.size() - i, "unexpected end of data"};
        }
        const unsigned char c = at(i + k);
        const bool ok = k == 1 ? (c >= lo && c <= hi) : (c >= 0x80 && c <= 0xBF);
        if (!ok) {
            return {0, k, "invalid continuation byte"};
        }
    }
    return {need + 1, 0, {}};
}

Result<std::string> read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    const std::streamoff size = in ? static_cast<std::streamoff>(in.tellg()) : -1;
    std::string text(size > 0 ? static_cast<std::size_t>(size) : 0U, '\0');
    in.seekg(0);
    if (size < 0 || !in.read(text.data(), size)) {
        return fail(ErrorCode::Io, "no se puede leer " + path_to_utf8(path));
    }
    return text;
}

} // namespace

Json extract_meta(std::string_view tool, const Json& params, std::string_view saved_model) {
    Json meta = Json::object();
    if (params.is_object()) {
        for (const auto& [key, raw] : params.items()) {
            if (!safe_key(key) || raw.is_null() || is_empty_str(raw)) {
                continue;
            }
            if (raw.is_boolean() || raw.is_number_integer() || raw.is_number_unsigned()) {
                meta[key] = raw;
            } else if (raw.is_number_float()) {
                meta[key] = detail::py_round(raw.get<double>(), 4);
            } else if (raw.is_string()) {
                meta[key] = detail::py_slice(raw.get_ref<const std::string&>(), kMetaStrMax);
            }
        }
    }
    if (std::ranges::find(kWhisperTools, tool) != kWhisperTools.end()) {
        const Json given = get(meta, "model");
        std::string resolved = resolve_transcribe_model(given.is_string() ? given : Json(nullptr), saved_model);
        if (!py_truthy(given)) {
            meta["model_source"] = "ajustes";
        }
        meta["model"] = std::move(resolved);
    }
    return meta;
}

std::vector<std::string> py_splitlines(std::string_view s) {
    std::vector<std::string> out;
    std::size_t start = 0;
    std::size_t i = 0;
    while (i < s.size()) {
        const auto c = static_cast<unsigned char>(s[i]);
        std::size_t width = 0; // bytes del salto (0: no lo es)
        if (c == '\r') {
            width = i + 1 < s.size() && s[i + 1] == '\n' ? 2 : 1;
        } else if (c == '\n' || c == 0x0B || c == 0x0C || c == 0x1C || c == 0x1D || c == 0x1E) {
            width = 1;
        } else if (c == 0xC2 && i + 1 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0x85) {
            width = 2; // U+0085
        } else if (c == 0xE2 && i + 2 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0x80 &&
                   (static_cast<unsigned char>(s[i + 2]) == 0xA8 || static_cast<unsigned char>(s[i + 2]) == 0xA9)) {
            width = 3; // U+2028 y U+2029
        }
        if (width == 0) {
            ++i;
            continue;
        }
        out.emplace_back(s.substr(start, i - start));
        i += width;
        start = i;
    }
    if (start < s.size()) {
        out.emplace_back(s.substr(start));
    }
    return out;
}

std::optional<std::string> utf8_decode_error(std::string_view bytes) {
    std::size_t i = 0;
    while (i < bytes.size()) {
        const Utf8Step step = utf8_step(bytes, i);
        if (step.length == 0) {
            if (step.bad == 1) {
                return std::format("'utf-8' codec can't decode byte 0x{:02x} in position {}: {}",
                                   static_cast<unsigned char>(bytes[i]), i, step.reason);
            }
            return std::format("'utf-8' codec can't decode bytes in position {}-{}: {}", i, i + step.bad - 1,
                               step.reason);
        }
        i += step.length;
    }
    return std::nullopt;
}

Audit::Audit(std::filesystem::path data_dir, AuditDeps deps) : data_dir_(std::move(data_dir)), deps_(std::move(deps)) {}

void Audit::set_source(std::optional<std::string> source) {
    t_source = std::move(source);
}

std::optional<std::string> Audit::source() {
    return current_source();
}

std::string Audit::now_iso() const {
    using namespace std::chrono;
    const std::int64_t us = deps_.now_us ? deps_.now_us()
                                         : duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();
    const sys_time<microseconds> t{microseconds(us)};
    const auto day = floor<days>(t);
    const year_month_day ymd{day};
    const hh_mm_ss hms{floor<milliseconds>(t - day)};
    // isoformat(timespec="milliseconds"): los µs se truncan.
    return std::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}.{:03}+00:00", static_cast<int>(ymd.year()),
                       static_cast<unsigned>(ymd.month()), static_cast<unsigned>(ymd.day()), hms.hours().count(),
                       hms.minutes().count(), hms.seconds().count(), hms.subseconds().count());
}

std::string Audit::begin(std::string_view tool, std::string_view access, const Json& project_id, const Json& meta) {
    RandomIdSource random;
    IdSource& ids = deps_.ids != nullptr ? *deps_.ids : random;
    std::string token = ids.hex32().substr(0, 12);
    Json entry = {{"token", token},
                  {"ts", now_iso()},
                  {"tool", tool},
                  {"access", access},
                  {"project_id", project_id},
                  {"status", "run"},
                  {"meta", meta.is_object() ? meta : Json::object()}};
    if (const auto src = current_source()) {
        entry["source"] = *src;
    }
    const std::scoped_lock lock(mutex_);
    live_[token] = std::move(entry);
    return token;
}

Result<Json> Audit::finish(std::string_view token, const FinishArgs& args) {
    Json live = nullptr;
    {
        const std::scoped_lock lock(mutex_);
        if (const auto it = live_.find(token); it != live_.end()) {
            live = std::move(*it);
            live_.erase(it);
        }
    }
    const Json& tool = get(live, "tool");
    const Json& access = get(live, "access");
    LogArgs log_args;
    log_args.project_id = get(live, "project_id");
    log_args.param_keys = args.param_keys;
    log_args.status = args.status;
    log_args.ms = args.ms;
    log_args.error = args.error;
    Json meta = get(live, "meta").is_object() ? get(live, "meta") : Json::object();
    if (args.extra_meta.is_object()) {
        for (const auto& [key, value] : args.extra_meta.items()) {
            meta[key] = value;
        }
    }
    log_args.meta = meta.empty() ? Json(nullptr) : std::move(meta);
    return log(py_truthy(tool) && tool.is_string() ? tool.get<std::string>() : "unknown",
               py_truthy(access) && access.is_string() ? access.get<std::string>() : "read", log_args);
}

void Audit::track_job(std::string_view job_id, std::string_view tool, std::string_view access, const Json& project_id,
                      const Json& meta, const std::optional<std::string>& source_name) {
    if (job_id.empty()) {
        return;
    }
    Json entry = {{"job_id", job_id},
                  {"tool", tool},
                  {"access", access},
                  {"project_id", project_id},
                  {"status", "run"},
                  {"meta", meta.is_object() ? meta : Json::object()},
                  {"ts", now_iso()}};
    const auto src = source_name && !source_name->empty() ? source_name : current_source();
    if (src && !src->empty()) {
        entry["source"] = *src;
    }
    const std::scoped_lock lock(mutex_);
    jobs_[std::string(job_id)] = std::move(entry);
}

Json Audit::active(const Json& project_id) {
    Json tracked;
    Json live;
    {
        const std::scoped_lock lock(mutex_);
        tracked = jobs_;
        live = live_;
    }
    Json out = Json::array();
    std::set<std::string> tracked_ids;
    std::vector<std::string> dead;
    for (const auto& [jid, entry] : tracked.items()) {
        if (!matches_project(entry, project_id)) {
            continue;
        }
        const auto job = deps_.job ? deps_.job(jid) : std::nullopt;
        if (!job || std::ranges::find(kJobDone, job->status) != kJobDone.end()) {
            dead.push_back(jid);
            continue;
        }
        tracked_ids.insert(jid);
        Json item = entry;
        item["status"] = job->status.empty() ? std::string("run") : job->status;
        item["progress"] = job->progress == 0.0 ? Json(0) : Json(job->progress); // `progress or 0`
        item["message"] = job->message;
        out.push_back(std::move(item));
    }
    if (!dead.empty()) {
        const std::scoped_lock lock(mutex_);
        for (const std::string& jid : dead) {
            jobs_.erase(jid);
        }
    }
    for (const auto& [token, entry] : live.items()) {
        if (!matches_project(entry, project_id)) {
            continue;
        }
        const Json& jid = get(get(entry, "meta"), "job_id");
        const bool followed = jid.is_string() && tracked_ids.contains(jid.get<std::string>());
        if (py_truthy(jid) && followed) {
            continue; // el job ya sale con su progreso
        }
        Json item = entry;
        item["progress"] = nullptr;
        item["message"] = nullptr;
        out.push_back(std::move(item));
    }
    return out;
}

void Audit::reset_runtime() {
    const std::scoped_lock lock(mutex_);
    live_ = Json::object();
    jobs_ = Json::object();
}

Result<Json> Audit::log(std::string_view tool, std::string_view access, const LogArgs& args) {
    Json entry = {{"ts", now_iso()},
                  {"tool", tool},
                  {"access", access},
                  {"project_id", args.project_id},
                  {"param_keys", sorted_keys(args.param_keys)},
                  {"status", args.status}};
    if (args.ms) {
        entry["ms"] = detail::py_round(*args.ms, 1);
    }
    if (args.error && !args.error->empty()) {
        entry["error"] = detail::py_slice(*args.error, kErrorMax);
    }
    if (py_truthy(args.meta)) {
        entry["meta"] = args.meta;
    }
    if (const auto src = current_source()) {
        entry["source"] = *src;
    }
    auto line = py_json_dumps(entry);
    if (!line) {
        return std::unexpected(line.error());
    }
    const std::filesystem::path file = path();
    const std::scoped_lock lock(file_mutex_);
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    std::ofstream out(file, std::ios::binary | std::ios::app);
    out << *line << kNewline;
    if (!out.flush()) {
        return fail(ErrorCode::Io, "no se puede escribir " + path_to_utf8(file));
    }
    return entry;
}

Result<Json> Audit::read_all() const {
    const std::filesystem::path file = path();
    std::error_code ec;
    if (!std::filesystem::exists(file, ec)) {
        return Json::array();
    }
    std::string text;
    {
        const std::scoped_lock lock(file_mutex_);
        auto read = read_file(file);
        if (!read) {
            return std::unexpected(read.error());
        }
        text = std::move(*read);
    }
    if (auto bad = utf8_decode_error(text)) {
        return fail(ErrorCode::Parse, "UnicodeDecodeError: " + *bad);
    }
    Json out = Json::array();
    for (const std::string& raw : py_splitlines(text)) {
        const std::string line = detail::py_strip(raw);
        if (line.empty()) {
            continue;
        }
        auto row = py_json_loads(line);
        if (!row) {
            return fail(ErrorCode::Parse, "JSONDecodeError: " + row.error().message);
        }
        out.push_back(std::move(*row));
    }
    return out;
}

Result<Json> Audit::read_recent(const Json& limit, const Json& project_id) const {
    const std::int64_t n = std::clamp<std::int64_t>(detail::py_int(limit).value_or(80), 1, 200);
    auto rows = read_all();
    if (!rows) {
        return rows;
    }
    Json kept = Json::array();
    if (py_truthy(project_id)) {
        for (const Json& e : *rows) {
            if (!e.is_object()) {
                return fail(ErrorCode::Internal,
                            std::string("AttributeError: '") + py_type_name(e) + "' object has no attribute 'get'");
            }
            const Json& pid = get(e, "project_id");
            if (pid.is_null() || is_empty_str(pid) || detail::py_equal(pid, project_id)) {
                kept.push_back(e);
            }
        }
    } else {
        kept = std::move(*rows);
    }
    const auto size = static_cast<std::int64_t>(kept.size());
    Json out = Json::array();
    for (std::int64_t i = std::max<std::int64_t>(0, size - n); i < size; ++i) {
        out.push_back(kept[static_cast<std::size_t>(i)]);
    }
    return out;
}

} // namespace gc::core::mcp
