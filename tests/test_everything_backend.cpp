// EverythingBackend のテスト。
//
// FILETIME 変換は Everything 非依存の純粋関数なので常に実行する。
// 実際の検索は起動中の Everything を要求するため、利用できない環境では QSKIP
// する (CI には Everything が無い)。単体テストを環境依存で skip はしない。
#include <QTimeZone>
#include <QtTest>

#include <limits>

#include "backend/everything/EverythingBackend.h"
#include "core/FileKinds.h"

class TestEverythingBackend : public QObject {
    Q_OBJECT

private slots:
    void initTestCase();

    // --- 常に実行する単体テスト ---------------------------------------------
    void fileTimeToDateTime_data();
    void fileTimeToDateTime();

    // --- Everything が必要な統合テスト --------------------------------------
    void searchReturnsResults();
    void searchRespectsMaxResults();
    void searchReturnsMetadata();
    void directoryFilterReturnsOnlyDirectories();
    void extAndRegexAreCombinedWithAnd();

private:
    // 空でなければ統合テストを skip する理由。
    QString m_skipReason;
    efs::EverythingBackend m_backend;
};

void TestEverythingBackend::initTestCase()
{
    QString reason;
    if (!m_backend.isAvailable(&reason)) {
        m_skipReason = QStringLiteral("Everything SDK を利用できない: %1").arg(reason);
        return;
    }

    // DLL があっても Everything.exe が起動していなければ IPC は失敗する。
    efs::SearchQuery probe;
    probe.text = QStringLiteral("a");
    probe.maxResults = 1;
    const efs::SearchResults results = m_backend.search(probe);
    if (!results.error.isEmpty())
        m_skipReason = QStringLiteral("Everything へ問い合わせできない: %1").arg(results.error);
}

#define SKIP_WITHOUT_EVERYTHING()                                                                  \
    do {                                                                                           \
        if (!m_skipReason.isEmpty())                                                               \
            QSKIP(qPrintable(m_skipReason));                                                       \
    } while (false)

void TestEverythingBackend::fileTimeToDateTime_data()
{
    QTest::addColumn<quint64>("fileTime");
    QTest::addColumn<bool>("valid");
    QTest::addColumn<qint64>("msecsSinceEpoch");

    // FILETIME の 0 だけが「日時なし」。
    QTest::newRow("zero") << quint64(0) << false << qint64(0);
    // qint64 で表せない値は日時ではない。1601 年付近へ化けさせず無効を返す。
    QTest::newRow("not a date (all bits set)")
        << std::numeric_limits<quint64>::max() << false << qint64(0);

    QTest::newRow("unix epoch") << quint64(116444736000000000ULL) << true << qint64(0);
    QTest::newRow("1 tick before unix epoch")
        << quint64(116444736000000000ULL - 1) << true << qint64(0);
    QTest::newRow("1s before unix epoch")
        << quint64(116444736000000000ULL - 10000000ULL) << true << qint64(-1000);
    // 1970 より前も負の Unix epoch time として正しく変換する。
    const qint64 msecs1960 =
        QDateTime(QDate(1960, 3, 4), QTime(5, 6, 7), QTimeZone::UTC).toMSecsSinceEpoch();
    QTest::newRow("1960-03-04T05:06:07Z")
        << quint64(116444736000000000ULL - static_cast<quint64>(-msecs1960) * 10000ULL) << true
        << msecs1960;
    // FILETIME の原点付近 (1601-01-01T00:00:00.001Z)。下端でも破綻しないこと。
    QTest::newRow("filetime epoch + 1ms")
        << quint64(10000) << true
        << QDateTime(QDate(1601, 1, 1), QTime(0, 0, 0, 1), QTimeZone::UTC).toMSecsSinceEpoch();
    QTest::newRow("unix epoch + 1s")
        << quint64(116444736000000000ULL + 10000000ULL) << true << qint64(1000);
    // 実日時。期待値は QDateTime から導出し、テスト側で暦計算をしない。
    const qint64 msecs =
        QDateTime(QDate(2026, 8, 16), QTime(9, 30), QTimeZone::UTC).toMSecsSinceEpoch();
    QTest::newRow("2026-08-16T09:30Z")
        << quint64(116444736000000000ULL + (static_cast<quint64>(msecs) * 10000ULL)) << true
        << msecs;
}

void TestEverythingBackend::fileTimeToDateTime()
{
    QFETCH(quint64, fileTime);
    QFETCH(bool, valid);
    QFETCH(qint64, msecsSinceEpoch);

    const QDateTime converted = efs::fileTimeToDateTime(fileTime);
    QCOMPARE(converted.isValid(), valid);
    if (valid)
        QCOMPARE(converted.toMSecsSinceEpoch(), msecsSinceEpoch);
}

void TestEverythingBackend::searchReturnsResults()
{
    SKIP_WITHOUT_EVERYTHING();

    efs::SearchQuery query;
    query.text = QStringLiteral("exe");
    const efs::SearchResults results = m_backend.search(query);

    QVERIFY(results.error.isEmpty());
    QVERIFY(!results.rows.isEmpty());
    QVERIFY(results.totalMatches >= static_cast<quint64>(results.rows.size()));
    QCOMPARE(results.truncated, results.totalMatches > static_cast<quint64>(results.rows.size()));
    QVERIFY(results.elapsedMs >= 0);
}

void TestEverythingBackend::searchRespectsMaxResults()
{
    SKIP_WITHOUT_EVERYTHING();

    efs::SearchQuery query;
    query.text = QStringLiteral("e"); // 大量にヒットさせて打ち切りを起こす
    query.id = 42;
    query.maxResults = 10;
    const efs::SearchResults results = m_backend.search(query);

    QVERIFY(results.error.isEmpty());
    QCOMPARE(results.id, quint64(42)); // 要求 id が結果に載って返ること
    QVERIFY(results.rows.size() <= 10);
    QVERIFY(results.totalMatches >= static_cast<quint64>(results.rows.size()));
    if (results.totalMatches > 10)
        QVERIFY(results.truncated);
}

void TestEverythingBackend::searchReturnsMetadata()
{
    SKIP_WITHOUT_EVERYTHING();

    efs::SearchQuery query;
    query.text = QStringLiteral("exe");
    query.maxResults = 20;
    // 名前昇順の先頭はディレクトリで埋まりうる (実測)。サイズ降順にして
    // ファイルを確実に含め、ついでにソート指定が backend へ渡ることも見る。
    query.sortKey = efs::SortKey::Size;
    query.sortOrder = efs::SortOrder::Desc;
    const efs::SearchResults results = m_backend.search(query);

    QVERIFY(results.error.isEmpty());
    QVERIFY(!results.rows.isEmpty());

    int withSize = 0;
    int withDate = 0;
    for (const efs::ResultRow& row : results.rows) {
        QVERIFY(!row.name.isEmpty());
        QVERIFY(!row.path.isEmpty());
        if (row.isDir)
            QCOMPARE(row.size, qint64(-1)); // ディレクトリのサイズは表示しない
        else if (row.size >= 0)
            ++withSize;
        if (row.modified.isValid()) {
            ++withDate;
            // 1980 年より前 / 未来すぎる値は変換ミスを疑う。
            QVERIFY(row.modified.date().year() >= 1980);
            QVERIFY(row.modified < QDateTime::currentDateTime().addYears(1));
        }
    }
    QVERIFY(withSize > 0);
    QVERIFY(withDate > 0);
    // サイズ降順が効いていること。
    QVERIFY(results.rows.first().size >= results.rows.last().size);
}

void TestEverythingBackend::directoryFilterReturnsOnlyDirectories()
{
    SKIP_WITHOUT_EVERYTHING();

    efs::SearchQuery query;
    query.kind = efs::FileKind::Directory;
    query.maxResults = 50;
    const efs::SearchResults results = m_backend.search(query);

    QVERIFY(results.error.isEmpty());
    QVERIFY(!results.rows.isEmpty());
    for (const efs::ResultRow& row : results.rows)
        QVERIFY2(row.isDir, qPrintable(row.name));
}

// Phase 0 の spike (削除済み) が実機で確認した結論の回帰テスト。
//
// `ext:...` と `regex:...` は Everything_SetRegex(FALSE) のまま AND 結合される。
// 陽性 (全行が拡張子条件を満たす) と陰性対照 (一致しない正規表現で 0 件) の
// 両方を見る。片方だけでは「regex: 項が無視されている」場合と区別できない。
void TestEverythingBackend::extAndRegexAreCombinedWithAnd()
{
    SKIP_WITHOUT_EVERYTHING();

    efs::SearchQuery positive;
    positive.kind = efs::FileKind::Document;
    positive.regex = true;
    positive.text = QStringLiteral("^.");
    positive.maxResults = 100;
    const efs::SearchResults positiveResults = m_backend.search(positive);

    QVERIFY(positiveResults.error.isEmpty());
    if (positiveResults.rows.isEmpty())
        QSKIP("この環境には Document 種別のファイルが無く、併用を判定できない");

    const QStringList extensions = efs::extensionsFor(efs::FileKind::Document);
    for (const efs::ResultRow& row : positiveResults.rows) {
        const QString suffix = row.name.section(u'.', -1).toLower();
        QVERIFY2(extensions.contains(suffix), qPrintable(row.name));
    }

    // 陰性対照。ext: 項が生きていても regex: 項が AND されていれば 0 件になる。
    efs::SearchQuery negative = positive;
    negative.text = QStringLiteral("^ZZQXNOMATCH");
    const efs::SearchResults negativeResults = m_backend.search(negative);

    QVERIFY(negativeResults.error.isEmpty());
    QVERIFY(negativeResults.rows.isEmpty());
    QCOMPARE(negativeResults.totalMatches, quint64(0));
}

QTEST_GUILESS_MAIN(TestEverythingBackend)
#include "test_everything_backend.moc"
