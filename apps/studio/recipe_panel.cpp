// Recetas en un clic (EdRecipes.jsx y applyRecipeUI de VideoEditor.jsx de video-yt): el catálogo con si cada
// receta se puede aplicar con lo seleccionado (gc/core/recipes.hpp) y aplicarla. En video-yt la aplica el
// backend sobre la timeline guardada; aquí es la misma operación (apply_recipe) sobre la sesión, en un paso de
// deshacer y sin esperar: no hay «Aplicando…».

#include <algorithm>
#include <string>
#include <vector>

#include "editor_model.hpp"
#include "gc/core/bg.hpp"
#include "gc/core/recipes.hpp"

namespace gc::studio {

namespace {

QString qstr(std::string_view s) { return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size())); }

// (auto.status || 'idle') === 'idle' y auto?.enabled de applyRecipeUI: «Eliminar fondo» encendido y sin calcular.
bool bg_auto_pending(const core::TimelineClip& c) {
    if (!c.bg_removal || !c.bg_removal->is_object()) {
        return false;
    }
    const auto it = c.bg_removal->find("auto");
    if (it == c.bg_removal->end() || !it->is_object()) {
        return false;
    }
    const Json& enabled = it->value("enabled", Json());
    if (!(enabled.is_boolean() && enabled.get<bool>())) {
        return false;
    }
    const Json& status = it->value("status", Json());
    const bool blank = status.is_string() && status.get_ref<const std::string&>().empty();
    return status.is_null() || blank || status == false || status == 0 || status == "idle";
}

} // namespace

QVariantList EditorModel::recipes() const {
    std::vector<std::string> kinds; // los `kind` de selectedClips
    for (const auto& id : selection_.ids) {
        if (const core::TimelineClip* c = session_ ? clip(qstr(id)) : nullptr) {
            kinds.push_back(c->kind);
        }
    }
    QVariantList out;
    for (const core::RecipeInfo& r : core::recipe_catalog()) {
        const core::RecipeReadiness ready = core::recipe_ready(r.needs, kinds);
        out.push_back(QVariantMap{{"id", qstr(r.id)}, {"label", qstr(r.label)}, {"trick", r.trick},
                                  {"desc", qstr(r.desc)}, {"ok", ready.ok}, {"why", qstr(ready.why)}});
    }
    return out;
}

void EditorModel::applyRecipe(const QString& id) {
    const core::RecipeInfo* recipe = core::find_recipe(id.toStdString());
    if (!session_ || recipe == nullptr) {
        return;
    }
    Json clip_ids = Json::array(); // selIdsRef.current
    for (const auto& s : selection_.ids) {
        clip_ids.push_back(s);
    }
    const QString label = qstr(recipe->label);
    const auto change = session_->apply(core::ops::ApplyRecipe{Json(std::string(recipe->id)), clip_ids,
                                                               Json{{"at_time", playhead_}}});
    if (!change) {
        set_notice("error", change.error().message.empty() ? QString("No se pudo aplicar la receta.")
                                                           : qstr(change.error().message));
        return;
    }
    // Lo tocado, en el orden de la timeline.
    std::vector<std::string> touched;
    for (const auto& c : session_->timeline()->clips) {
        if (std::ranges::find(change->changed, c->id) != change->changed.end()) {
            touched.push_back(c->id);
        }
    }
    for (const auto& t : touched) {
        const core::TimelineClip* c = clip(qstr(t));
        if (c != nullptr && core::bg_capable(*c) && bg_auto_pending(*c)) {
            start_bg_auto(*c);
        }
    }
    const auto sel = std::ranges::find_if(touched, [this](const std::string& t) {
        const core::TimelineClip* c = clip(qstr(t));
        return c != nullptr && c->kind != "audio";
    });
    if (!touched.empty()) {
        const std::string pick = sel != touched.end() ? *sel : touched.front();
        selected_track_.clear();
        selection_ = core::ClipSelection{{pick}, pick};
        rebuild();
        emit selectionChanged();
    }
    QStringList warn;
    for (const auto& w : change->warnings) {
        if (!w.contains("recorte IA")) {
            warn << qstr(w);
        }
    }
    set_notice(warn.isEmpty() ? "success" : "info",
               QString("«%1» aplicada").arg(label) + (warn.isEmpty() ? QString() : " · " + warn.join(" · ")));
}

} // namespace gc::studio
