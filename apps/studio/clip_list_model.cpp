#include "clip_list_model.hpp"

#include <algorithm>

namespace gc::studio {

namespace {

const QList<QByteArray> kRoles{"clipId",   "trackId", "kind",       "label", "speed",   "start",
                               "duration", "clipY",   "clipHeight", "muted", "disabled"};

} // namespace

int ClipListModel::rowCount(const QModelIndex& parent) const {
    return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

QVariant ClipListModel::data(const QModelIndex& index, int role) const {
    const int r = role - Qt::UserRole;
    if (!index.isValid() || index.row() >= rows_.size() || r < 0 || r >= kRoles.size()) {
        return {};
    }
    return rows_[index.row()].value(QString::fromLatin1(kRoles[r]));
}

QHash<int, QByteArray> ClipListModel::roleNames() const {
    QHash<int, QByteArray> out;
    for (int i = 0; i < kRoles.size(); ++i) {
        out.insert(Qt::UserRole + i, kRoles[i]);
    }
    return out;
}

void ClipListModel::sync(QVector<QVariantMap> rows) {
    const bool same_ids =
        rows.size() == rows_.size() && std::ranges::equal(
                                           rows, rows_, {}, [](const QVariantMap& m) { return m["clipId"]; },
                                           [](const QVariantMap& m) { return m["clipId"]; });
    if (!same_ids) {
        beginResetModel();
        rows_ = std::move(rows);
        endResetModel();
        return;
    }
    for (int i = 0; i < rows.size(); ++i) {
        if (rows[i] != rows_[i]) {
            rows_[i] = std::move(rows[i]);
            emit dataChanged(index(i), index(i));
        }
    }
}

} // namespace gc::studio
