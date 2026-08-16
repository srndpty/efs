#include "app/ResultTableModel.h"

#include "core/Formatting.h"

namespace efs {

ResultTableModel::ResultTableModel(QObject* parent) : QAbstractTableModel(parent) {}

void ResultTableModel::setRows(QVector<ResultRow> rows)
{
    beginResetModel();
    m_rows = std::move(rows);
    endResetModel();
}

int ResultTableModel::rowCount(const QModelIndex& parent) const
{
    // テーブルモデルなので、子を持つのはルートだけ。
    if (parent.isValid())
        return 0;
    return static_cast<int>(m_rows.size());
}

int ResultTableModel::columnCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    return ColumnCount;
}

QVariant ResultTableModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.parent().isValid())
        return {};
    if (index.row() < 0 || index.row() >= m_rows.size())
        return {};
    if (index.column() < 0 || index.column() >= ColumnCount)
        return {};

    const ResultRow& row = m_rows.at(index.row());

    if (role == Qt::TextAlignmentRole) {
        if (index.column() == ColumnSize)
            return QVariant::fromValue(Qt::Alignment(Qt::AlignRight | Qt::AlignVCenter));
        return {};
    }

    if (role != Qt::DisplayRole)
        return {};

    switch (index.column()) {
    case ColumnName:
        return row.name;
    case ColumnPath:
        return row.path;
    case ColumnSize:
        return formatSize(row.size);
    case ColumnDateModified:
        return formatModified(row.modified);
    default:
        return {};
    }
}

QVariant ResultTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (role != Qt::DisplayRole || orientation != Qt::Horizontal)
        return {};

    switch (section) {
    case ColumnName:
        return QStringLiteral("Name");
    case ColumnPath:
        return QStringLiteral("Path");
    case ColumnSize:
        return QStringLiteral("Size");
    case ColumnDateModified:
        return QStringLiteral("Date Modified");
    default:
        return {};
    }
}

} // namespace efs
