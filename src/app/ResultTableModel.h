// 結果テーブルのモデル (計画 7)。
//
// 差分更新は実装しない。Everything の結果は入力のたびに丸ごと別物になるうえ、
// 5,000 行のリセットは 1ms 未満で終わるため、差分に意味が無い。
#pragma once

#include "core/SearchTypes.h"

#include <QAbstractTableModel>
#include <QVector>

namespace efs {

class ResultTableModel : public QAbstractTableModel {
    Q_OBJECT

public:
    enum Column { ColumnName = 0, ColumnPath, ColumnSize, ColumnDateModified, ColumnCount };

    explicit ResultTableModel(QObject* parent = nullptr);

    void setRows(QVector<ResultRow> rows);

    [[nodiscard]] int rowCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] int columnCount(const QModelIndex& parent = {}) const override;
    [[nodiscard]] QVariant data(const QModelIndex& index, int role) const override;
    [[nodiscard]] QVariant headerData(int section, Qt::Orientation orientation,
                                      int role = Qt::DisplayRole) const override;

private:
    QVector<ResultRow> m_rows;
};

} // namespace efs
