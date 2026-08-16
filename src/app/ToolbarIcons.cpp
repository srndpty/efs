#include "app/ToolbarIcons.h"

#include <QFont>
#include <QIcon>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QPolygonF>

namespace efs {

namespace {

// 論理 16px のツールバーで縮小表示される前提で、余裕を持って 64px で描く。
constexpr int kSize = 64;
constexpr qreal kStroke = 5.0;
constexpr auto kInk = "#d4d4d4";

QPainterPath imageGlyph()
{
    QPainterPath path;
    path.addRoundedRect(QRectF(9, 13, 46, 38), 5, 5);
    path.addEllipse(QPointF(22, 26), 5, 5);
    path.moveTo(13, 47);
    path.lineTo(26, 33);
    path.lineTo(35, 41);
    path.lineTo(44, 30);
    path.lineTo(53, 43);
    return path;
}

QPainterPath videoGlyph()
{
    QPainterPath path;
    path.addRoundedRect(QRectF(9, 15, 46, 34), 5, 5);
    QPolygonF play;
    play << QPointF(27, 24) << QPointF(43, 32) << QPointF(27, 40);
    path.addPolygon(play);
    path.closeSubpath();
    return path;
}

QPainterPath audioGlyph()
{
    QPainterPath path;
    path.addEllipse(QPointF(23, 45), 10, 8);
    path.moveTo(33, 45);
    path.lineTo(33, 12);
    path.lineTo(51, 18);
    path.lineTo(51, 26);
    return path;
}

QPainterPath documentGlyph()
{
    QPainterPath path;
    path.moveTo(15, 9);
    path.lineTo(38, 9);
    path.lineTo(49, 20);
    path.lineTo(49, 55);
    path.lineTo(15, 55);
    path.closeSubpath();
    path.moveTo(38, 9);
    path.lineTo(38, 20);
    path.lineTo(49, 20);
    path.moveTo(23, 33);
    path.lineTo(41, 33);
    path.moveTo(23, 43);
    path.lineTo(41, 43);
    return path;
}

QPainterPath directoryGlyph()
{
    QPainterPath path;
    path.moveTo(9, 51);
    path.lineTo(9, 16);
    path.lineTo(25, 16);
    path.lineTo(31, 23);
    path.lineTo(55, 23);
    path.lineTo(55, 51);
    path.closeSubpath();
    return path;
}

// All は「絞り込み無し = 一覧そのもの」なので行のリストとして描く。
QPainterPath allGlyph()
{
    QPainterPath path;
    for (const int y : {18, 32, 46}) {
        path.moveTo(12, y);
        path.lineTo(52, y);
    }
    return path;
}

QIcon fromPath(const QPainterPath& path)
{
    QPixmap pixmap(kSize, kSize);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    // 括弧だと関数宣言に解釈される (most vexing parse) ため波括弧で初期化する。
    QPen pen{QColor(kInk)};
    pen.setWidthF(kStroke);
    pen.setJoinStyle(Qt::RoundJoin);
    pen.setCapStyle(Qt::RoundCap);
    painter.setPen(pen);
    painter.drawPath(path);
    painter.end();

    return {pixmap};
}

} // namespace

QIcon kindIcon(FileKind kind)
{
    switch (kind) {
    case FileKind::All:
        return fromPath(allGlyph());
    case FileKind::Image:
        return fromPath(imageGlyph());
    case FileKind::Video:
        return fromPath(videoGlyph());
    case FileKind::Audio:
        return fromPath(audioGlyph());
    case FileKind::Document:
        return fromPath(documentGlyph());
    case FileKind::Directory:
        return fromPath(directoryGlyph());
    }
    return {};
}

QIcon regexIcon()
{
    QPixmap pixmap(kSize, kSize);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(QColor(kInk));
    QFont font = painter.font();
    font.setPixelSize(46);
    font.setBold(true);
    painter.setFont(font);
    // 正規表現の記号そのものを見せるのが一番分かりやすい。
    painter.drawText(pixmap.rect(), Qt::AlignCenter, QStringLiteral(".*"));
    painter.end();

    return {pixmap};
}

} // namespace efs
