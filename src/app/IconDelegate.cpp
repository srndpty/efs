#include "app/IconDelegate.h"

#include "app/IconCache.h"
#include "app/ResultTableModel.h"
#include "app/ShellIcon.h"

#include <QImage>
#include <QStyleOptionViewItem>

namespace efs {

namespace {

QPixmap toPixmap(const QImage& image)
{
    return image.isNull() ? QPixmap() : QPixmap::fromImage(image);
}

} // namespace

IconDelegate::IconDelegate(IconCache* cache, QObject* parent)
    : QStyledItemDelegate(parent), m_cache(cache)
{
    // 汎用アイコンは 2 回だけ同期で引く。行数に比例しないので UI をブロックしない。
    m_genericFile = toPixmap(shellIconImage({.isDirectory = false, .extension = {}}));
    m_genericDirectory = toPixmap(shellIconImage({.isDirectory = true, .extension = {}}));
}

void IconDelegate::invalidate()
{
    m_pixmaps.clear();
}

QPixmap IconDelegate::pixmapFor(const QString& key) const
{
    const auto cached = m_pixmaps.constFind(key);
    if (cached != m_pixmaps.constEnd())
        return cached.value();

    QImage image;
    if (!m_cache || !m_cache->image(key, &image)) {
        // cache miss。要求は IconCache が worker へ出している (重複しない)。
        // 今回は汎用アイコンで場所だけ確保する — ここで実ファイルを見に行ったり
        // 完了を待ったりは絶対にしない。**変換結果はキャッシュしない**
        // (本物が来たときに差し替わらなくなる)。
        return parseIconKey(key).isDirectory ? m_genericDirectory : m_genericFile;
    }

    // lookup 失敗 (null 画像) も cache hit。汎用アイコンで確定させる。
    QPixmap pixmap = toPixmap(image);
    if (pixmap.isNull())
        pixmap = parseIconKey(key).isDirectory ? m_genericDirectory : m_genericFile;
    m_pixmaps.insert(key, pixmap);
    return pixmap;
}

void IconDelegate::initStyleOption(QStyleOptionViewItem* option, const QModelIndex& index) const
{
    QStyledItemDelegate::initStyleOption(option, index);

    if (index.column() != ResultTableModel::ColumnName)
        return;

    const QString key = index.data(ResultTableModel::IconKeyRole).toString();
    if (key.isEmpty())
        return;

    const QPixmap pixmap = pixmapFor(key);
    if (pixmap.isNull())
        return;

    option->icon = QIcon(pixmap);
    option->features |= QStyleOptionViewItem::HasDecoration;
}

} // namespace efs
