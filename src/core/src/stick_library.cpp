#include "gc/core/stick_library.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <format>
#include <system_error>
#include <tuple>
#include <utility>

#include "disk_scan.hpp"
#include "gc/core/io.hpp"
#include "json_reader.hpp"
#include "py.hpp"

namespace gc::core {

namespace fs = std::filesystem;
using detail::py_equal;
using detail::py_iter;
using detail::py_lower;
using detail::py_path_name;
using detail::py_path_stem;
using detail::py_path_str;
using detail::py_path_suffix;
using detail::py_str;
using detail::py_strip;
using detail::py_truthy;

namespace {

constexpr std::string_view kManifest = "stick.json";
constexpr std::string_view kOther = "otros";
constexpr std::string_view kDefaultChroma = "#00FF00";
constexpr std::int64_t kNoOrder = 999;

struct DefaultExpression {
    std::string_view id;
    std::string_view label;
    std::string_view emoji;
};
// La taxonomía de un stick nuevo (DEFAULT_EXPRESSIONS, la de scientist_stick).
constexpr std::array<DefaultExpression, 11> kDefaultExpressions{{
    {"happy", "Feliz", "😄"},
    {"sad", "Triste", "😢"},
    {"angry", "Enojado", "😠"},
    {"surprised", "Sorprendido", "😮"},
    {"scared", "Asustado", "😨"},
    {"confused", "Confundido", "😕"},
    {"thinking", "Pensando", "🤔"},
    {"explaining", "Explicando", "👉"},
    {"tired", "Cansado", "😴"},
    {"neutral", "Neutral", "🙂"},
    {kOther, "Otros", "✨"},
}};

const Json kNull;

// Una excepción de Python que sale de la función: se convierte en el Result de la API.
struct PyFailure : std::exception {
    explicit PyFailure(Error e) : error(std::move(e)) {}
    [[nodiscard]] const char* what() const noexcept override { return error.message.c_str(); }
    Error error;
};

[[noreturn]] void py_raise(ErrorCode code, std::string message) { throw PyFailure(Error{code, std::move(message), {}}); }

template <class F>
Result<Json> guarded(F&& f) {
    try {
        return std::forward<F>(f)();
    } catch (const PyFailure& e) {
        return std::unexpected(e.error);
    }
}

// d.get(key) de Python sobre un objeto JSON (None si no está o no es un objeto).
const Json& get(const Json& obj, std::string_view key) {
    const auto it = obj.find(key);
    return it == obj.end() ? kNull : *it;
}

// `a or b` de Python.
const Json& or_else(const Json& a, const Json& b) { return py_truthy(a) ? a : b; }

// str(x or "") de Python.
std::string str_or_empty(const Json& j) { return py_truthy(j) ? py_str(j) : std::string(); }

std::string slashes(std::string s) {
    std::ranges::replace(s, '\\', '/');
    return s;
}

// for x in (v or []): los elementos de una lista, las claves de un dict o los caracteres de un
// texto. Un número o un bool: TypeError.
std::vector<Json> iter_or_empty(const Json& v, std::string_view what) {
    if (!py_truthy(v)) {
        return {};
    }
    auto items = py_iter(v);
    if (!items) {
        py_raise(ErrorCode::InvalidArgument, std::format("stick.json: «{}» no es una lista ({})", what,
                                                      detail::json_type_name(v)));
    }
    return std::move(*items);
}

// --- Claves de dict y set de Python (los ids de las expresiones) ------------------------

// hash(x): una lista o un dict no puede ser clave (TypeError).
void require_hashable(const Json& j) {
    if (j.is_array() || j.is_object()) {
        py_raise(ErrorCode::InvalidArgument,
              std::format("stick.json: un id de expresión no puede ser {}", detail::json_type_name(j)));
    }
}

// La igualdad de las claves de un dict o un set: la de Python, y los NaN de json.loads son el
// mismo objeto (el set los compara por identidad antes que con ==).
bool key_equal(const Json& a, const Json& b) {
    if (a.is_number_float() && b.is_number_float() && std::isnan(a.get<double>()) && std::isnan(b.get<double>())) {
        return true;
    }
    return py_equal(a, b);
}

// Posición de la clave `k` en `keys`; nullopt si no está.
std::optional<std::size_t> key_index(const std::vector<Json>& keys, const Json& k) {
    require_hashable(k);
    const auto it = std::ranges::find_if(keys, [&](const Json& x) { return key_equal(x, k); });
    return it == keys.end() ? std::nullopt : std::optional(static_cast<std::size_t>(it - keys.begin()));
}

// sorted(ids, key=lambda eid: (order_index.get(eid, 999), eid)): por la posición en `ordered`
// y, entre las que empatan, por el id. Como Python, falla si el empate junta texto y números
// (los ids son verdaderos: nunca None, 0 ni «»). Un empate entre números se deja como está:
// siempre trae un número que no es una expresión del stick y no tiene etiqueta, y Python falla
// después en su .replace() (el detalle también).
std::vector<Json> sort_ids(const std::vector<Json>& ids, const std::vector<Json>& ordered) {
    struct Keyed {
        std::size_t index;
        Json id;
    };
    std::vector<Keyed> keyed;
    keyed.reserve(ids.size());
    for (const Json& id : ids) {
        keyed.push_back({key_index(ordered, id).value_or(kNoOrder), id});
    }
    std::ranges::stable_sort(keyed, {}, &Keyed::index);
    for (auto first = keyed.begin(); first != keyed.end();) {
        const auto last = std::find_if(first, keyed.end(), [&](const Keyed& k) { return k.index != first->index; });
        if (std::all_of(first, last, [](const Keyed& k) { return k.id.is_string(); })) {
            std::sort(first, last, [](const Keyed& a, const Keyed& b) {
                return a.id.get_ref<const std::string&>() < b.id.get_ref<const std::string&>();
            });
        } else if (!std::all_of(first, last, [](const Keyed& k) { return k.id.is_number() || k.id.is_boolean(); })) {
            py_raise(ErrorCode::InvalidArgument, "stick.json: «<» entre un id de expresión de texto y uno que no lo es");
        }
        first = last;
    }
    std::vector<Json> out;
    out.reserve(keyed.size());
    for (auto& k : keyed) {
        out.push_back(std::move(k.id));
    }
    return out;
}

// --- Manifiesto ------------------------------------------------------------------------

// _read_manifest: el dict de <base>/<cid>/stick.json, o nada si no está, no se lee o no es un dict.
std::optional<Json> read_manifest(const fs::path& base, std::string_view cid) {
    if (cid.contains('\0')) { // is_file() da False (Windows cortaría la ruta)
        return std::nullopt;
    }
    const fs::path file = base / path_from_utf8(cid) / kManifest;
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) {
        return std::nullopt;
    }
    auto data = read_json_file_py(file);
    if (!data || !data->is_object()) {
        return std::nullopt;
    }
    return std::move(*data);
}

void write_manifest(const fs::path& base, std::string_view cid, const Json& man) {
    if (auto w = write_json_file(base / path_from_utf8(cid) / kManifest, man, 2); !w) {
        throw PyFailure(w.error());
    }
}

std::string chroma_color(const Json& man) {
    const Json& chroma = get(man, "chroma");
    const Json& color = chroma.is_object() ? get(chroma, "color") : kNull;
    return py_truthy(color) ? py_str(color) : std::string(kDefaultChroma);
}

// _expr_defs: las expresiones del manifiesto que son un dict con id; si no hay, las de fábrica.
std::vector<Json> expr_defs(const Json& man) {
    std::vector<Json> defs;
    for (Json& e : iter_or_empty(get(man, "expressions"), "expressions")) {
        if (e.is_object() && py_truthy(get(e, "id"))) {
            defs.push_back(std::move(e));
        }
    }
    if (defs.empty()) {
        for (const auto& d : kDefaultExpressions) {
            defs.push_back(Json{{"id", d.id}, {"label", d.label}, {"emoji", d.emoji}});
        }
    }
    return defs;
}

// _root_and_manifest: la raíz, el id y el manifiesto de un stick.
struct Stick {
    fs::path base;
    std::string cid;
    Json man;
};

Stick root_and_manifest(const std::optional<fs::path>& base, std::string_view raw) {
    auto cid = safe_collection_id(raw);
    if (!cid) {
        throw PyFailure(cid.error());
    }
    auto man = base ? read_manifest(*base, *cid) : std::nullopt;
    if (!man) {
        py_raise(ErrorCode::NotFound, "Stick no encontrado.");
    }
    return {*base, std::move(*cid), std::move(*man)};
}

// --- Expresiones por carpeta -------------------------------------------------------------

struct Folder {
    std::string name; // la subcarpeta, como está en el disco
    Json id;
    Json label;
    Json emoji;
    bool custom = false;
};

// _folder_expressions: cada subcarpeta directa del stick con su expresión (por el id o la
// etiqueta, sin tildes ni mayúsculas) o como categoría propia.
std::vector<Folder> folder_expressions(const fs::path& coll_dir, const std::vector<Json>& defs) {
    std::vector<std::pair<std::string, const Json*>> by_key; // setdefault: manda la primera
    for (const Json& e : defs) {
        for (const auto& k : {stick_norm(get(e, "id")), stick_norm(or_else(get(e, "label"), Json("")))}) {
            if (!k.empty() && std::ranges::find(by_key, k, &std::pair<std::string, const Json*>::first) == by_key.end()) {
                by_key.emplace_back(k, &e);
            }
        }
    }
    std::vector<Folder> out;
    std::error_code ec;
    if (!fs::is_directory(coll_dir, ec)) {
        return out;
    }
    for (auto& name : detail::sorted_subdirs(coll_dir)) {
        const auto it = std::ranges::find(by_key, stick_norm(Json(name)), &std::pair<std::string, const Json*>::first);
        if (it != by_key.end()) {
            const Json& e = *it->second;
            out.push_back({std::move(name), get(e, "id"), or_else(get(e, "label"), get(e, "id")),
                           or_else(get(e, "emoji"), Json("")), false});
        } else {
            Json id = stick_slug(Json(name), kOther);
            Json label = name;
            out.push_back({std::move(name), std::move(id), std::move(label), Json("📁"), true});
        }
    }
    return out;
}

// _all_expressions: las del manifiesto y las categorías propias, sin repetir id.
std::vector<Json> all_expressions(const std::vector<Json>& defs, const std::vector<Folder>& folders) {
    std::vector<Json> seen;
    std::vector<Json> out;
    for (const Json& e : defs) {
        const Json& id = get(e, "id");
        if (!key_index(seen, id)) {
            seen.push_back(id);
            out.push_back(Json{{"id", id}, {"label", or_else(get(e, "label"), id)},
                               {"emoji", or_else(get(e, "emoji"), Json(""))}});
        }
    }
    for (const Folder& f : folders) {
        if (f.custom && !key_index(seen, f.id)) {
            seen.push_back(f.id);
            out.push_back(Json{{"id", f.id}, {"label", f.label}, {"emoji", f.emoji}});
        }
    }
    return out;
}

std::vector<Json> ids_of(const std::vector<Json>& exprs) {
    std::vector<Json> ids;
    ids.reserve(exprs.size());
    for (const Json& e : exprs) {
        ids.push_back(e.at("id"));
    }
    return ids;
}

// --- Recursos del stick ------------------------------------------------------------------

// El tipo de columna del stick para un tipo de colección: el GIF va con las imágenes.
std::optional<std::string_view> stick_kind(std::string_view collection_kind) {
    if (collection_kind == "video") {
        return "video";
    }
    if (collection_kind == "image" || collection_kind == "gif") {
        return "image";
    }
    return std::nullopt;
}

// Las partes de Path(text) de Windows para mirar la primera: las de una ruta con unidad o raíz
// empiezan por algo que nunca es el nombre de una subcarpeta.
std::vector<std::string> path_parts(std::string_view text) {
    std::vector<std::string> parts;
    const std::string s = py_path_str(text);
    if (s == ".") {
        return parts;
    }
    std::size_t start = 0;
    for (std::size_t pos; (pos = s.find('\\', start)) != std::string::npos; start = pos + 1) {
        parts.push_back(s.substr(start, pos - start));
    }
    parts.push_back(s.substr(start));
    return parts;
}

struct Collected {
    std::vector<Json> cards;
    std::vector<Json> exprs;
};

// _collect: los vídeos, imágenes y GIF del stick con su expresión y título. Primero los del
// manifiesto que existen y luego los demás, en el orden de sorted(rglob()).
Collected collect(const fs::path& base, const std::string& cid, const Json& man) {
    const fs::path coll_dir = base / path_from_utf8(cid);
    const auto defs = expr_defs(man);
    const auto folders = folder_expressions(coll_dir, defs);
    Collected out{{}, all_expressions(defs, folders)};
    const std::string color = chroma_color(man);

    std::vector<std::pair<std::string, Json>> listed; // dict: una clave repetida se queda en su sitio
    for (Json& it : iter_or_empty(get(man, "items"), "items")) {
        if (it.is_object() && py_truthy(get(it, "file"))) {
            std::string key = slashes(py_str(it.at("file")));
            const auto found = std::ranges::find(listed, key, &std::pair<std::string, Json>::first);
            if (found != listed.end()) {
                found->second = std::move(it);
            } else {
                listed.emplace_back(std::move(key), std::move(it));
            }
        }
    }

    const auto add = [&](const std::string& rel, const Json& expression, const Json& title) {
        Json card = collection_entry(cid, rel);
        const Json kind = card.at("kind");
        const auto sk = kind.is_string() ? stick_kind(kind.get_ref<const std::string&>()) : std::nullopt;
        if (!sk) {
            return;
        }
        card["rel"] = slashes(py_path_str(rel));
        card["stick_kind"] = std::string(*sk);
        card["title"] = title;
        card["expression"] = expression;
        card["chroma_color"] = color;
        card["scope"] = "collection";
        if (kind == "gif") {
            card["animated"] = true;
            card["loop"] = true;
        }
        out.cards.push_back(std::move(card));
    };
    const auto folder_expr = [&](const std::vector<std::string>& parts) -> const Json* {
        if (parts.size() < 2) {
            return nullptr;
        }
        const auto it = std::ranges::find(folders, parts.front(), &Folder::name);
        return it == folders.end() ? nullptr : &it->id;
    };
    const Json other(kOther);

    for (const auto& [rel, it] : listed) {
        if (!collection_resolve(base, std::format("{}/{}", cid, rel))) {
            continue; // el archivo del manifiesto ya no existe en disco
        }
        const Json* from_folder = folder_expr(path_parts(rel));
        const Json& expr = or_else(get(it, "expression"), from_folder ? *from_folder : other);
        const Json& title = get(it, "title");
        add(rel, expr, py_truthy(title) ? title : Json(stick_pretty_title(py_path_stem(rel))));
    }

    std::error_code ec;
    if (fs::is_directory(coll_dir, ec)) {
        for (const auto& parts :
             detail::sorted_files(coll_dir, [](std::string_view name) { return name != kManifest; })) {
            std::string rel;
            for (const auto& part : parts) {
                rel += (rel.empty() ? "" : "/") + part;
            }
            if (std::ranges::find(listed, rel, &std::pair<std::string, Json>::first) != listed.end()) {
                continue;
            }
            const Json* from_folder = folder_expr(parts);
            add(rel, from_folder ? *from_folder : other, Json(stick_pretty_title(py_path_stem(parts.back()))));
        }
    }
    return out;
}

Json count_kinds(const std::vector<Json>& cards) {
    Json kinds = Json::object();
    for (const std::string_view k : {"video", "image"}) {
        kinds[std::string(k)] =
            std::ranges::count_if(cards, [&](const Json& c) { return c.at("stick_kind") == Json(k); });
    }
    return kinds;
}

// stick_detail sobre el manifiesto ya leído.
Json detail_of(const Stick& s) {
    const Collected collected = collect(s.base, s.cid, s.man);
    const auto& cards = collected.cards;
    const auto& exprs = collected.exprs;
    const auto ordered = ids_of(exprs);

    const auto meta = [&](const Json& eid) {
        const auto i = key_index(ordered, eid);
        const Json& label = i ? get(exprs[*i], "label") : kNull;
        Json out = Json::object();
        out["id"] = eid;
        if (py_truthy(label)) {
            out["label"] = label;
        } else if (eid.is_string()) {
            std::string text = eid.get<std::string>();
            std::ranges::replace(text, '_', ' ');
            out["label"] = detail::py_title(text);
        } else { // eid.replace(…): AttributeError
            py_raise(ErrorCode::InvalidArgument,
                  std::format("stick.json: la expresión {} no es texto", detail::py_repr(eid)));
        }
        out["emoji"] = or_else(i ? get(exprs[*i], "emoji") : kNull, Json(""));
        return out;
    };
    const auto with_items = [&](const Json& eid, Json items) {
        Json g = meta(eid);
        g["items"] = std::move(items);
        return g;
    };

    // {*order_index, *(c["expression"] for c in cards)}: se queda la primera de las iguales.
    std::vector<Json> all = ordered;
    for (const Json& c : cards) {
        if (!key_index(all, c.at("expression"))) {
            all.push_back(c.at("expression"));
        }
    }
    all = sort_ids(all, ordered);

    const auto by_kind = [&](std::string_view kind) {
        std::vector<Json> keys;
        std::vector<Json> groups;
        for (const Json& c : cards) {
            if (c.at("stick_kind") != Json(kind)) {
                continue;
            }
            if (const auto i = key_index(keys, c.at("expression"))) {
                groups[*i].push_back(c);
            } else {
                keys.push_back(c.at("expression"));
                groups.push_back(Json::array({c}));
            }
        }
        Json out = Json::array();
        for (const Json& eid : sort_ids(keys, ordered)) {
            out.push_back(with_items(eid, groups[*key_index(keys, eid)]));
        }
        return out;
    };

    Json out = Json::object();
    out["id"] = s.cid;
    out["name"] = or_else(get(s.man, "name"), Json(s.cid));
    out["emoji"] = or_else(get(s.man, "emoji"), Json(""));
    out["chroma_color"] = chroma_color(s.man);
    out["kinds"] = count_kinds(cards);
    out["video"] = by_kind("video");
    out["image"] = by_kind("image");
    Json expressions = Json::array();
    Json groups = Json::array();
    for (const Json& eid : all) {
        expressions.push_back(meta(eid));
    }
    for (const Json& eid : all) {
        Json items = Json::array();
        for (const Json& c : cards) {
            if (py_equal(c.at("expression"), eid)) {
                items.push_back(c);
            }
        }
        groups.push_back(with_items(eid, std::move(items)));
    }
    out["expressions"] = std::move(expressions);
    out["groups"] = std::move(groups);
    return out;
}

Json detail_of(const std::optional<fs::path>& base, std::string_view cid) {
    return detail_of(root_and_manifest(base, cid));
}

Json list_of(const std::optional<fs::path>& base) {
    if (!base) {
        return Json{{"available", false}, {"sticks", Json::array()}};
    }
    struct Listed {
        std::int64_t order;
        std::string name_key;
        Json value;
    };
    std::vector<Listed> listed;
    for (const auto& cid : detail::sorted_subdirs(*base)) {
        const auto man = read_manifest(*base, cid);
        if (!man) {
            continue;
        }
        const auto cards = collect(*base, cid, *man).cards;
        const Json kinds = count_kinds(cards);
        const auto cover = std::ranges::find_if(cards, [](const Json& c) { return c.at("stick_kind") == "image"; });
        const Json& raw_order = get(*man, "order");
        const auto order = detail::py_int(raw_order.is_null() ? Json(kNoOrder) : raw_order);
        if (!order) {
            py_raise(ErrorCode::InvalidArgument,
                  std::format("stick.json de {}: «order» no es un entero ({})", cid, detail::py_repr(raw_order)));
        }
        Json s = Json::object();
        s["id"] = cid;
        s["name"] = or_else(get(*man, "name"), Json(cid));
        s["emoji"] = or_else(get(*man, "emoji"), Json(""));
        s["order"] = *order;
        s["chroma_color"] = chroma_color(*man);
        s["kinds"] = kinds;
        s["count"] = kinds.at("video").get<std::int64_t>() + kinds.at("image").get<std::int64_t>();
        s["cover_url"] = cover == cards.end() ? Json("") : cover->at("url");
        listed.push_back({*order, {}, std::move(s)});
    }
    // La clave (order, name.lower()) se calcula para todos antes de ordenar.
    for (auto& l : listed) {
        const Json& name = l.value.at("name");
        if (!name.is_string()) { // .lower(): AttributeError
            py_raise(ErrorCode::InvalidArgument, std::format("stick.json de {}: «name» no es texto",
                                                          l.value.at("id").get<std::string>()));
        }
        l.name_key = py_lower(name.get_ref<const std::string&>());
    }
    std::ranges::stable_sort(listed, [](const Listed& a, const Listed& b) {
        return std::tie(a.order, a.name_key) < std::tie(b.order, b.name_key);
    });
    Json sticks = Json::array();
    for (auto& l : listed) {
        sticks.push_back(std::move(l.value));
    }
    return Json{{"available", true}, {"sticks", std::move(sticks)}};
}

// _expression_folder: la subcarpeta que representa la expresión o una nueva con su etiqueta.
fs::path expression_folder(const fs::path& coll_dir, const Json& man, const std::string& expression) {
    const Json want(expression);
    const auto defs = expr_defs(man);
    for (const Folder& f : folder_expressions(coll_dir, defs)) {
        if (py_equal(f.id, want)) {
            return coll_dir / path_from_utf8(f.name);
        }
    }
    const auto def = std::ranges::find_if(defs, [&](const Json& e) { return py_equal(get(e, "id"), want); });
    const Json& label = or_else(def == defs.end() ? kNull : get(*def, "label"), want);
    fs::path target = coll_dir / path_from_utf8(stick_slug(label, expression));
    std::error_code ec;
    fs::create_directory(target, ec); // mkdir(exist_ok=True): falla si es un archivo
    if (ec) {
        py_raise(ErrorCode::Io, std::format("no se pudo crear la carpeta {}: {}", path_to_utf8(target), ec.message()));
    }
    return target;
}

void require_expression(const std::vector<Json>& exprs, const std::string& expression) {
    if (!key_index(ids_of(exprs), Json(expression))) {
        py_raise(ErrorCode::InvalidArgument, "Expresión desconocida: " + expression);
    }
}

} // namespace

// --- Nombres -------------------------------------------------------------------------------

std::string stick_norm(const Json& text) {
    std::string s = detail::py_nfkd_fold(str_or_empty(text));
    std::erase(s, '_');
    return s;
}

std::string stick_slug(const Json& text, std::string_view fallback) {
    std::string s;
    for (const char c : detail::py_nfkd_fold(str_or_empty(text))) { // cada racha de «_» en una
        if (c != '_' || s.empty() || s.back() != '_') {
            s += c;
        }
    }
    const auto strip = [](std::string& t) {
        const auto first = t.find_first_not_of('_');
        t = first == std::string::npos ? std::string() : t.substr(first, t.find_last_not_of('_') - first + 1);
    };
    strip(s);
    s.resize(std::min<std::size_t>(s.size(), 40)); // ASCII: 40 bytes son 40 caracteres
    strip(s);
    return s.empty() ? std::string(fallback) : s;
}

std::string stick_pretty_title(std::string_view stem) {
    // _NOISE.sub("", stem): r"_(?:\d{3,4}p|\d{8,})(?=_|$)", con \d de Unicode y el $ que vale
    // también antes de un «\n» final.
    std::vector<std::pair<char32_t, std::string_view>> chars;
    for (std::size_t i = 0; i < stem.size();) {
        const std::size_t begin = i;
        const char32_t c = detail::utf8_next(stem, i);
        chars.emplace_back(c, stem.substr(begin, i - begin));
    }
    const std::size_t n = chars.size();
    const auto boundary = [&](std::size_t k) {
        return k == n || chars[k].first == U'_' || (k + 1 == n && chars[k].first == U'\n');
    };
    std::string s;
    for (std::size_t i = 0; i < n;) {
        if (chars[i].first == U'_') {
            std::size_t j = i + 1;
            while (j < n && detail::py_isdecimal(chars[j].first)) {
                ++j;
            }
            const std::size_t digits = j - i - 1;
            std::size_t end = 0;
            if ((digits == 3 || digits == 4) && j < n && chars[j].first == U'p' && boundary(j + 1)) {
                end = j + 1;
            } else if (digits >= 8 && boundary(j)) {
                end = j;
            }
            if (end != 0) {
                i = end;
                continue;
            }
        }
        s += chars[i].second;
        ++i;
    }
    std::ranges::replace(s, '_', ' ');
    s = py_strip(s);
    return s.empty() ? std::string(stem) : s;
}

// --- API -----------------------------------------------------------------------------------

Result<Json> list_sticks(const std::optional<fs::path>& base) {
    return guarded([&] { return list_of(base); });
}

Result<Json> stick_detail(const std::optional<fs::path>& base, std::string_view cid) {
    return guarded([&] { return detail_of(base, cid); });
}

Result<Json> create_stick(const std::optional<fs::path>& base, const Json& name, const Json& emoji,
                          const Json& chroma_color) {
    return guarded([&] {
        if (!base) {
            py_raise(ErrorCode::InvalidArgument, "No hay carpeta raíz de material. Elige una en la Biblioteca.");
        }
        const std::string clean = py_strip(str_or_empty(name));
        if (clean.empty()) {
            py_raise(ErrorCode::InvalidArgument, "Ponle un nombre al stick.");
        }
        std::string color = py_strip(str_or_empty(chroma_color));
        const bool hex = color.size() == 7 && color[0] == '#' &&
                         std::all_of(color.begin() + 1, color.end(), [](char c) {
                             return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
                         });
        if (!hex) {
            color = kDefaultChroma;
        }

        const std::string stem = stick_slug(Json(clean));
        std::string cid = stem;
        std::error_code ec;
        for (int n = 2; fs::exists(*base / path_from_utf8(cid), ec); ++n) {
            cid = std::format("{}_{}", stem, n);
        }
        const fs::path folder = *base / path_from_utf8(cid);
        if (!fs::create_directories(folder, ec) || ec) {
            py_raise(ErrorCode::Io, std::format("no se pudo crear la carpeta {}: {}", cid, ec.message()));
        }
        for (const auto& e : kDefaultExpressions) {
            fs::create_directory(folder / path_from_utf8(stick_slug(Json(e.label), e.id)), ec);
            if (ec) {
                py_raise(ErrorCode::Io, std::format("no se pudo crear la carpeta de {}: {}", e.label, ec.message()));
            }
        }

        std::optional<std::int64_t> top;
        for (const Json& s : list_of(base).at("sticks")) {
            if (const auto order = s.at("order").get<std::int64_t>(); order < kNoOrder) {
                top = std::max(top.value_or(order), order);
            }
        }
        Json expressions = Json::array();
        for (const auto& e : kDefaultExpressions) {
            expressions.push_back(Json{{"id", e.id}, {"label", e.label}, {"emoji", e.emoji}});
        }
        Json man = Json::object();
        man["schema"] = "stick/1";
        man["name"] = clean;
        man["emoji"] = detail::py_slice(py_strip(str_or_empty(emoji)), 4);
        man["order"] = top ? *top + 1 : 0;
        man["chroma"] = Json{{"color", color}};
        man["expressions"] = std::move(expressions);
        man["items"] = Json::array();
        write_manifest(*base, cid, man);
        return detail_of(base, cid);
    });
}

Result<Json> add_stick_files(const std::optional<fs::path>& base, std::string_view cid,
                             const std::vector<IncomingFile>& files, const Json& expression) {
    return guarded([&] {
        const Stick s = root_and_manifest(base, cid);
        const fs::path coll_dir = s.base / path_from_utf8(s.cid);
        const auto exprs = collect(s.base, s.cid, s.man).exprs;
        const std::string expr = py_truthy(expression) ? py_str(expression) : std::string(kOther);
        require_expression(exprs, expr);
        const fs::path folder = expression_folder(coll_dir, s.man, expr);
        const std::string folder_name = path_to_utf8(folder.filename());

        Json saved = Json::array();
        Json errors = Json::array();
        for (const IncomingFile& f : files) {
            // _safe_filename: Path(str(raw or "")).name saneado, o «archivo».
            std::string name = detail::clean_name(py_path_name(str_or_empty(f.name)));
            if (name.empty()) {
                name = "archivo";
            }
            const auto kind = collection_kind(name);
            if (!kind || !stick_kind(*kind)) {
                errors.push_back(Json{{"file", f.name},
                                      {"error", "Solo vídeos o imágenes (mp4, mov, webm, png, jpg, webp, gif…)."}});
                continue;
            }
            const std::string stem = py_path_stem(name);
            const std::string suffix = py_path_suffix(name);
            std::string target = name;
            std::error_code ec;
            for (int n = 2; fs::exists(folder / path_from_utf8(target), ec); ++n) {
                target = std::format("{}_{}{}", stem, n, suffix);
            }
            if (!f.write) {
                py_raise(ErrorCode::InvalidArgument, "falta el contenido de " + target);
            }
            if (auto written = f.write(folder / path_from_utf8(target)); !written) {
                throw PyFailure(written.error());
            }
            saved.push_back(std::format("{}/{}", folder_name, target));
        }
        Json out = Json::object();
        out["saved"] = std::move(saved);
        out["errors"] = std::move(errors);
        out["detail"] = detail_of(base, s.cid);
        return out;
    });
}

Result<Json> set_stick_expression(const std::optional<fs::path>& base, std::string_view cid, const Json& rel_raw,
                                  const Json& expression) {
    return guarded([&] {
        Stick s = root_and_manifest(base, cid);
        std::string rel = slashes(str_or_empty(rel_raw));
        rel.erase(0, std::min(rel.find_first_not_of('/'), rel.size())); // .lstrip("/")
        const auto path = collection_resolve(s.base, s.cid + "/" + rel);
        if (rel.empty() || !path || !detail::resolved_inside(s.base / path_from_utf8(s.cid), *path, true)) {
            py_raise(ErrorCode::NotFound, "Recurso no encontrado en el stick.");
        }
        const auto ck = collection_kind(path_to_utf8(path->filename()));
        const auto kind = ck ? stick_kind(*ck) : std::nullopt;
        if (!kind) {
            py_raise(ErrorCode::InvalidArgument, "Solo se categorizan vídeos o imágenes.");
        }
        const Collected collected = collect(s.base, s.cid, s.man);
        const auto& cards = collected.cards;
        const auto& exprs = collected.exprs;
        const std::string expr = str_or_empty(expression);
        require_expression(exprs, expr);

        Json items = Json::array();
        for (Json& it : iter_or_empty(get(s.man, "items"), "items")) {
            if (it.is_object()) {
                items.push_back(std::move(it));
            }
        }
        const auto cur = std::ranges::find_if(items, [&](const Json& it) {
            return slashes(str_or_empty(get(it, "file"))) == rel;
        });
        if (cur == items.end()) {
            const auto card = std::ranges::find_if(cards, [&](const Json& c) { return c.at("rel") == rel; });
            const Json title = card != cards.end() ? card->at("title") : Json(stick_pretty_title(py_path_stem(rel)));
            items.push_back(Json{{"file", rel}, {"kind", std::string(*kind)}, {"expression", expr}, {"title", title}});
        } else {
            (*cur)["expression"] = expr;
        }
        s.man["items"] = std::move(items);
        // Una categoría propia pasa a la taxonomía: el manifiesto la recuerda aunque se vacíe.
        auto defs = expr_defs(s.man);
        if (!key_index(ids_of(defs), Json(expr))) {
            defs.push_back(*std::ranges::find_if(exprs, [&](const Json& e) { return py_equal(e.at("id"), Json(expr)); }));
            s.man["expressions"] = defs;
        }
        write_manifest(s.base, s.cid, s.man);
        return detail_of(base, s.cid);
    });
}

} // namespace gc::core
