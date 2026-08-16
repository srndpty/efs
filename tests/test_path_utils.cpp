// fullPath() の単体テスト (計画 8 / Phase 2)。
//
// Everything にも Widgets にも依存しない純粋関数なので常に実行する。
// 期待値の根拠は Phase 2 冒頭の実機観測 (README の「ResultRow::path の形」)。
#include <QtTest>

#include "core/PathUtils.h"

class TestPathUtils : public QObject {
    Q_OBJECT

private slots:
    void buildsFullPath_data();
    void buildsFullPath();
    void isUsableAsAFileSystemPath();
};

void TestPathUtils::buildsFullPath_data()
{
    QTest::addColumn<QString>("path");
    QTest::addColumn<QString>("name");
    QTest::addColumn<bool>("isDir");
    QTest::addColumn<QString>("expected");

    QTest::newRow("normal file") << QStringLiteral("C:\\dev\\soft") << QStringLiteral("a.txt")
                                 << false << QStringLiteral("C:\\dev\\soft\\a.txt");
    QTest::newRow("directory") << QStringLiteral("C:\\dev") << QStringLiteral("soft") << true
                               << QStringLiteral("C:\\dev\\soft");
    QTest::newRow("path with spaces")
        << QStringLiteral("C:\\Program Files\\Some App") << QStringLiteral("read me.txt") << false
        << QStringLiteral("C:\\Program Files\\Some App\\read me.txt");
    QTest::newRow("unicode") << QStringLiteral("C:\\ユーザー\\書類")
                             << QStringLiteral("報告書 2026.xlsx") << false
                             << QStringLiteral("C:\\ユーザー\\書類\\報告書 2026.xlsx");
    QTest::newRow("shell metacharacters stay literal")
        << QStringLiteral("C:\\a & b (c), d") << QStringLiteral("e&f (1).txt") << false
        << QStringLiteral("C:\\a & b (c), d\\e&f (1).txt");

    // --- Windows の edge case (実測) ------------------------------------------
    // ドライブ直下のファイルは path が "C:" (末尾に \ が付かない) で返る。
    // 区切りを省くと "C:a.txt" というドライブ相対パスになってしまう。
    QTest::newRow("file at drive root")
        << QStringLiteral("C:") << QStringLiteral("a.txt") << false << QStringLiteral("C:\\a.txt");
    // ドライブそのものは path が空、name が "C:"。裸の "C:" は相対パスなので
    // 区切りを補う。
    QTest::newRow("drive itself") << QString() << QStringLiteral("C:") << true
                                  << QStringLiteral("C:\\");
    QTest::newRow("path already ends with separator")
        << QStringLiteral("C:\\") << QStringLiteral("a.txt") << false
        << QStringLiteral("C:\\a.txt");
    QTest::newRow("path ends with forward slash")
        << QStringLiteral("C:/dev/") << QStringLiteral("a.txt") << false
        << QStringLiteral("C:/dev/a.txt");
    QTest::newRow("UNC path") << QStringLiteral("\\\\server\\share\\dir") << QStringLiteral("a.txt")
                              << false << QStringLiteral("\\\\server\\share\\dir\\a.txt");
    QTest::newRow("empty name") << QStringLiteral("C:\\dev") << QString() << true
                                << QStringLiteral("C:\\dev");
    QTest::newRow("empty path and non drive name")
        << QString() << QStringLiteral("a.txt") << false << QStringLiteral("a.txt");
    QTest::newRow("both empty") << QString() << QString() << false << QString();
}

void TestPathUtils::buildsFullPath()
{
    QFETCH(QString, path);
    QFETCH(QString, name);
    QFETCH(bool, isDir);
    QFETCH(QString, expected);

    efs::ResultRow row;
    row.path = path;
    row.name = name;
    row.isDir = isDir;

    QCOMPARE(efs::fullPath(row), expected);
}

// 実在するディレクトリで、組み立てた文字列がそのままファイルシステムの
// パスとして通ること。区切りの過不足はここで露見する。
void TestPathUtils::isUsableAsAFileSystemPath()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());

    const QString fileName = QStringLiteral("efs テスト ファイル (1) & co.txt");
    const QString absolute = QDir(dir.path()).absoluteFilePath(fileName);
    QFile file(absolute);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();

    efs::ResultRow row;
    // Everything は native 区切りで返すので、それに合わせる。
    row.path = QDir::toNativeSeparators(dir.path());
    row.name = fileName;

    QVERIFY2(QFileInfo::exists(efs::fullPath(row)), qPrintable(efs::fullPath(row)));
}

QTEST_GUILESS_MAIN(TestPathUtils)
#include "test_path_utils.moc"
