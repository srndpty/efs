#include "app/ToolbarIcons.h"

#include <QColor>
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

QIcon fromPath(const QPainterPath& path, const QColor& ink)
{
    QPixmap pixmap(kSize, kSize);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    // 括弧だと関数宣言に解釈される (most vexing parse) ため波括弧で初期化する。
    QPen pen{ink};
    pen.setWidthF(kStroke);
    pen.setJoinStyle(Qt::RoundJoin);
    pen.setCapStyle(Qt::RoundCap);
    painter.setPen(pen);
    painter.drawPath(path);
    painter.end();

    return {pixmap};
}

} // namespace

QIcon kindIcon(FileKind kind, const QColor& ink)
{
    switch (kind) {
    case FileKind::All:
        return fromPath(allGlyph(), ink);
    case FileKind::Image:
        return fromPath(imageGlyph(), ink);
    case FileKind::Video:
        return fromPath(videoGlyph(), ink);
    case FileKind::Audio:
        return fromPath(audioGlyph(), ink);
    case FileKind::Document:
        return fromPath(documentGlyph(), ink);
    case FileKind::Directory:
        return fromPath(directoryGlyph(), ink);
    }
    return {};
}

QIcon themeIcon(const QColor& ink)
{
    QPixmap pixmap(kSize, kSize);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPen pen{ink};
    pen.setWidthF(kStroke);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);

    // 円の輪郭 + 右半分の塗りつぶし = 明暗の対比。
    const QRectF circle(14, 14, 36, 36);
    painter.drawEllipse(circle);
    painter.setPen(Qt::NoPen);
    painter.setBrush(ink);
    painter.drawChord(circle, -90 * 16, 180 * 16);
    painter.end();

    return {pixmap};
}

QIcon regexIcon(const QColor& ink)
{
    QPixmap pixmap(kSize, kSize);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(ink);
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
