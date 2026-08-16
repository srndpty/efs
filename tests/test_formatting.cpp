// 表示整形の単体テスト。Everything に依存しないため常に実行する。
//
// ロケール依存の表記そのもの (小数点や桁区切り記号) は環境で変わるので固定
// しない。「ディレクトリは空欄」「無効日時は -」「打ち切り表示が出る」といった
// 仕様側の性質を検証する。
#include <QLocale>
#include <QtTest>

#include "core/Formatting.h"

class TestFormatting : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();
    void formatSizeHandlesRange_data();
    void formatSizeHandlesRange();
    void formatModifiedHandlesInvalid();
    void formatModifiedShowsDate();
    void formatStatusNormal();
    void formatStatusSingleResult();
    void formatStatusTruncated();
    void formatStatusError();
    void formatStatusZeroResultsIsNotAnError();
};

void TestFormatting::initTestCase()
{
    // 桁区切りの期待値を固定するため。既定ロケールは環境依存。
    QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
}

void TestFormatting::formatSizeHandlesRange_data()
{
    QTest::addColumn<qint64>("size");
    QTest::addColumn<bool>("expectEmpty");

    QTest::newRow("directory / unknown") << qint64(-1) << true;
    QTest::newRow("zero") << qint64(0) << false;
    QTest::newRow("1023 bytes") << qint64(1023) << false;
    QTest::newRow("1 MiB") << qint64(1024) * 1024 << false;
    QTest::newRow("huge") << qint64(1024) * 1024 * 1024 * 1024 * 3 << false;
}

void TestFormatting::formatSizeHandlesRange()
{
    QFETCH(qint64, size);
    QFETCH(bool, expectEmpty);

    const QString text = efs::formatSize(size);
    QCOMPARE(text.isEmpty(), expectEmpty);
    if (!expectEmpty)
        QCOMPARE(text, QLocale().formattedDataSize(size));
}

void TestFormatting::formatModifiedHandlesInvalid()
{
    QCOMPARE(efs::formatModified(QDateTime()), QStringLiteral("-"));
}

void TestFormatting::formatModifiedShowsDate()
{
    const QDateTime when(QDate(2026, 8, 16), QTime(9, 30));
    const QString text = efs::formatModified(when);
    QVERIFY(!text.isEmpty());
    QCOMPARE(text, QLocale().toString(when, QLocale::ShortFormat));
    // 無効日時の表記と衝突しないこと。
    QVERIFY(text != QStringLiteral("-"));
    // 日時が違えば表示も違うこと (書式の桁数はロケール依存なので中身は固定しない)。
    QVERIFY(text != efs::formatModified(when.addYears(1)));
    QVERIFY(text != efs::formatModified(when.addSecs(3600)));
}

void TestFormatting::formatStatusNormal()
{
    efs::SearchResults results;
    results.rows.resize(1234);
    results.totalMatches = 1234;
    results.elapsedMs = 18;

    QCOMPARE(efs::formatStatus(results), QStringLiteral("1,234 results / 18 ms"));
}

void TestFormatting::formatStatusSingleResult()
{
    efs::SearchResults results;
    results.rows.resize(1);
    results.totalMatches = 1;
    results.elapsedMs = 3;

    QCOMPARE(efs::formatStatus(results), QStringLiteral("1 result / 3 ms"));
}

void TestFormatting::formatStatusTruncated()
{
    efs::SearchResults results;
    results.rows.resize(5000);
    results.totalMatches = 1234567;
    results.truncated = true;
    results.elapsedMs = 18;

    QCOMPARE(efs::formatStatus(results),
             QStringLiteral("1,234,567 results (showing first 5,000) / 18 ms"));
}

void TestFormatting::formatStatusError()
{
    efs::SearchResults results;
    results.error = QStringLiteral("Everything is not running.");
    results.elapsedMs = 1;

    // エラー時は件数を出さない。「正常に 0 件」と取り違えないよう必ず
    // "Search failed:" で始める (Phase 3)。
    QCOMPARE(efs::formatStatus(results),
             QStringLiteral("Search failed: Everything is not running."));
}

// 正常な 0 件は失敗と見分けが付くこと。ここが混ざると「Everything が落ちて
// いるのか、単に一致が無いのか」が UI から分からなくなる。
void TestFormatting::formatStatusZeroResultsIsNotAnError()
{
    efs::SearchResults results;
    results.totalMatches = 0;
    results.elapsedMs = 3;

    const QString status = efs::formatStatus(results);
    QCOMPARE(status, QStringLiteral("0 results / 3 ms"));
    QVERIFY(!status.startsWith(QStringLiteral("Search failed")));
}

QTEST_GUILESS_MAIN(TestFormatting)
#include "test_formatting.moc"
