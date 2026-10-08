#pragma once

// Utilidades compartidas por las operaciones de edición (edit_ops*.cpp).

#include <optional>
#include <string>
#include <string_view>

#include "gc/core/edit.hpp"
#include "json_reader.hpp"
#include "py.hpp"

namespace gc::core::detail {

inline double round3(double v) { return py_round(v, 3); }

inline std::unexpected<Error> invalid(std::string message) {
    return fail(ErrorCode::InvalidArgument, std::move(message));
}

inline Result<std::size_t> find_clip(const Timeline& tl, std::string_view id) {
    for (std::size_t i = 0; i < tl.clips.size(); ++i) {
        if (tl.clips[i]->id == id) {
            return i;
        }
    }
    return invalid("Clip inexistente: " + std::string(id));
}

inline Result<std::size_t> find_track(const Timeline& tl, std::string_view id) {
    for (std::size_t i = 0; i < tl.tracks.size(); ++i) {
        if (tl.tracks[i].id == id) {
            return i;
        }
    }
    return invalid("Pista inexistente: " + std::string(id));
}

inline bool has_text(const std::optional<std::string>& s) { return s && !s->empty(); }

// Valor de una clave de un objeto JSON (null si falta o si no es un objeto).
inline const Json& field(const Json& obj, std::string_view key) {
    static const Json kNull;
    if (!obj.is_object()) {
        return kNull;
    }
    const auto it = obj.find(key);
    return it == obj.end() ? kNull : *it;
}

// Vídeo o imagen (clip_kind.is_visual_clip).
inline bool is_visual(const TimelineClip& c) { return c.kind == "video" || c.kind == "image"; }

// Tras partir un clip en `rel` (s locales), la parte derecha continúa sus
// keyframes donde iban: t − rel e ids nuevos (edit_ops.cpp).
Result<void> shift_keyframe_items(TimelineClip& clip, double rel, IdSource& ids);

// --- Conversión de parámetros con la semántica de Python ---------------------------------
// (la usan los kwargs de los comandos y los `patch` de update_clip)

// `x is not None` → bool(x)
inline std::optional<bool> opt_truthy(const Json& v) {
    return v.is_null() ? std::nullopt : std::optional<bool>(py_truthy(v));
}

// float(x)
inline Result<double> to_py_float(const Json& v, std::string_view what) {
    if (const auto f = py_float(v)) {
        return *f;
    }
    return invalid(std::string(what) + " debe ser un número");
}

inline Result<std::optional<double>> opt_py_float(const Json& v, std::string_view what) {
    if (v.is_null()) {
        return std::optional<double>{};
    }
    auto f = to_py_float(v, what);
    if (!f) {
        return std::unexpected(f.error());
    }
    return std::optional<double>{*f};
}

// Operando de una comparación (`x < 0`): número o bool, nunca texto.
inline Result<std::optional<double>> opt_py_number(const Json& v, std::string_view what) {
    if (v.is_null()) {
        return std::optional<double>{};
    }
    if (const auto n = py_number(v)) {
        return n;
    }
    return invalid(std::string(what) + " debe ser un número");
}

// Texto opcional; con `falsy_is_none`, el `x or None` de Python ("" y 0 = ausente).
inline Result<std::optional<std::string>> opt_string(const Json& v, std::string_view what, bool falsy_is_none) {
    if (v.is_null() || (falsy_is_none && !py_truthy(v))) {
        return std::optional<std::string>{};
    }
    if (!v.is_string()) {
        return invalid(std::string(what) + " debe ser texto");
    }
    return std::optional<std::string>{v.get<std::string>()};
}

inline Result<std::string> req_string(const Json& v, std::string_view what) {
    if (!v.is_string()) {
        return invalid(std::string(what) + " debe ser texto");
    }
    return v.get<std::string>();
}

// Desempaqueta un Result o devuelve su error.
#define GC_TRY(var, expr)                                                                                             \
    auto var##_r = (expr);                                                                                            \
    if (!var##_r) {                                                                                                   \
        return std::unexpected(var##_r.error());                                                                      \
    }                                                                                                                 \
    auto var = std::move(*var##_r) // NOLINT(bugprone-macro-parentheses): declara `var`

} // namespace gc::core::detail
