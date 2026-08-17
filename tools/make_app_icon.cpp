// exe に埋め込む efs.ico をビルド時に生成する (Phase 4)。
//
// Explorer / タスクバーが出すアイコンは PE のリソースなので、実行時に QPainter で
// 描く appIcon() では出せず、ビルド時に .ico の実体が要る。**図形の定義を 2 箇所に
// 持たない**ため、リポジトリへ .ico をコミットするのではなく、同じ
// `efs::paintAppIcon()` をここから呼んで生成する。
//
// QPixmap を作らないので QGuiApplication は要らない (QImage + QPainter だけ)。
//
// ICO の書き出しは手書きする。Qt の ICO ハンドラは 1 画像しか書けず、複数サイズを
// 1 ファイルへ収められないため。16/32/48 は DIB、256 は PNG で持つ (256 を DIB に
// すると数百 KB になり、PNG 圧縮は Vista 以降が確実に読む)。
#include "app/ToolbarIcons.h"

#include <QBuffer>
#include <QByteArray>
#include <QCoreApplication>
#include <QDataStream>
#include <QFile>
#include <QImage>
#include <QList>
#include <QPainter>
#include <QString>
#include <QStringList>
#include <Qt>

#include <array>
#include <cstdio>

namespace {

// 収録するサイズ。Explorer の各表示 (詳細 16 / 中 32・48 / 特大 256) を賄う。
constexpr std::array<int, 4> kSizes{16, 32, 48, 256};
// これ以上は PNG で持つ (境界は Windows の慣習に合わせる)。
constexpr int kPngThreshold = 256;

// ICONDIRENTRY と BITMAPINFOHEADER の両方で使う。32bpp 固定・プレーンは 1 枚。
constexpr quint16 kPlanes = 1;
constexpr quint16 kBitsPerPixel = 32;

QImage render(int size)
{
    QImage image(size, size, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    efs::paintAppIcon(painter, size);
    painter.end();
    return image;
}

QByteArray toPng(const QImage& image)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, "PNG"))
        return {};
    return bytes;
}

// ICO 内の DIB: BITMAPINFOHEADER + ボトムアップの BGRA + AND マスク。
// 32bpp ではマスクは実質使われないが、**無いと読めない実装がある**ので必ず書く。
QByteArray toDib(const QImage& source)
{
    const QImage image = source.convertToFormat(QImage::Format_ARGB32);
    const int width = image.width();
    const int height = image.height();

    QByteArray bytes;
    QDataStream out(&bytes, QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::LittleEndian);

    // BITMAPINFOHEADER。QDataStream へ流す値は**型付きの定数**で用意する
    // (`quint32(0)` のような関数形式のキャストは新しい clang-tidy が
    // C-style cast として弾く)。
    constexpr quint32 kHeaderSize = 40;
    constexpr quint32 kBiRgb = 0;
    constexpr quint32 kUnused = 0;
    constexpr qint32 kUnusedSigned = 0;

    // biHeight は XOR 面と AND 面の 2 枚ぶんを表す (ICO の決まり)。
    out << kHeaderSize << static_cast<qint32>(width) << static_cast<qint32>(height * 2) << kPlanes
        << kBitsPerPixel << kBiRgb << kUnused                   /* biSizeImage */
        << kUnusedSigned /* biXPelsPerMeter */ << kUnusedSigned /* biYPelsPerMeter */
        << kUnused /* biClrUsed */ << kUnused /* biClrImportant */;

    for (int y = height - 1; y >= 0; --y) {
        const auto* line = reinterpret_cast<const QRgb*>(image.constScanLine(y));
        for (int x = 0; x < width; ++x) {
            const QRgb pixel = line[x];
            out << static_cast<quint8>(qBlue(pixel)) << static_cast<quint8>(qGreen(pixel))
                << static_cast<quint8>(qRed(pixel)) << static_cast<quint8>(qAlpha(pixel));
        }
    }

    // AND マスク: 1bpp、行は 4 バイト境界へ揃える。全 0 = 全ピクセル不透明扱い
    // (実際の透過は BGRA のアルファが持つ)。
    const int maskStride = ((width + 31) / 32) * 4;
    const QByteArray mask(static_cast<qsizetype>(maskStride) * height, '\0');
    out.writeRawData(mask.constData(), static_cast<int>(mask.size()));

    return bytes;
}

} // namespace

int main(int argc, char** argv)
{
    // 出力先のパスは argv から直接読まず QCoreApplication::arguments() を使う。
    // Windows では Qt が Unicode のコマンドラインから組み立てるので、非 ASCII を
    // 含むビルドディレクトリでも壊れない (argv は現在のコードページ止まり)。
    const QCoreApplication app(argc, argv);
    const QStringList args = QCoreApplication::arguments();
    if (args.size() != 2) {
        std::fprintf(stderr, "usage: efs_icongen <out.ico>\n");
        return 2;
    }
    const QString& outPath = args.at(1);

    QList<QByteArray> images;
    for (const int size : kSizes) {
        const QImage image = render(size);
        const QByteArray payload = size >= kPngThreshold ? toPng(image) : toDib(image);
        if (payload.isEmpty()) {
            std::fprintf(stderr, "efs_icongen: %dpx の書き出しに失敗した\n", size);
            return 1;
        }
        images.append(payload);
    }

    QByteArray ico;
    QDataStream out(&ico, QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::LittleEndian);

    constexpr quint16 kReserved = 0;
    constexpr quint16 kTypeIcon = 1;
    constexpr quint8 kZero8 = 0;

    const auto count = static_cast<quint16>(images.size());
    out << kReserved << kTypeIcon << count;

    // 画像本体はディレクトリの直後に並ぶ。先にオフセットを積算しておく
    // (ICONDIR が 6 バイト、ICONDIRENTRY が 1 件 16 バイト)。
    quint32 offset = 6 + (16 * static_cast<quint32>(count));
    for (int i = 0; i < images.size(); ++i) {
        const int size = kSizes[i];
        // 256 は 0 で表す (1 バイトに入らないため)。
        const auto dimension = static_cast<quint8>(size >= 256 ? 0 : size);
        out << dimension << dimension << kZero8 /* colorCount */ << kZero8 /* reserved */
            << kPlanes << kBitsPerPixel << static_cast<quint32>(images[i].size()) << offset;
        offset += static_cast<quint32>(images[i].size());
    }
    for (const QByteArray& payload : images)
        out.writeRawData(payload.constData(), static_cast<int>(payload.size()));

    QFile file(outPath);
    if (!file.open(QIODevice::WriteOnly)) {
        std::fprintf(stderr, "efs_icongen: %s を開けない: %s\n", qUtf8Printable(outPath),
                     qUtf8Printable(file.errorString()));
        return 1;
    }
    if (file.write(ico) != ico.size()) {
        std::fprintf(stderr, "efs_icongen: %s へ書き切れなかった\n", qUtf8Printable(outPath));
        return 1;
    }
    return 0;
}
