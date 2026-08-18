#include "app/IconDelegate.h"

#include "app/IconCache.h"
#include "app/ResultTableModel.h"
#include "app/ShellIcon.h"

#include <QApplication>
#include <QImage>
#include <QStyle>
#include <QStyleOptionViewItem>

namespace efs {

namespace {

QPixmap toPixmap(const QImage& image)
{
    return image.isNull() ? QPixmap() : QPixmap::fromImage(image);
}

// Qt 標準の item 描画 (QCommonStyle::viewItemDrawText) は QTextLayout に
// テキストを流してから行単位で省略する。折り返しが**単語境界**で起きるため、
// 空白を含む長いパスは "C:\Program ..." のように単語ごと落ちてしまう。
// そこで描画前に文字単位で省略済みの文字列を作り、Qt 側の省略は無効化する。
void elideTextPerCharacter(QStyleOptionViewItem* option)
{
    if (option->text.isEmpty() || option->textElideMode == Qt::ElideNone)
        return;
    // sizeHint 経由では rect が未確定。ここで省略すると列幅計算が壊れる。
    if (!option->rect.isValid())
        return;

    const QWidget* widget = option->widget;
    QStyle* style = widget ? widget->style() : QApplication::style();
    const QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, option, widget);
    // viewItemDrawText と同じ左右マージン。
    const int margin = style->pixelMetric(QStyle::PM_FocusFrameHMargin, nullptr, widget) + 1;
    const int width = textRect.width() - (2 * margin);
    if (width <= 0)
        return;

    const QString elided =
        option->fontMetrics.elidedText(option->text, option->textElideMode, width);
    if (elided == option->text)
        return;

    option->text = elided;
    // 既に収まっているので、Qt に再度省略させない (二重の "..." を防ぐ)。
    option->textElideMode = Qt::ElideNone;
}

} // namespace

IconDelegate::IconDelegate(IconCache* cache, QObject* parent)
    : QStyledItemDelegate(parent), m_cache(cache)
{
    // 汎用アイコンは 2 回だけ同期で引く。行数に比例しないので UI をブロックしない。
    m_genericFile = toPixmap(shellIconImage({.isDirectory = false, .extension = {}}));
    m_genericDirectory = toPixmap(shellIconImage({.isDirectory = true, .extension = {}}));
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

    if (index.column() == ResultTableModel::ColumnName) {
        const QString key = index.data(ResultTableModel::IconKeyRole).toString();
        if (!key.isEmpty()) {
            const QPixmap pixmap = pixmapFor(key);
            if (!pixmap.isNull()) {
                option->icon = QIcon(pixmap);
                option->features |= QStyleOptionViewItem::HasDecoration;
            }
        }
    }

    // アイコンを入れた後で行う (装飾の分だけテキスト幅が縮むため)。
    elideTextPerCharacter(option);
}

} // namespace efs
