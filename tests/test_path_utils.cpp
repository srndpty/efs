// PathUtils (fullPath / normalizeQuerySeparators) の単体テスト (計画 8 / Phase 2)。
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
    void normalizesQuerySeparators_data();
    void normalizesQuerySeparators();
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

// `/` を `\` へ揃える。Everything がパス区切りとして見るのは `\` だけなので、
// これをしないと `path/to/file.txt` はどの行にも当たらない。
//
// 関数構文の値 (`dm:2026/01/01` 等) だけは触らない。日付の `/` はパス区切りでは
// ないため、変換すると検索そのものが壊れる。
void TestPathUtils::normalizesQuerySeparators_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<QString>("expected");

    QTest::newRow("empty") << QString() << QString();
    QTest::newRow("no separator") << QStringLiteral("report") << QStringLiteral("report");
    QTest::newRow("relative path")
        << QStringLiteral("path/to/file.txt") << QStringLiteral("path\\to\\file.txt");
    QTest::newRow("drive path") << QStringLiteral("C:/dev/soft") << QStringLiteral("C:\\dev\\soft");
    QTest::newRow("backslash is left as is")
        << QStringLiteral("path\\to\\file.txt") << QStringLiteral("path\\to\\file.txt");
    QTest::newRow("mixed separators")
        << QStringLiteral("C:\\dev/soft") << QStringLiteral("C:\\dev\\soft");
    QTest::newRow("UNC path") << QStringLiteral("//server/share")
                              << QStringLiteral("\\\\server\\share");
    QTest::newRow("quoted path") << QStringLiteral("\"my docs/2026\"")
                                 << QStringLiteral("\"my docs\\2026\"");
    QTest::newRow("negated term") << QStringLiteral("!build/tmp") << QStringLiteral("!build\\tmp");
    QTest::newRow("multiple terms")
        << QStringLiteral("src/core report") << QStringLiteral("src\\core report");
    QTest::newRow("terms separated by OR")
        << QStringLiteral("src/core|src/app") << QStringLiteral("src\\core|src\\app");
    QTest::newRow("grouped terms")
        << QStringLiteral("<src/core a>") << QStringLiteral("<src\\core a>");
    QTest::newRow("wildcard path")
        << QStringLiteral("src/*/main.cpp") << QStringLiteral("src\\*\\main.cpp");

    // --- 関数構文の値は触らない ----------------------------------------------
    // 日付の `/` はパス区切りではない。ここを変換すると検索そのものが壊れる。
    QTest::newRow("date function is untouched")
        << QStringLiteral("dm:2026/01/01") << QStringLiteral("dm:2026/01/01");
    QTest::newRow("date range function is untouched")
        << QStringLiteral("dc:2026/01/01..2026/12/31")
        << QStringLiteral("dc:2026/01/01..2026/12/31");
    QTest::newRow("negated date function is untouched")
        << QStringLiteral("!dm:2026/01/01") << QStringLiteral("!dm:2026/01/01");
    QTest::newRow("function term does not leak into the next term")
        << QStringLiteral("dm:2026/01/01 src/core") << QStringLiteral("dm:2026/01/01 src\\core");
    QTest::newRow("path function value is untouched")
        << QStringLiteral("path:C:/dev") << QStringLiteral("path:C:/dev");
}

void TestPathUtils::normalizesQuerySeparators()
{
    QFETCH(QString, text);
    QFETCH(QString, expected);

    QCOMPARE(efs::normalizeQuerySeparators(text), expected);
}

QTEST_GUILESS_MAIN(TestPathUtils)
#include "test_path_utils.moc"
