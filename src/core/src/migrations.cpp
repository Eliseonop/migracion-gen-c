#include "gc/core/migrations.hpp"

#include <array>
#include <cstdint>
#include <limits>
#include <vector>

#include "gc/core/model.hpp"
#include "json_reader.hpp"

namespace gc::core {

namespace {

using detail::py_truthy;

const Json* find(const Json& obj, const char* key) {
    if (!obj.is_object()) {
        return nullptr;
    }
    const auto it = obj.find(key);
    return it == obj.end() ? nullptr : &*it;
}

// `tl.get("clips") or []` de Python: lo que no sea una lista con contenido se
// trata como lista vacía (y el paso deja "clips" = [] en la salida).
Json clips_of(const Json& tl) {
    const Json* clips = find(tl, "clips");
    if (clips == nullptr || !py_truthy(*clips)) {
        return Json::array();
    }
    return *clips;
}

// v1 → v2: timing real por palabra. Puramente aditivo: solo se sella la versión.
Json v1_to_v2(Json tl) { return tl; }

// v2 → v3: los clips de texto fijan su rol (caption/free).
Json v2_to_v3(Json tl) {
    Json clips = clips_of(tl);
    if (clips.is_array()) {
        for (Json& c : clips) {
            if (!c.is_object()) {
                continue;
            }
            const Json* kind = find(c, "kind");
            if (kind == nullptr || *kind != "text") {
                continue;
            }
            const auto role = resolve_text_role(c);
            c["text_role"] = role ? Json(*role) : Json(nullptr);
        }
    }
    tl["clips"] = std::move(clips);
    return tl;
}

// v3 → v4: el preview conserva el tono al acelerar; keep_pitch:false (que el
// editor nunca aplicó) pasa a true para que el export suene igual.
Json v3_to_v4(Json tl) {
    Json clips = clips_of(tl);
    if (clips.is_array()) {
        for (Json& c : clips) {
            if (!c.is_object()) {
                continue;
            }
            const Json* keep = find(c, "keep_pitch");
            if (keep != nullptr && keep->is_boolean() && !keep->get<bool>()) {
                c["keep_pitch"] = true;
            }
        }
    }
    tl["clips"] = std::move(clips);
    return tl;
}

// v4 → v5: las pistas de vídeo y texto pasan a ser UNA pila ordenada por el array.
// Hasta v4 el texto iba siempre encima del vídeo: se reordenan (vídeos primero,
// textos detrás, cada grupo en su orden) en los mismos huecos del array. El audio
// no se mueve.
Json v4_to_v5(Json tl) {
    const Json* tracks = find(tl, "tracks");
    if (tracks == nullptr || !tracks->is_array()) {
        return tl;
    }
    auto kind_of = [](const Json& t) -> const Json* { return t.is_object() ? find(t, "kind") : nullptr; };
    std::vector<std::size_t> slots;
    std::vector<Json> videos;
    std::vector<Json> texts;
    for (std::size_t i = 0; i < tracks->size(); ++i) {
        const Json& t = (*tracks)[i];
        const Json* kind = kind_of(t);
        if (kind == nullptr) {
            continue;
        }
        if (*kind == "video") {
            slots.push_back(i);
            videos.push_back(t);
        } else if (*kind == "text") {
            slots.push_back(i);
            texts.push_back(t);
        }
    }
    Json out_tracks = *tracks;
    std::size_t s = 0;
    for (Json& t : videos) {
        out_tracks[slots[s++]] = std::move(t);
    }
    for (Json& t : texts) {
        out_tracks[slots[s++]] = std::move(t);
    }
    tl["tracks"] = std::move(out_tracks);
    return tl;
}

using Step = Json (*)(Json);
// Índice = versión de origen − 1: el paso lleva de N a N + 1.
constexpr std::array<Step, kCurrentSchemaVersion - 1> kSteps{v1_to_v2, v2_to_v3, v3_to_v4, v4_to_v5};

} // namespace

int timeline_version(const Json& timeline) {
    const Json* v = find(timeline, "schema_version");
    if (v == nullptr) {
        return 1;
    }
    // isinstance(v, int) de Python: los bool también son int (True == 1).
    if (v->is_boolean()) {
        return 1;
    }
    if (v->is_number_unsigned()) {
        const auto n = v->get<std::uint64_t>();
        return n > static_cast<std::uint64_t>(std::numeric_limits<int>::max()) ? std::numeric_limits<int>::max()
                                                                                : static_cast<int>(n);
    }
    if (v->is_number_integer()) {
        const auto n = v->get<std::int64_t>();
        if (n <= 0) {
            return 1;
        }
        return n > std::numeric_limits<int>::max() ? std::numeric_limits<int>::max() : static_cast<int>(n);
    }
    return 1;
}

Json migrate_timeline(const Json& timeline) {
    if (!timeline.is_object() || timeline.empty()) {
        return timeline;
    }
    int v = timeline_version(timeline);
    if (v >= kCurrentSchemaVersion) {
        return timeline;
    }
    Json out = timeline;
    while (v < kCurrentSchemaVersion) {
        out = kSteps[static_cast<std::size_t>(v - 1)](std::move(out));
        ++v;
    }
    out["schema_version"] = v;
    return out;
}

std::optional<std::string> resolve_text_role(const Json& clip) {
    if (!clip.is_object()) {
        return std::nullopt;
    }
    const Json* kind = find(clip, "kind");
    if (kind == nullptr || *kind != "text") {
        return std::nullopt;
    }
    if (const Json* role = find(clip, "text_role"); role != nullptr && (*role == "caption" || *role == "free")) {
        return role->get<std::string>();
    }
    const Json* origin = find(clip, "origin");
    const Json* words = find(clip, "words");
    if ((origin != nullptr && py_truthy(*origin)) || (words != nullptr && words->is_array() && !words->empty())) {
        return "caption";
    }
    return "free";
}

} // namespace gc::core
