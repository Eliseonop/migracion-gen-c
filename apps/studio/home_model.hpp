#pragma once

// ViewModel del inicio (Home.jsx): carpetas a la izquierda y, a la derecha, los proyectos de
// la carpeta abierta. En la raíz se ven las carpetas y los proyectos sueltos; un group_id que
// no existe cuenta como sin carpeta.

#include <QObject>
#include <QString>
#include <QVariantList>

#include "gc/core/ids.hpp"
#include "gc/core/projects_store.hpp"

namespace gc::studio {

class HomeModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList groups READ groups NOTIFY changed)
    Q_PROPERTY(QVariantList projects READ projects NOTIFY changed)
    Q_PROPERTY(QString groupId READ groupId NOTIFY changed)
    Q_PROPERTY(QString groupName READ groupName NOTIFY changed)
    Q_PROPERTY(int rootCount READ rootCount NOTIFY changed)
    Q_PROPERTY(bool empty READ empty NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY errorChanged)

public:
    HomeModel(core::ProjectsStore& store, core::IdSource& ids, QObject* parent = nullptr);

    [[nodiscard]] QVariantList groups() const { return groups_; }
    [[nodiscard]] QVariantList projects() const { return visible_; }
    [[nodiscard]] QString groupId() const { return group_id_; }
    [[nodiscard]] QString groupName() const;
    [[nodiscard]] int rootCount() const { return root_count_; }
    [[nodiscard]] bool empty() const { return all_.empty() && groups_.empty(); }
    [[nodiscard]] QString error() const { return error_; }

    Q_INVOKABLE void reload();
    Q_INVOKABLE void openGroup(const QString& gid);
    Q_INVOKABLE QString createProject(const QString& name, const QString& gid);
    Q_INVOKABLE void renameProject(const QString& pid, const QString& name);
    Q_INVOKABLE void duplicateProject(const QString& pid);
    Q_INVOKABLE void deleteProject(const QString& pid);
    Q_INVOKABLE void moveProject(const QString& pid, const QString& gid);
    Q_INVOKABLE void createGroup(const QString& name);
    Q_INVOKABLE void renameGroup(const QString& gid, const QString& name);
    Q_INVOKABLE void deleteGroup(const QString& gid);

signals:
    void changed();
    void errorChanged();

private:
    template <class T>
    bool ok(const Result<T>& r);
    [[nodiscard]] QString groupOf(const core::Project& p) const;
    [[nodiscard]] const core::Project* find(const QString& pid) const;

    core::ProjectsStore& store_;
    core::IdSource& ids_;
    std::vector<core::Project> all_;
    QVariantList groups_;
    QVariantList visible_;
    QString group_id_;
    int root_count_ = 0;
    QString error_;
};

} // namespace gc::studio
