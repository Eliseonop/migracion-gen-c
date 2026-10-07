#include "gc/core/clip_attrs.hpp"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <optional>
#include <set>

#include "edit_util.hpp"
#include "gc/core/keyframes.hpp"
#include "gc/core/serialize.hpp"

namespace gc::core {

using detail::field;
using detail::invalid;
using detail::py_iter;
using detail::py_len;
using detail::py_num;
using detail::py_round;
using detail::py_truthy;
using detail::to_py_float;

namespace {

constexpr double kEps = 1e-6;
constexpr double kTEps = 1e-4;

using Keys = std::vector<std::string_view>;

bool in(std::string_view v, std::initializer_list<std::string_view> list) { return std::ranges::find(list, v) != list.end(); }

// --- Grupos -----------------------------------------------------------------------------

bool kind_fits(std::string_view group, std::string_view kind) {
    const bool visual = in(kind, {"video", "image", "shape", "text"});
    const bool media = in(kind, {"video", "image"});
    const bool sound = in(kind, {"video", "audio"});
    if (in(group, {"transform", "flip", "blend", "animation", "animations", "mask"})) {
        return visual;
    }
    if (group == "transitions") {
        return in(kind, {"video", "image", "text"});
    }
    if (in(group, {"crop", "vfx", "chroma"})) {
        return media;
    }
    if (group == "effects") {
        return in(kind, {"video", "image", "adjustment"});
    }
    if (group == "style") {
        return in(kind, {"text", "shape"});
    }
    if (in(group, {"speed", "audio"})) {
        return sound;
    }
    return false;
}

// Propiedades animables que lleva cada grupo (_KF_FAMILIES).
Keys kf_family(std::string_view group) {
    if (group == "animation") {
        return {"x", "y", "scale", "rotation", "opacity", "rot_x", "rot_y", "draw"};
    }
    if (group == "crop") {
        return {"cx", "cy", "zoom"};
    }
    if (group == "mask") {
        return {"mx", "my", "mw", "mh", "msx", "msy", "mrot", "mfeather"};
    }
    if (group == "audio") {
        Keys out{"volume"};
        out.insert(out.end(), kKfPropKeys.end() - 11, kKfPropKeys.end()); // los efectos de audio
        return out;
    }
    if (group == "style") {
        return {kTextStyleKfKeys.begin(), kTextStyleKfKeys.end()};
    }
    return {};
}

bool touches_keyframes(std::string_view group) {
    return in(group, {"animation", "crop", "mask", "audio", "style", "transform", "blend"});
}

// --- Utilidades sobre el model_dump() ------------------------------------------------------

const std::string& kind_of(const Json& clip) {
    static const std::string kEmpty;
    const Json& k = field(clip, "kind");
    return k.is_string() ? k.get_ref<const std::string&>() : kEmpty;
}

// Dónde guardan su pose los textos ("style") y las figuras ("shape"); los demás, en "transform".
std::string_view holder(const Json& clip) {
    const auto& kind = kind_of(clip);
    return kind == "text" ? "style" : kind == "shape" ? "shape" : "";
}

// `x or {}` antes de `{**x}` / `.get()`: lo que no es un dict (y es verdadero) es un error.
Result<Json> dict_or_empty(const Json& v, std::string_view what) {
    if (!py_truthy(v)) {
        return Json::object();
    }
    if (!v.is_object()) {
        return invalid(std::string(what) + " debe ser un objeto");
    }
    return v;
}

// dict(x or {}): un dict, o una lista de pares clave-valor. Las claves que no son texto
// se descartan, porque nunca sobreviven a _restrict_items ni a _style_items_of.
Result<Json> dict_of(const Json& v) {
    if (!py_truthy(v)) {
        return Json::object();
    }
    if (v.is_object()) {
        return v;
    }
    if (!v.is_array()) {
        return invalid("props debe ser un diccionario");
    }
    Json out = Json::object();
    for (const Json& el : v) {
        Json key;
        Json value;
        if (el.is_array() && el.size() == 2) {
            key = el[0];
            value = el[1];
        } else if (el.is_object() && el.size() == 2) { // un dict se recorre por sus claves
            key = el.begin().key();
            value = std::next(el.begin()).key();
        } else if (el.is_string() && py_len(el.get_ref<const std::string&>()) == 2) {
            const auto& s = el.get_ref<const std::string&>();
            const auto cut = (static_cast<unsigned char>(s[0]) < 0x80) ? 1
                             : (static_cast<unsigned char>(s[0]) < 0xE0) ? 2
                             : (static_cast<unsigned char>(s[0]) < 0xF0) ? 3
                                                                          : 4;
            key = s.substr(0, cut);
            value = s.substr(cut);
        } else {
            return invalid("props debe ser un diccionario");
        }
        if (key.is_array() || key.is_object()) {
            return invalid("props: clave no válida");
        }
        if (key.is_string()) {
            out[key.get<std::string>()] = value;
        }
    }
    return out;
}

double t_of(const Json& item) { return item.at("t").get<double>(); }

const Json& prop(const Json& item, std::string_view key) { return field(item.at("props"), key); }

// --- Valores fijos (pose, opacidad) ------------------------------------------------------

Result<Json> pose_of(const Json& clip) {
    const auto h = holder(clip);
    GC_TRY(src, dict_or_empty(field(clip, h.empty() ? "transform" : h), h.empty() ? "transform" : h));
    Json out = Json::object();
    out["x"] = py_num(field(src, "x"), 0.5);
    out["y"] = py_num(field(src, "y"), 0.5);
    out["scale"] = py_num(field(src, "scale"), 1.0);
    out["rotation"] = py_num(field(src, "rotation"), 0.0);
    return out;
}

Result<double> opacity_of(const Json& clip) {
    const auto h = holder(clip);
    if (h.empty()) {
        return py_num(field(clip, "opacity"), 1.0);
    }
    GC_TRY(st, dict_or_empty(field(clip, h), h));
    return py_num(field(st, "opacity"), 1.0);
}

// --- Cada grupo (_APPLY) ------------------------------------------------------------------

Result<void> paste_transform(Json& out, const Json& src) {
    const auto h = holder(out);
    const std::string key(h.empty() ? "transform" : h);
    GC_TRY(merged, dict_or_empty(field(out, key), key));
    GC_TRY(pose, pose_of(src));
    for (const auto& [k, v] : pose.items()) {
        merged[k] = v;
    }
    out[key] = std::move(merged);
    if (kind_of(out) == "text" && kind_of(src) == "text") {
        GC_TRY(sst, dict_or_empty(field(src, "style"), "style"));
        for (const char* k : {"rot_x", "rot_y", "perspective"}) {
            if (field(sst, k).is_null()) {
                out["style"].erase(k);
            } else {
                out["style"][k] = sst.at(k);
            }
        }
    }
    if (in(kind_of(out), {"video", "image"}) && in(kind_of(src), {"video", "image"})) {
        for (const char* k : {"layout", "frame"}) {
            if (py_truthy(field(src, k))) {
                out[k] = src.at(k);
            }
        }
    }
    return {};
}

Result<void> paste_blend(Json& out, const Json& src) {
    GC_TRY(o, opacity_of(src));
    const auto h = holder(out);
    if (h.empty()) {
        out["opacity"] = o;
    } else {
        const std::string key(h);
        GC_TRY(merged, dict_or_empty(field(out, key), key));
        merged["opacity"] = o;
        out[key] = std::move(merged);
    }
    const Json& mode = field(src, "blend_mode");
    out["blend_mode"] = py_truthy(mode) ? mode : Json();
    return {};
}

// Copia una lista de dicts con un id nuevo en cada uno (`[{**x, key: id} for x in …]`).
Result<Json> with_new_ids(const Json& list, std::string_view key, IdSource& ids, std::string_view prefix,
                          std::size_t hex_len, bool skip_non_dicts) {
    Json out = Json::array();
    if (!py_truthy(list)) {
        return out;
    }
    const auto items = py_iter(list);
    if (!items) {
        return invalid(std::string(key) + ": no es una lista");
    }
    for (const Json& x : *items) {
        if (!x.is_object()) {
            if (skip_non_dicts) {
                continue;
            }
            return invalid(std::string(key) + ": cada elemento debe ser un objeto");
        }
        Json item = x;
        item[std::string(key)] = std::string(prefix) + ids.hex32().substr(0, hex_len);
        out.push_back(std::move(item));
    }
    return out;
}

Result<void> paste_crop(Json& out, const Json& src, IdSource& ids) {
    if (!py_truthy(field(src, "reframe"))) {
        out["reframe"] = nullptr;
        return {};
    }
    Json rf = src.at("reframe");
    for (const char* k : {"keyframes", "keyframes2"}) {
        // El id no está en el modelo (se descarta al validar), pero gasta un uuid4 como en Python.
        GC_TRY(list, with_new_ids(field(rf, k), "id", ids, "k", 8, false));
        rf[k] = std::move(list);
    }
    // `master` es de cómo está guardado el ARCHIVO del destino.
    GC_TRY(trf, dict_or_empty(field(out, "reframe"), "reframe"));
    rf["master"] = py_truthy(field(trf, "master"));
    out["reframe"] = std::move(rf);
    return {};
}

Result<void> paste_chroma(Json& out, const Json& src) {
    const Json& sraw = field(src, "bg_removal");
    const Json& traw = field(out, "bg_removal");
    const Json* sb = sraw.is_object() ? &sraw : nullptr;
    const Json* tb = traw.is_object() ? &traw : nullptr;
    const bool s_ok = sb != nullptr && !sb->empty();
    const bool t_ok = tb != nullptr && !tb->empty();
    if (!s_ok && !t_ok) {
        return {};
    }
    // El recorte IA (auto) es del archivo del destino: nunca se pega.
    Json next = t_ok ? *tb : Json::object();
    for (const char* k : {"chroma", "outline"}) {
        if (s_ok && py_truthy(field(*sb, k))) {
            next[k] = sb->at(k);
        } else {
            next.erase(k);
        }
    }
    if (!t_ok) {
        const Json& en = field(*sb, "enabled");
        next["enabled"] = !(en.is_boolean() && !en.get<bool>());
        if (py_truthy(field(*sb, "mode"))) {
            next["mode"] = sb->at("mode");
        }
    } else if (s_ok) {
        GC_TRY(chroma, dict_or_empty(field(*sb, "chroma"), "bg_removal.chroma"));
        if (py_truthy(field(chroma, "enabled"))) {
            next["enabled"] = true;
        }
    }
    const bool keep =
        py_truthy(field(next, "chroma")) || py_truthy(field(next, "outline")) || py_truthy(field(next, "auto"));
    out["bg_removal"] = keep ? std::move(next) : Json();
    return {};
}

Result<void> paste_style(Json& out, const Json& src) {
    if (kind_of(out) == "text") {
        GC_TRY(st, dict_or_empty(field(src, "style"), "style"));
        GC_TRY(tst, dict_or_empty(field(out, "style"), "style"));
        for (const char* k : {"x", "y", "scale", "rotation", "rot_x", "rot_y", "perspective", "opacity"}) {
            if (field(tst, k).is_null()) {
                st.erase(k);
            } else {
                st[k] = tst.at(k);
            }
        }
        out["style"] = std::move(st);
        return {};
    }
    GC_TRY(shape, dict_or_empty(field(out, "shape"), "shape"));
    GC_TRY(sshape, dict_or_empty(field(src, "shape"), "shape"));
    for (const char* k : {"fill", "stroke", "strokeWidth", "cornerRadius", "dash"}) {
        if (field(sshape, k).is_null()) {
            shape.erase(k);
        } else {
            shape[k] = sshape.at(k);
        }
    }
    out["shape"] = std::move(shape);
    return {};
}

Result<void> apply_group(std::string_view g, Json& out, const Json& src, IdSource& ids) {
    auto truthy_or = [&](const char* key, Json fallback) {
        const Json& v = field(src, key);
        return py_truthy(v) ? v : fallback;
    };
    if (g == "transform") {
        return paste_transform(out, src);
    }
    if (g == "flip") {
        out["flip_h"] = py_truthy(field(src, "flip_h"));
        out["flip_v"] = py_truthy(field(src, "flip_v"));
    } else if (g == "blend") {
        return paste_blend(out, src);
    } else if (g == "animation") {
        out["anim"] = field(src, "anim"); // pistas `anim` antiguas
    } else if (g == "transitions") {
        out["appear"] = truthy_or("appear", "none");
        out["exit"] = truthy_or("exit", "none");
    } else if (g == "animations") {
        out["animations"] = field(src, "animations");
    } else if (g == "crop") {
        return paste_crop(out, src, ids);
    } else if (g == "effects") {
        out["effects"] = truthy_or("effects", Json::object());
        out["look"] = truthy_or("look", "none");
        out["filters"] = field(src, "filters");
    } else if (g == "vfx") {
        // Efectos animados: cada copia con su propio uid.
        GC_TRY(vfx, with_new_ids(field(src, "vfx"), "uid", ids, "fx", 10, true));
        out["vfx"] = vfx.empty() ? Json() : std::move(vfx);
    } else if (g == "mask") {
        GC_TRY(masks, with_new_ids(field(src, "masks"), "id", ids, "m", 8, false));
        out["masks"] = std::move(masks);
    } else if (g == "chroma") {
        return paste_chroma(out, src);
    } else if (g == "style") {
        return paste_style(out, src);
    } else if (g == "speed") {
        out["speed"] = py_num(field(src, "speed"), 1.0);
        const Json& kp = field(src, "keep_pitch");
        out["keep_pitch"] = !(kp.is_boolean() && !kp.get<bool>());
        out["reverse"] = py_truthy(field(src, "reverse"));
    } else if (g == "audio") {
        out["volume"] = py_num(field(src, "volume"), 1.0);
        out["muted"] = py_truthy(field(src, "muted"));
        out["audio_fx"] = truthy_or("audio_fx", Json::object());
    }
    return {};
}

// --- Keyframes -----------------------------------------------------------------------------

// Items con t (como real) y props (como dict), ordenados por t.
Result<std::vector<Json>> items_of(const Json& clip) {
    std::vector<Json> out;
    const Json& kf = field(clip, "keyframes");
    if (!kf.is_object() || !py_truthy(field(kf, "enabled")) || !field(kf, "items").is_array()) {
        return out;
    }
    for (const Json& k : kf.at("items")) {
        if (!k.is_object()) {
            continue;
        }
        const double t = py_num(field(k, "t"), std::numeric_limits<double>::quiet_NaN());
        if (std::isnan(t)) {
            continue;
        }
        GC_TRY(props, dict_of(field(k, "props")));
        Json item = k;
        item["t"] = t;
        item["props"] = std::move(props);
        out.push_back(std::move(item));
    }
    std::ranges::stable_sort(out, {}, t_of);
    return out;
}

bool is_crop_key(std::string_view key) { return in(key, {"cx", "cy", "zoom"}); }

// Solo las propiedades `keys` que cambian algo (fuera las que valen lo mismo que el
// valor fijo en todos los keyframes) y sin keyframes vacíos.
Result<std::vector<Json>> restrict_items(const std::vector<Json>& items, const Keys& keys, const KfProps& base) {
    Keys keep;
    for (const auto key : keys) {
        if (std::ranges::none_of(items, [&](const Json& k) { return !prop(k, key).is_null(); })) {
            continue;
        }
        if (is_crop_key(key)) {
            keep.push_back(key);
            continue;
        }
        bool all_same = true;
        for (const Json& k : items) {
            const Json& v = prop(k, key);
            if (v.is_null()) {
                continue;
            }
            GC_TRY(f, to_py_float(v, key));
            if (!(std::abs(f - base.get(key)) < kEps)) {
                all_same = false;
                break;
            }
        }
        if (!all_same) {
            keep.push_back(key);
        }
    }
    std::vector<Json> out;
    for (const Json& k : items) {
        Json props = Json::object();
        for (const auto key : keep) {
            const Json& v = prop(k, key);
            if (!v.is_null()) {
                GC_TRY(f, to_py_float(v, key));
                props[std::string(key)] = f;
            }
        }
        if (!props.empty()) {
            Json item = k;
            item["props"] = std::move(props);
            out.push_back(std::move(item));
        }
    }
    return out;
}

// Claves de las props en orden de aparición (_keys_of).
std::vector<std::string> keys_of(const std::vector<Json>& items) {
    std::vector<std::string> out;
    for (const Json& k : items) {
        for (const auto& [key, v] : k.at("props").items()) {
            if (std::ranges::find(out, key) == out.end()) {
                out.push_back(key);
            }
        }
    }
    return out;
}

// Props que valen lo mismo en todos los items; nullopt si alguna cambia o falta.
// Python recorre un `set` (orden arbitrario); aquí van en orden de aparición. El
// exportador de fixtures usa en Python la misma función en ese orden.
std::optional<Json> constant_props(const std::vector<Json>& items) {
    Json out = Json::object();
    for (const auto& key : keys_of(items)) {
        const Json& v0 = prop(items.front(), key);
        if (v0.is_null()) {
            return std::nullopt;
        }
        for (const Json& k : items) {
            const Json& v = prop(k, key);
            if (v.is_null() || std::abs(v.get<double>() - v0.get<double>()) > kEps) {
                return std::nullopt;
            }
        }
        out[key] = v0;
    }
    return out;
}

// Origen y destino animan cosas distintas en instantes distintos: un keyframe en
// cada instante de cualquiera de los dos, con todas las propiedades interpoladas.
std::vector<Json> resample(const std::vector<Json>& s, const std::vector<Json>& t, const KfProps& base) {
    std::vector<double> all;
    for (const auto* list : {&s, &t}) {
        for (const Json& k : *list) {
            all.push_back(t_of(k));
        }
    }
    std::ranges::sort(all);
    std::vector<double> times;
    for (const double v : all) {
        if (times.empty() || v - times.back() > kTEps) {
            times.push_back(v);
        }
    }
    const auto s_keys = keys_of(s);
    const auto t_keys = keys_of(t);
    std::vector<Json> out;
    for (const double time : times) {
        const Json* own = nullptr;
        for (const auto* list : {&s, &t}) {
            for (const Json& k : *list) {
                if (own == nullptr && std::abs(t_of(k) - time) <= kTEps) {
                    own = &k;
                }
            }
        }
        const KfProps ps = interp_items(s, time, base);
        const KfProps pt = interp_items(t, time, base);
        Json props = Json::object();
        for (const auto& key : t_keys) {
            props[key] = py_round(pt.get(key), 6);
        }
        for (const auto& key : s_keys) {
            props[key] = py_round(ps.get(key), 6);
        }
        const Json& interp = own != nullptr ? field(*own, "interpolation") : Json();
        Json item = Json::object();
        item["t"] = time;
        item["interpolation"] = py_truthy(interp) ? interp : Json("linear");
        item["props"] = std::move(props);
        if (own != nullptr && py_truthy(field(*own, "bezier"))) {
            item["bezier"] = own->at("bezier");
        }
        out.push_back(std::move(item));
    }
    return out;
}

// Keyframes del estilo del texto (van por propiedad): se llevan tal cual.
Result<std::vector<Json>> style_items_of(const Json& clip) {
    GC_TRY(items, items_of(clip));
    std::vector<Json> out;
    for (const Json& k : items) {
        Json props = Json::object();
        for (const auto key : kTextStyleKfKeys) {
            const Json& v = prop(k, key);
            if (!v.is_null() && !(v.is_string() && v.get_ref<const std::string&>().empty())) {
                props[std::string(key)] = v;
            }
        }
        if (!props.empty()) {
            Json item = k;
            item["props"] = std::move(props);
            out.push_back(std::move(item));
        }
    }
    return out;
}

std::vector<Json> with_style_items(std::vector<Json> items, const std::vector<Json>& style) {
    for (const Json& st : style) {
        const auto hit = std::ranges::find_if(items, [&](const Json& k) { return std::abs(t_of(k) - t_of(st)) <= kTEps; });
        if (hit != items.end()) {
            for (const auto& [key, v] : st.at("props").items()) {
                (*hit)["props"][key] = v;
            }
        } else {
            items.push_back(st);
        }
    }
    std::ranges::stable_sort(items, {}, t_of);
    return items;
}

// Keyframes del destino tras pegar las propiedades `pasted` del origen: las del origen
// sustituyen a las del destino (si el origen no las anima, el destino deja de animarlas)
// y el resto del destino se conserva.
Result<Json> merge_keyframes(const Json& target, const Json& source, const TimelineClip& target_clip,
                             const TimelineClip& source_clip, const std::set<std::string_view>& pasted,
                             IdSource& ids) {
    Keys s_keys;
    Keys t_keys;
    for (const auto key : kKfPropKeys) {
        (pasted.contains(key) ? s_keys : t_keys).push_back(key);
    }
    GC_TRY(s_items, items_of(source));
    GC_TRY(s, restrict_items(s_items, s_keys, static_props(source_clip)));
    GC_TRY(t_items, items_of(target));
    GC_TRY(t, restrict_items(t_items, t_keys, static_props(target_clip)));
    // El estilo animado del texto viene del origen si se pega el Estilo; si no, el del destino.
    const bool style_from_source = std::ranges::any_of(kTextStyleKfKeys, [&](auto k) { return pasted.contains(k); });
    GC_TRY(style, style_items_of(style_from_source ? source : target));
    const Json& tkf = field(target, "keyframes");
    if (s.empty() && style.empty() && !(tkf.is_object() && py_truthy(field(tkf, "enabled")))) {
        return tkf;
    }
    std::vector<Json> items;
    if (s.empty()) {
        items = std::move(t);
    } else if (t.empty()) {
        items = std::move(s);
    } else {
        const auto ct = constant_props(t);
        const auto cs = ct ? std::nullopt : constant_props(s);
        if (ct || cs) {
            items = ct ? std::move(s) : std::move(t);
            for (Json& k : items) {
                for (const auto& [key, v] : (ct ? *ct : *cs).items()) {
                    k["props"][key] = v;
                }
            }
        } else {
            items = resample(s, t, static_props(target_clip));
        }
    }
    items = with_style_items(std::move(items), style);
    if (items.empty()) {
        return Json();
    }
    Json out_items = Json::array();
    for (Json& k : items) {
        k["id"] = make_uid(ids, "k");
        out_items.push_back(std::move(k));
    }
    Json out = Json::object();
    out["enabled"] = true;
    out["items"] = std::move(out_items);
    return out;
}

} // namespace

bool group_applies(std::string_view group, const TimelineClip& source, const TimelineClip& target) {
    if (std::ranges::find(kAttrGroupIds, group) == kAttrGroupIds.end()) {
        return false;
    }
    if (!kind_fits(group, source.kind) || !kind_fits(group, target.kind)) {
        return false;
    }
    return !(group == "style" && source.kind != target.kind);
}

Result<Json> paste_attributes(const TimelineClip& target, const TimelineClip& source,
                              const std::vector<std::string>& groups, IdSource& ids) {
    Json out = to_json(target);
    if (target.id == source.id) {
        return out;
    }
    const std::set<std::string_view> wanted(groups.begin(), groups.end());
    std::vector<std::string_view> chosen;
    for (const auto g : kAttrGroupIds) {
        if (wanted.contains(g) && group_applies(g, source, target)) {
            chosen.push_back(g);
        }
    }
    if (chosen.empty()) {
        return out;
    }
    const Json src = to_json(source);
    const Json original = out;
    for (const auto g : chosen) {
        if (auto r = apply_group(g, out, src, ids); !r) {
            return std::unexpected(r.error());
        }
    }
    if (std::ranges::any_of(chosen, touches_keyframes)) {
        std::set<std::string_view> pasted;
        for (const auto g : chosen) {
            for (const auto k : kf_family(g)) {
                pasted.insert(k);
            }
        }
        GC_TRY(kf, merge_keyframes(original, src, target, source, pasted, ids));
        out["keyframes"] = std::move(kf);
    }
    return out;
}

} // namespace gc::core
