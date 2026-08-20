#include "app/IconDelegate.h"

#include "app/IconCache.h"
#include "app/ResultTableModel.h"
#include "app/ShellIcon.h"
#include "app/Theme.h"

#include <QApplication>
#include <QImage>
#include <QPainter>
#include <QStyle>
#include <QStyleOptionViewItem>
#include <QTextLayout>
#include <QTextOption>

#include <utility>

namespace efs {

namespace {

QPixmap toPixmap(const QImage& image)
{
    return image.isNull() ? QPixmap() : QPixmap::fromImage(image);
}

// viewItemDrawText と同じ左右マージン。
int textMargin(QStyle* style, const QWidget* widget)
{
    return style->pixelMetric(QStyle::PM_FocusFrameHMargin, nullptr, widget) + 1;
}

QStyle* styleFor(const QStyleOptionViewItem& option)
{
    return option.widget ? option.widget->style() : QApplication::style();
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
    QStyle* style = styleFor(*option);
    const QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, option, widget);
    const int width = textRect.width() - (2 * textMargin(style, widget));
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

// QCommonStyle::viewItemDrawText と同じ規則でテキスト色を選ぶ。
QColor textColor(const QStyleOptionViewItem& option)
{
    QPalette::ColorGroup group =
        (option.state & QStyle::State_Enabled) ? QPalette::Normal : QPalette::Disabled;
    if (group == QPalette::Normal && !(option.state & QStyle::State_Active))
        group = QPalette::Inactive;
    return option.palette.color(group, (option.state & QStyle::State_Selected)
                                           ? QPalette::HighlightedText
                                           : QPalette::Text);
}

// option.text (= 省略済みの表示文字列) を、一致部分に地色を敷いて描く。
// delegate の状態を見ないので自由関数にしてある。
void drawHighlightedText(QPainter* painter, const QStyleOptionViewItem& option,
                         const QList<MatchRange>& ranges)
{
    QStyle* style = styleFor(option);
    QRect textRect = style->subElementRect(QStyle::SE_ItemViewItemText, &option, option.widget);
    const int margin = textMargin(style, option.widget);
    textRect.adjust(margin, 0, -margin, 0);
    if (textRect.width() <= 0 || textRect.height() <= 0)
        return;

    QTextLayout layout(option.text, option.font);
    QTextOption textOption;
    textOption.setWrapMode(QTextOption::NoWrap);
    textOption.setTextDirection(option.direction);
    textOption.setAlignment(QStyle::visualAlignment(option.direction, option.displayAlignment));
    layout.setTextOption(textOption);

    // 太字ではなく地色で示す。ダークテーマでは字面の太さの差が読み取りにくい
    // うえ、太字は幅が伸びて省略位置ともずれる。
    const MatchColors colors = matchColors(option.palette);
    QTextCharFormat highlight;
    highlight.setBackground(colors.background);
    highlight.setForeground(colors.text);

    QList<QTextLayout::FormatRange> formats;
    formats.reserve(ranges.size());
    for (const MatchRange& range : ranges)
        formats.append({.start = range.start, .length = range.length, .format = highlight});
    layout.setFormats(formats);

    layout.beginLayout();
    QTextLine line = layout.createLine();
    if (line.isValid())
        line.setLineWidth(textRect.width());
    layout.endLayout();
    if (!line.isValid())
        return;

    // 縦方向は displayAlignment に従う (既定の AlignVCenter を含む)。横方向は
    // QTextOption 側で済んでいるので、ここでは触らない。
    qreal y = textRect.y();
    const Qt::Alignment vertical = option.displayAlignment & Qt::AlignVertical_Mask;
    if (vertical & Qt::AlignBottom)
        y += textRect.height() - line.height();
    else if (!(vertical & Qt::AlignTop))
        y += (textRect.height() - line.height()) / 2.0;

    painter->save();
    // 地色の矩形が隣の列へ流れ出さないようにセル内へ切り詰める。
    painter->setClipRect(textRect, Qt::IntersectClip);
    painter->setPen(textColor(option));
    layout.draw(painter, QPointF(textRect.x(), y));
    painter->restore();
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

void IconDelegate::setHighlighter(MatchHighlighter highlighter, bool matchPath)
{
    m_highlighter = std::move(highlighter);
    m_matchPath = matchPath;
}

void IconDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
                         const QModelIndex& index) const
{
    // 強調する余地があるのは、backend が実際に照合した列だけ。Size / Date Modified
    // はそもそもクエリの対象ではなく、Path 列も matchPath が有効なときだけ
    // (見た目の文字列一致と、検索条件が当たった箇所を混同しない)。
    const bool matchable = !m_highlighter.isEmpty() &&
                           (index.column() == ResultTableModel::ColumnName ||
                            (m_matchPath && index.column() == ResultTableModel::ColumnPath));
    if (!matchable) {
        QStyledItemDelegate::paint(painter, option, index);
        return;
    }

    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);
    // 照合するのは**省略後**の表示文字列。実際に描く文字と位置がずれないため。
    const QList<MatchRange> ranges = m_highlighter.ranges(opt.text);
    if (ranges.isEmpty()) {
        QStyledItemDelegate::paint(painter, option, index);
        return;
    }

    // 背景・選択・交互行・アイコン・フォーカス枠は style に任せ、テキストだけ
    // 自前で描く。style へはテキストを空にして渡す (二重描画を防ぐ)。
    const QString text = opt.text;
    opt.text.clear();
    styleFor(opt)->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);
    opt.text = text;

    drawHighlightedText(painter, opt, ranges);
}

} // namespace efs
