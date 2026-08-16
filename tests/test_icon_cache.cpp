// 結果行のアイコンキャッシュ (Phase 3)。
//
// 実際の shell lookup (ShellIcon.cpp / Win32) は注入されるので、ここではフェイクの
// loader を使って決定的に検証する。**sleep でタイミングを合わせない** — loader を
// semaphore で止め、進行を完全に制御する。
//
// 固定したいのは:
//   - キーの正規化 (ディレクトリ / 大小同一 / 多重ドット / 拡張子なし)
//   - 同じキーの要求を重複させない (5,000 行の .txt で lookup 1 回)
//   - cache hit では二度と lookup しない (失敗した lookup も含む)
//   - 行番号を持たない完了通知なので、モデルを reset しても壊れない
#include <QMutex>
#include <QSemaphore>
#include <QSignalSpy>
#include <QtTest>

#include "app/IconCache.h"
#include "app/ResultTableModel.h"

namespace {

// 呼ばれた key を記録し、gate を 1 つ取れるまで戻らない loader。
class GatedLoader {
public:
    QImage operator()(const efs::IconKey& key)
    {
        {
            const QMutexLocker locker(&m_mutex);
            m_keys << (key.isDirectory ? QStringLiteral("dir") : key.extension);
        }
        m_started.release();
        m_gate.acquire();

        if (m_returnNull)
            return {};
        QImage image(4, 4, QImage::Format_ARGB32);
        image.fill(Qt::red);
        return image;
    }

    void release(int n = 1) { m_gate.release(n); }
    [[nodiscard]] bool waitStarted(int timeoutMs = 5000)
    {
        return m_started.tryAcquire(1, timeoutMs);
    }
    [[nodiscard]] QStringList keys() const
    {
        const QMutexLocker locker(&m_mutex);
        return m_keys;
    }
    [[nodiscard]] int lookupCount() const { return static_cast<int>(keys().size()); }
    void setReturnNull(bool on) { m_returnNull = on; }

private:
    mutable QMutex m_mutex;
    QStringList m_keys;
    QSemaphore m_started;
    QSemaphore m_gate;
    std::atomic<bool> m_returnNull{false};
};

// std::function にコピーされても同じ状態を共有するための薄いラッパ。
efs::IconLoader loaderFor(GatedLoader* loader)
{
    return [loader](const efs::IconKey& key) { return (*loader)(key); };
}

} // namespace

class TestIconCache : public QObject {
    Q_OBJECT

private slots:
    void iconKeyNormalization_data();
    void iconKeyNormalization();
    void iconKeyRoundTrip_data();
    void iconKeyRoundTrip();
    void modelExposesIconKeyWithoutTouchingDisk();

    void missReturnsFalseAndRequestsOnce();
    void duplicateRequestsAreDeduped();
    void cacheHitDoesNotLookUpAgain();
    void failedLookupIsCachedAndNotRetried();
    void completionAfterModelResetIsHarmless();
    void destroyingCacheWithPendingRequestIsSafe();
};

// --- キーの正規化 -------------------------------------------------------------
void TestIconCache::iconKeyNormalization_data()
{
    QTest::addColumn<bool>("isDir");
    QTest::addColumn<QString>("name");
    QTest::addColumn<QString>("expected");

    QTest::newRow("directory") << true << QStringLiteral("Program Files") << QStringLiteral("dir:");
    // ディレクトリ名に拡張子らしきものが付いていてもディレクトリ扱い。
    QTest::newRow("directory with dot")
        << true << QStringLiteral("node_modules.bak") << QStringLiteral("dir:");
    QTest::newRow("lowercase ext")
        << false << QStringLiteral("report.txt") << QStringLiteral("ext:txt");
    QTest::newRow("uppercase ext")
        << false << QStringLiteral("REPORT.TXT") << QStringLiteral("ext:txt");
    QTest::newRow("mixed case ext")
        << false << QStringLiteral("Report.TxT") << QStringLiteral("ext:txt");
    QTest::newRow("multi-dot") << false << QStringLiteral("archive.tar.gz")
                               << QStringLiteral("ext:gz");
    QTest::newRow("extensionless") << false << QStringLiteral("README") << QStringLiteral("ext:");
    QTest::newRow("trailing dot") << false << QStringLiteral("name.") << QStringLiteral("ext:");
    QTest::newRow("leading dot") << false << QStringLiteral(".gitignore")
                                 << QStringLiteral("ext:gitignore");
    QTest::newRow("empty name") << false << QString() << QStringLiteral("ext:");
    QTest::newRow("spaces in name")
        << false << QStringLiteral("my report v2.PDF") << QStringLiteral("ext:pdf");
}

void TestIconCache::iconKeyNormalization()
{
    QFETCH(bool, isDir);
    QFETCH(QString, name);
    QFETCH(QString, expected);
    QCOMPARE(efs::iconKeyFor(isDir, name), expected);
}

// キーの生成と解釈は必ず対になっていること (片方だけ変えると lookup が静かに壊れる)。
void TestIconCache::iconKeyRoundTrip_data()
{
    QTest::addColumn<bool>("isDir");
    QTest::addColumn<QString>("name");
    QTest::addColumn<QString>("extension");

    QTest::newRow("directory") << true << QStringLiteral("Windows") << QString();
    QTest::newRow("txt") << false << QStringLiteral("a.TXT") << QStringLiteral("txt");
    QTest::newRow("multi-dot") << false << QStringLiteral("a.tar.gz") << QStringLiteral("gz");
    QTest::newRow("extensionless") << false << QStringLiteral("LICENSE") << QString();
}

void TestIconCache::iconKeyRoundTrip()
{
    QFETCH(bool, isDir);
    QFETCH(QString, name);
    QFETCH(QString, extension);

    const efs::IconKey parsed = efs::parseIconKey(efs::iconKeyFor(isDir, name));
    QCOMPARE(parsed.isDirectory, isDir);
    QCOMPARE(parsed.extension, extension);
}

// モデルが返すのは plain data の文字列だけ。QtGui にも実ファイルにも触らない。
void TestIconCache::modelExposesIconKeyWithoutTouchingDisk()
{
    efs::ResultTableModel model;
    QVector<efs::ResultRow> rows;

    efs::ResultRow file;
    file.name = QStringLiteral("Report.TXT");
    // 実在しないパス。data() が stat しないことの担保でもある。
    file.path = QStringLiteral("Z:\\does\\not\\exist");
    rows << file;

    efs::ResultRow dir;
    dir.name = QStringLiteral("Some Folder");
    dir.path = QStringLiteral("Z:\\does\\not\\exist");
    dir.isDir = true;
    rows << dir;

    model.setRows(rows);

    const QVariant fileKey =
        model.index(0, efs::ResultTableModel::ColumnName).data(efs::ResultTableModel::IconKeyRole);
    const QVariant dirKey =
        model.index(1, efs::ResultTableModel::ColumnName).data(efs::ResultTableModel::IconKeyRole);

    QCOMPARE(fileKey.metaType().id(), QMetaType::QString);
    QCOMPARE(fileKey.toString(), QStringLiteral("ext:txt"));
    QCOMPARE(dirKey.toString(), QStringLiteral("dir:"));

    // DecorationRole は空のまま (アイコンはモデルの責務ではない)。
    QVERIFY(!model.index(0, efs::ResultTableModel::ColumnName).data(Qt::DecorationRole).isValid());
}

// --- キャッシュの挙動 ---------------------------------------------------------
void TestIconCache::missReturnsFalseAndRequestsOnce()
{
    GatedLoader loader;
    loader.release(16);
    efs::IconCache cache(loaderFor(&loader));
    QSignalSpy spy(&cache, &efs::IconCache::imagesReady);

    QImage image;
    QVERIFY(!cache.image(QStringLiteral("ext:txt"), &image)); // miss
    QVERIFY(image.isNull());

    QTRY_COMPARE(spy.count(), 1);
    QCOMPARE(loader.keys(), QStringList({QStringLiteral("txt")}));

    // 次からは hit。
    QVERIFY(cache.image(QStringLiteral("ext:txt"), &image));
    QVERIFY(!image.isNull());
}

// 5,000 行が全部 .txt でも shell lookup は 1 回。
void TestIconCache::duplicateRequestsAreDeduped()
{
    GatedLoader loader;
    efs::IconCache cache(loaderFor(&loader));

    QImage image;
    QVERIFY(!cache.image(QStringLiteral("ext:txt"), &image));
    QVERIFY(loader.waitStarted()); // worker は 1 本目で止まっている

    // 止まっている間に同じキーを大量に要求する。
    for (int i = 0; i < 500; ++i)
        QVERIFY(!cache.image(QStringLiteral("ext:txt"), &image));

    loader.release();
    QTRY_VERIFY(cache.image(QStringLiteral("ext:txt"), &image));

    // 遅れて 2 本目が走らないこと。
    QTest::qWait(50);
    QCOMPARE(loader.lookupCount(), 1);
}

void TestIconCache::cacheHitDoesNotLookUpAgain()
{
    GatedLoader loader;
    loader.release(16);
    efs::IconCache cache(loaderFor(&loader));

    QImage image;
    QVERIFY(!cache.image(QStringLiteral("ext:png"), &image));
    QTRY_VERIFY(cache.image(QStringLiteral("ext:png"), &image));

    const int afterFirst = loader.lookupCount();
    for (int i = 0; i < 100; ++i)
        QVERIFY(cache.image(QStringLiteral("ext:png"), &image));

    QTest::qWait(50);
    QCOMPARE(loader.lookupCount(), afterFirst);
    QCOMPARE(afterFirst, 1);
}

// 未知の拡張子で lookup が失敗しても、毎回引き直さない。
void TestIconCache::failedLookupIsCachedAndNotRetried()
{
    GatedLoader loader;
    loader.setReturnNull(true);
    loader.release(16);
    efs::IconCache cache(loaderFor(&loader));
    QSignalSpy spy(&cache, &efs::IconCache::imagesReady);

    QImage image;
    QVERIFY(!cache.image(QStringLiteral("ext:qqqunknown"), &image));
    QTRY_COMPARE(spy.count(), 1);

    // hit ではあるが画像は null。UI 側は汎用アイコンで描く。
    QVERIFY(cache.image(QStringLiteral("ext:qqqunknown"), &image));
    QVERIFY(image.isNull());

    for (int i = 0; i < 50; ++i)
        QVERIFY(cache.image(QStringLiteral("ext:qqqunknown"), &image));
    QTest::qWait(50);
    QCOMPARE(loader.lookupCount(), 1);
}

// 高速に検索を切り替えている最中に icon の完了通知が来ても壊れないこと。
// 完了通知が行番号を持たない設計なので、モデルが何行になっていても関係ない。
void TestIconCache::completionAfterModelResetIsHarmless()
{
    GatedLoader loader;
    efs::IconCache cache(loaderFor(&loader));
    efs::ResultTableModel model;

    efs::ResultRow row;
    row.name = QStringLiteral("a.txt");
    model.setRows({row});

    QImage image;
    QVERIFY(!cache.image(model.index(0, efs::ResultTableModel::ColumnName)
                             .data(efs::ResultTableModel::IconKeyRole)
                             .toString(),
                         &image));
    QVERIFY(loader.waitStarted());

    // lookup 中に結果が総入れ替えになる (別の検索が完了した)。
    model.setRows({});
    QCOMPARE(model.rowCount(), 0);

    int updates = 0;
    connect(&cache, &efs::IconCache::imagesReady, this, [&updates] { ++updates; });

    loader.release();
    QTRY_COMPARE(updates, 1);

    // 通知は届くが行には触れない。キャッシュには入っている。
    QVERIFY(cache.image(QStringLiteral("ext:txt"), &image));
    QCOMPARE(model.rowCount(), 0);
}

// 検索を閉じる / ウィンドウを閉じる際に、lookup 中でもきれいに終われること。
void TestIconCache::destroyingCacheWithPendingRequestIsSafe()
{
    GatedLoader loader;
    {
        efs::IconCache cache(loaderFor(&loader));
        QImage image;
        QVERIFY(!cache.image(QStringLiteral("ext:mp3"), &image));
        QVERIFY(loader.waitStarted());
        // worker を止めたまま破棄へ入るので、デストラクタは gate 解放を待つ。
        loader.release();
    }
    // ここへ到達すればスレッドは正しく join されている。
    QCOMPARE(loader.lookupCount(), 1);
}

QTEST_GUILESS_MAIN(TestIconCache)
#include "test_icon_cache.moc"
