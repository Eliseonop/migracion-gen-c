// Operaciones compuestas: pegar atributos (espejo de backend/app/timeline_ops.py).

#include <algorithm>

#include "edit_ops.hpp"
#include "edit_util.hpp"
#include "gc/core/clip_attrs.hpp"
#include "gc/core/serialize.hpp"

namespace gc::core::detail {

namespace {

// repr() de una lista de grupos (ids sencillos): ['crop', 'speed']
std::string repr_list(const std::vector<std::string>& items) {
    std::string out = "[";
    for (std::size_t i = 0; i < items.size(); ++i) {
        out += (i == 0 ? "'" : ", '") + items[i] + "'";
    }
    return out + "]";
}

// `[str(x) for x in v]`; nullopt si `v` no se puede recorrer.
std::optional<std::vector<std::string>> str_list(const Json& v) {
    const auto items = py_iter(v);
    if (!items) {
        return std::nullopt;
    }
    std::vector<std::string> out;
    for (const Json& x : *items) {
        out.push_back(py_str(x));
    }
    return out;
}

} // namespace

Result<EditResult> run(const Timeline& tl, const ops::PasteClipAttributes& op, IdSource& ids) {
    std::vector<std::string> wanted;
    if (op.groups.is_null()) {
        wanted.assign(kAttrGroupIds.begin(), kAttrGroupIds.end());
    } else {
        auto list = str_list(op.groups);
        if (!list) {
            return invalid("groups debe ser una lista");
        }
        wanted = std::move(*list);
    }
    std::vector<std::string> bad;
    for (const auto& g : wanted) {
        if (std::ranges::find(kAttrGroupIds, g) == kAttrGroupIds.end()) {
            bad.push_back(g);
        }
    }
    if (!bad.empty()) {
        return invalid("groups: grupos no válidos " + repr_list(bad) + " (usa " +
                       repr_list({kAttrGroupIds.begin(), kAttrGroupIds.end()}) + ")");
    }
    if (wanted.empty()) {
        return invalid("groups vacío: indica al menos un grupo");
    }

    std::vector<std::string> targets; // sin repetir, en orden (dict.fromkeys)
    if (py_truthy(op.target_clip_ids)) {
        auto list = str_list(op.target_clip_ids);
        if (!list) {
            return invalid("target_clip_ids debe ser una lista");
        }
        for (auto& t : *list) {
            if (t != op.source_clip_id && std::ranges::find(targets, t) == targets.end()) {
                targets.push_back(std::move(t));
            }
        }
    }
    if (targets.empty()) {
        return invalid("target_clip_ids: indica al menos un clip destino distinto del origen");
    }

    Timeline out = tl;
    GC_TRY(si, find_clip(out, op.source_clip_id));
    const TimelineClip src = *out.clips[si];
    std::vector<std::string> changed;
    std::vector<std::string> warnings;
    for (const auto& tid : targets) {
        GC_TRY(i, find_clip(out, tid));
        const TimelineClip cur = *out.clips[i];
        std::vector<std::string> ok;
        std::vector<std::string> skipped;
        for (const auto& g : wanted) {
            (group_applies(g, src, cur) ? ok : skipped).push_back(g);
        }
        if (!skipped.empty() && !op.groups.is_null()) {
            warnings.push_back(tid + " (" + cur.kind + "): no admite " + repr_list(skipped));
        }
        if (ok.empty()) {
            continue;
        }
        GC_TRY(pasted, paste_attributes(cur, src, ok, ids));
        GC_TRY(clip, clip_from_json(pasted));
        out.clips[i] = std::move(clip);
        changed.push_back(tid);
    }
    if (changed.empty()) {
        return invalid("ningún clip destino admite esos grupos");
    }
    return EditResult{std::move(out), std::move(changed), std::move(warnings)};
}

} // namespace gc::core::detail
