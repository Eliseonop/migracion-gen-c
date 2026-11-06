#include "home_model.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <unordered_map>

#include "gc/core/io.hpp"

#include "thumbnails.hpp"

namespace gc::studio {

namespace {

std::string utf8(const QString& s) {
    return s.trimmed().toStdString();
}

QString qstr(const std::string& s) {
    return QString::fromStdString(s);
}

std::optional<std::string_view> group_arg(const std::string& gid) {
    return gid.empty() ? std::nullopt : std::optional<std::string_view>(gid);
}

} // namespace

HomeModel::HomeModel(core::ProjectsStore& store, core::IdSource& ids, Thumbnails* thumbnails, QObject* parent)
    : QObject(parent), store_(store), ids_(ids), thumbnails_(thumbnails) {
    if (thumbnails_ != nullptr) {
        connect(thumbnails_, &Thumbnails::changed, this, &HomeModel::update_thumbs);
    }
    reload();
}

void HomeModel::update_thumbs() {
    QVariantMap next;
    if (thumbnails_ != nullptr) {
        for (const auto& [pid, source] : sources_) {
            if (const auto file = thumbnails_->find(source); !file.empty()) {
                next[pid] = QString::fromStdString(core::path_to_utf8(file));
            }
        }
    }
    if (next != thumbs_) {
        thumbs_ = std::move(next);
        emit thumbsChanged();
    }
}

template <class T>
bool HomeModel::ok(const Result<T>& r) {
    const QString message = r ? QString() : qstr(r.error().message);
    if (message != error_) {
        error_ = message;
        emit errorChanged();
    }
    return r.has_value();
}

QString HomeModel::groupOf(const core::Project& p) const {
    const QString gid = p.group_id ? qstr(*p.group_id) : QString();
    const bool known =
        std::ranges::any_of(groups_, [&](const QVariant& g) { return g.toMap()["id"] == gid; });
    return known ? gid : QString();
}

const core::Project* HomeModel::find(const QString& pid) const {
    const auto it = std::ranges::find(all_, pid.toStdString(), &core::Project::id);
    return it == all_.end() ? nullptr : &*it;
}

QString HomeModel::groupName() const {
    for (const QVariant& g : groups_) {
        if (g.toMap()["id"] == group_id_) {
            return g.toMap()["name"].toString();
        }
    }
    return {};
}

void HomeModel::reload() {
    auto projects = store_.list_projects();
    auto groups = store_.list_groups();
    if (!ok(projects) || !ok(groups)) {
        return;
    }
    all_ = std::move(*projects);
    groups_.clear();
    for (const core::ProjectGroup& g : *groups) {
        groups_.push_back(QVariantMap{{"id", qstr(g.id)}, {"name", qstr(g.name)}, {"count", 0}});
    }
    std::unordered_map<QString, int> counts;
    for (const core::Project& p : all_) {
        ++counts[groupOf(p)];
    }
    for (QVariant& g : groups_) {
        QVariantMap m = g.toMap();
        m["count"] = counts[m["id"].toString()];
        g = m;
    }
    root_count_ = counts[QString()];
    if (!group_id_.isEmpty() && groupName().isEmpty()) {
        group_id_.clear();
    }
    visible_.clear();
    sources_.clear();
    std::vector<std::filesystem::path> wanted;
    for (const core::Project& p : all_) {
        if (groupOf(p) != group_id_) {
            continue;
        }
        visible_.push_back(QVariantMap{{"id", qstr(p.id)},
                                       {"name", qstr(p.name)},
                                       {"clips", static_cast<int>(p.clips.size())},
                                       {"date", qstr(p.created_at.substr(0, 10))}});
        // La miniatura es el primer fotograma del primer clip: el `<video src={p.clips[0].url}>` de
        // Home.jsx, cuya URL (/api/media/<pid>/video/<archivo>) sirve storage.resolve_media.
        if (!p.clips.empty()) {
            if (auto path = core::resolve_project_media(store_.clips_dir(), p, "video", p.clips.front().filename)) {
                sources_.emplace_back(qstr(p.id), *path);
                wanted.push_back(std::move(*path));
            }
        }
    }
    if (thumbnails_ != nullptr) {
        thumbnails_->request(std::move(wanted));
    }
    update_thumbs();
    emit changed();
}

void HomeModel::openGroup(const QString& gid) {
    group_id_ = gid;
    reload();
}

QString HomeModel::createProject(const QString& name, const QString& gid) {
    const std::string n = utf8(name);
    if (n.empty()) {
        return {};
    }
    const std::string g = gid.toStdString();
    const auto p = store_.create_project(n, group_arg(g), ids_);
    if (!ok(p)) {
        return {};
    }
    group_id_ = gid;
    reload();
    return qstr(p->id);
}

void HomeModel::renameProject(const QString& pid, const QString& name) {
    const core::Project* p = find(pid);
    const std::string n = utf8(name);
    if (p && !n.empty() && n != p->name && ok(store_.rename_project(p->id, n))) {
        reload();
    }
}

void HomeModel::duplicateProject(const QString& pid) {
    if (ok(store_.duplicate_project(pid.toStdString(), ids_))) {
        reload();
    }
}

void HomeModel::deleteProject(const QString& pid) {
    if (ok(store_.delete_project(pid.toStdString()))) {
        reload();
    }
}

void HomeModel::moveProject(const QString& pid, const QString& gid) {
    const core::Project* p = find(pid);
    const std::string g = gid.toStdString();
    if (p && groupOf(*p) != gid && ok(store_.move_project(p->id, group_arg(g)))) {
        reload();
    }
}

void HomeModel::createGroup(const QString& name) {
    const std::string n = utf8(name);
    if (!n.empty() && ok(store_.create_group(n, ids_))) {
        reload();
    }
}

void HomeModel::renameGroup(const QString& gid, const QString& name) {
    const std::string n = utf8(name);
    const bool same = std::ranges::any_of(groups_, [&](const QVariant& g) {
        return g.toMap()["id"] == gid && g.toMap()["name"].toString().toStdString() == n;
    });
    if (!n.empty() && !same && ok(store_.rename_group(gid.toStdString(), n))) {
        reload();
    }
}

void HomeModel::deleteGroup(const QString& gid) {
    if (ok(store_.delete_group(gid.toStdString()))) {
        reload();
    }
}

} // namespace gc::studio
