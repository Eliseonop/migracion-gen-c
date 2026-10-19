#pragma once

// Los clips de la timeline para el Repeater de QML. Un cambio que no altera qué clips hay ni su
// orden solo avisa de las filas que cambian: así QML no recrea los delegados (ni el que se está
// arrastrando) y una timeline con cientos de subtítulos no se repinta entera.

#include <QAbstractListModel>
#include <QVariantMap>
#include <QVector>

namespace gc::studio {

class ClipListModel : public QAbstractListModel {
    Q_OBJECT

public:
    using QAbstractListModel::QAbstractListModel;

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QHash<int, QByteArray> roleNames() const override;

    // Filas nuevas: {clipId, trackId, kind, label, speed, start, duration, clipY, clipHeight, muted,
    // disabled, locked, kfs}.
    void sync(QVector<QVariantMap> rows);
    [[nodiscard]] const QVector<QVariantMap>& rows() const { return rows_; }

private:
    QVector<QVariantMap> rows_;
};

} // namespace gc::studio
