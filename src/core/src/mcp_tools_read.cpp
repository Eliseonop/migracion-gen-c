#include "gc/core/mcp_tools_read.hpp"

#include <cmath>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

#include "gc/core/history_store.hpp"
#include "gc/core/io.hpp"
#include "gc/core/mcp_dto.hpp"
#include "gc/core/mcp_registry.hpp"

#include "json_reader.hpp"
#include "py.hpp"

namespace gc::core::mcp {

namespace {

using detail::py_len;
using detail::py_lower;
using detail::py_strip;
using detail::py_truthy;

// Un ValueError de Python: llega al agente, con el código que le daría _classify_value_error.
std::unexpected<Error> value_error(std::string message) {
    const bool not_found = classify_value_error(message).first == "resource_not_found";
    return fail(not_found ? ErrorCode::NotFound : ErrorCode::InvalidArgument, std::move(message));
}

// Un crash de Python (TypeError, AttributeError…): el servidor lo oculta al agente.
std::unexpected<Error> crash(const std::string& type, const std::string& message) {
    return fail(ErrorCode::Internal, type + ": " + message);
}

// type(x).__name__ de Python para un valor JSON.
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

// (x or "") de Python para un argumento de texto: None y el texto vacío son "".
std::string text_or_empty(const Json& v) {
    return v.is_string() ? v.get<std::string>() : std::string();
}

Result<Project> project_or_raise(const ProjectsStore& store, std::string_view project_id) {
    auto proj = store.get_project(project_id);
    if (!proj) {
        return std::unexpected(proj.error());
    }
    if (!*proj) {
        return value_error("Proyecto no encontrado: " + std::string(project_id));
    }
    return std::move(**proj);
}

// --- _norm de timeline_history.py --------------------------------------------------------------

// len(list(v)) de Python para v = h.get(clave) or []: un texto da sus caracteres, un dict sus claves.
Result<std::size_t> list_len(const Json& v) {
    if (!py_truthy(v)) {
        return std::size_t{0};
    }
    if (v.is_array() || v.is_object()) {
        return v.size();
    }
    if (v.is_string()) {
        return py_len(v.get_ref<const std::string&>());
    }
    return crash("TypeError", std::string("'") + py_type_name(v) + "' object is not iterable");
}

// ¿Dos claves de dict de Python son la misma? Por valor (1 == 1.0 == True), y el NaN de json.loads
// es siempre el mismo objeto, así que todas sus claves NaN son una.
bool same_key(const Json& a, const Json& b) {
    if (a.is_number_float() && b.is_number_float() && std::isnan(a.get<double>()) && std::isnan(b.get<double>())) {
        return true;
    }
    return detail::py_equal(a, b);
}

// list(dict(v).keys()) de Python para v = h.get("checkpoints") or {}.
Result<Json> dict_keys(const Json& v) {
    Json keys = Json::array();
    if (!py_truthy(v)) {
        return keys;
    }
    if (v.is_object()) {
        for (const auto& [key, _] : v.items()) {
            keys.push_back(key);
        }
        return keys;
    }
    if (!v.is_array() && !v.is_string()) {
        return crash("TypeError", std::string("'") + py_type_name(v) + "' object is not iterable");
    }
    // dict(secuencia): cada elemento, un par (clave, valor).
    const std::vector<Json> items = *detail::py_iter(v);
    for (std::size_t i = 0; i < items.size(); ++i) {
        const auto pair = detail::py_iter(items[i]);
        if (!pair) {
            return crash("TypeError", "object is not iterable");
        }
        if (pair->size() != 2) {
            return value_error("dictionary update sequence element #" + std::to_string(i) + " has length " +
                               std::to_string(pair->size()) + "; 2 is required");
        }
        const Json& key = pair->front();
        if (key.is_array() || key.is_object()) {
            return crash("TypeError", std::string("cannot use '") + py_type_name(key) +
                                          "' as a dict key (unhashable type: '" + py_type_name(key) + "')");
        }
        bool seen = false;
        for (const Json& k : keys) {
            if (same_key(k, key)) {
                seen = true; // la clave se queda donde estaba (solo cambia el valor)
                break;
            }
        }
        if (!seen) {
            keys.push_back(key);
        }
    }
    return keys;
}

// d.get(clave) de Python.
Json get_or_null(const Json& h, const char* key) {
    const auto it = h.find(key);
    return it != h.end() ? *it : Json(nullptr);
}

} // namespace

Result<Json> describe_capabilities(const Json& domain, const RuntimeEnv& env) {
    if (py_truthy(domain)) {
        if (!domain.is_string()) {
            return crash("AttributeError",
                         std::string("'") + py_type_name(domain) + "' object has no attribute 'strip'");
        }
        return describe(domain.get_ref<const std::string&>(), env);
    }
    return overview(env);
}

Result<Json> list_projects(const ProjectsStore& store) {
    auto projs = store.list_projects();
    if (!projs) {
        return std::unexpected(projs.error());
    }
    Json list = Json::array();
    for (const Project& p : *projs) {
        list.push_back(project_summary(p));
    }
    return Json{{"count", projs->size()}, {"projects", std::move(list)}};
}

Result<Json> resolve_project(const ProjectsStore& store, const Json& query) {
    const std::string raw = text_or_empty(query);
    const std::string q = py_strip(raw);
    if (q.empty()) {
        return value_error("Falta 'query' (id o nombre del proyecto).");
    }
    const std::string ql = py_lower(q);
    auto projs = store.list_projects();
    if (!projs) {
        return std::unexpected(projs.error());
    }

    // 1) id exacto.
    for (const Project& p : *projs) {
        if (p.id == q) {
            return Json{{"project_id", p.id}, {"name", p.name}, {"match", "id"}};
        }
    }

    // 2) nombre exacto → 3) prefijo de id → 4) nombre contiene.
    const auto name_exact = [&](const Project& p) { return py_lower(py_strip(p.name)) == ql; };
    const auto id_prefix = [&](const Project& p) { return py_lower(p.id).starts_with(ql); };
    const auto name_contains = [&](const Project& p) { return py_lower(p.name).contains(ql); };
    const std::pair<const char*, std::function<bool(const Project&)>> groups[] = {
        {"name", name_exact}, {"id_prefix", id_prefix}, {"name", name_contains}};
    for (const auto& [label, matches] : groups) {
        std::vector<const Project*> uniq;
        std::unordered_set<std::string> seen;
        for (const Project& p : *projs) {
            if (matches(p) && seen.insert(p.id).second) {
                uniq.push_back(&p);
            }
        }
        if (uniq.size() == 1) {
            return Json{{"project_id", uniq.front()->id}, {"name", uniq.front()->name}, {"match", label}};
        }
        if (uniq.size() > 1) {
            Json candidates = Json::array();
            for (std::size_t i = 0; i < uniq.size() && i < 10; ++i) {
                candidates.push_back(project_summary(*uniq[i]));
            }
            return Json{{"ambiguous", true},
                        {"candidates", std::move(candidates)},
                        {"note", "Varios proyectos coinciden; elige un project_id concreto."}};
        }
    }
    return value_error("Ningún proyecto coincide con: " + raw);
}

Result<Json> history_summary(const Json& raw_history) {
    // h = history or {}; después h.get(…): lo que no es un dict falla ahí.
    const Json h = py_truthy(raw_history) ? raw_history : Json::object();
    if (!h.is_object()) {
        return crash("AttributeError", std::string("'") + py_type_name(h) + "' object has no attribute 'get'");
    }
    auto undo = list_len(get_or_null(h, "undo"));
    if (!undo) {
        return std::unexpected(undo.error());
    }
    auto redo = list_len(get_or_null(h, "redo"));
    if (!redo) {
        return std::unexpected(redo.error());
    }
    auto checkpoints = dict_keys(get_or_null(h, "checkpoints"));
    if (!checkpoints) {
        return std::unexpected(checkpoints.error());
    }
    return Json{{"can_undo", *undo > 0}, {"can_redo", *redo > 0}, {"checkpoints", std::move(*checkpoints)}};
}

Result<Json> history_summary_file(const std::filesystem::path& file) {
    std::error_code ec;
    if (!std::filesystem::exists(file, ec)) {
        return history_summary(nullptr);
    }
    auto raw = read_json_file_py(file);
    return history_summary(raw ? *raw : Json(nullptr)); // ilegible: el historial vacío
}

Result<Json> get_project_context(const ProjectsStore& store, const std::filesystem::path& data_dir,
                                 std::string_view project_id) {
    auto proj = project_or_raise(store, project_id);
    if (!proj) {
        return std::unexpected(proj.error());
    }
    auto history = history_summary_file(history_path(data_dir, project_id));
    if (!history) {
        return std::unexpected(history.error());
    }
    return project_context(*proj, *history);
}

Result<Json> get_timeline(const ProjectsStore& store, std::string_view project_id) {
    auto proj = project_or_raise(store, project_id);
    if (!proj) {
        return std::unexpected(proj.error());
    }
    return timeline_detail(proj->timeline ? &*proj->timeline : nullptr);
}

Result<Json> inspect_clip(const ProjectsStore& store, std::string_view project_id, std::string_view clip_id) {
    auto proj = project_or_raise(store, project_id);
    if (!proj) {
        return std::unexpected(proj.error());
    }
    if (proj->timeline) {
        for (const auto& c : proj->timeline->clips) {
            if (c->id == clip_id) {
                return clip_detail(*c);
            }
        }
    }
    return value_error("Clip no encontrado en la timeline: " + std::string(clip_id));
}

Result<Json> list_media(const ProjectsStore& store, std::string_view project_id) {
    auto proj = project_or_raise(store, project_id);
    if (!proj) {
        return std::unexpected(proj.error());
    }
    return mcp::media_list(*proj);
}

Result<Json> search_transcript(const ProjectsStore& store, std::string_view project_id, const Json& query,
                               const Json& limit) {
    auto proj = project_or_raise(store, project_id);
    if (!proj) {
        return std::unexpected(proj.error());
    }
    const std::string q = py_lower(py_strip(text_or_empty(query)));
    if (q.empty()) {
        return value_error("Falta el texto a buscar (query).");
    }
    Json matches = Json::array();
    const auto hit = [&](const TranscriptSegment& seg) { return py_lower(seg.text).contains(q); };
    for (const Transcript& tr : proj->transcripts) {
        for (const TranscriptSegment& seg : tr.segments) {
            if (hit(seg)) {
                matches.push_back({{"scope", "project"},
                                   {"transcript_id", tr.id},
                                   {"start", seg.start},
                                   {"end", seg.end},
                                   {"text", seg.text}});
            }
        }
    }
    for (const ClipInfo& c : proj->clips) {
        if (!c.transcript) {
            continue;
        }
        for (const TranscriptSegment& seg : c.transcript->segments) {
            if (hit(seg)) {
                matches.push_back({{"scope", "clip"},
                                   {"clip_index", c.index},
                                   {"start", seg.start},
                                   {"end", seg.end},
                                   {"text", seg.text}});
            }
        }
    }

    // max(1, int(limit)): int() de Python, con sus errores.
    const auto n = detail::py_int(limit);
    if (!n) {
        if (limit.is_string()) {
            return value_error("invalid literal for int() with base 10: " + detail::py_repr(limit));
        }
        if (limit.is_number_float() && std::isnan(limit.get<double>())) {
            return value_error("cannot convert float NaN to integer");
        }
        if (limit.is_number_float()) {
            return crash("OverflowError", "cannot convert float infinity to integer");
        }
        return crash("TypeError", std::string("int() argument must be a string, a bytes-like object or a real "
                                              "number, not '") +
                                      py_type_name(limit) + "'");
    }
    const std::size_t keep = *n < 1 ? 1 : static_cast<std::size_t>(*n);
    const std::size_t count = matches.size();
    if (matches.size() > keep) {
        matches.erase(matches.begin() + static_cast<std::ptrdiff_t>(keep), matches.end());
    }
    return Json{{"query", query}, {"count", count}, {"matches", std::move(matches)}};
}

} // namespace gc::core::mcp
