// SearchController のデバウンスと stale 破棄の検証 (計画 4 / N3)。
//
// Everything には依存させない。代わりに「呼ばれたことを通知し、テスト側が許可
// するまで戻らない」フェイク backend を使い、検索スレッドの進行を完全に制御する。
// sleep のタイミング偶然に頼らないので flaky にならない。
//
// stale 破棄には 2 段ある。両方を別々のテストで固定する:
//   (1) worker 側 — キューに溜まった未実行の要求を実行前に捨てる
//   (2) UI 側     — 実行中だったために (1) をすり抜けた古い結果を採用しない
#include <QMutex>
#include <QSemaphore>
#include <QSignalSpy>
#include <QtTest>

#include "app/ResultTableModel.h"
#include "app/SearchController.h"
#include "core/ISearchBackend.h"

namespace {

// 呼ばれると started を上げ、gate を 1 つ取れるまで戻らない backend。
class GatedFakeBackend final : public efs::ISearchBackend {
public:
    [[nodiscard]] QString name() const override { return QStringLiteral("fake"); }
    [[nodiscard]] bool isAvailable(QString*) const override { return true; }

    efs::SearchResults search(const efs::SearchQuery& query) override
    {
        {
            const QMutexLocker locker(&m_mutex);
            m_executed << query.text;
        }
        m_started.release();
        m_gate.acquire();

        efs::SearchResults results;
        results.id = query.id;
        efs::ResultRow row;
        row.name = query.text; // どの要求の結果かを名前で見分ける
        results.rows << row;
        results.totalMatches = 1;
        return results;
    }

    // 検索スレッドが search() に入るのを待つ。
    [[nodiscard]] bool waitStarted(int timeoutMs = 5000)
    {
        return m_started.tryAcquire(1, timeoutMs);
    }
    void release(int n = 1) { m_gate.release(n); }

    [[nodiscard]] QStringList executed() const
    {
        const QMutexLocker locker(&m_mutex);
        return m_executed;
    }

private:
    mutable QMutex m_mutex;
    QStringList m_executed;
    QSemaphore m_started;
    QSemaphore m_gate;
};

efs::SearchResults resultsFrom(const QSignalSpy& spy, int index)
{
    return spy.at(index).at(0).value<efs::SearchResults>();
}

} // namespace

class TestSearchController : public QObject {
    Q_OBJECT

private slots:
    void staleQueuedRequestsAreDroppedBeforeExecution();
    void staleResultDoesNotOverwriteNewerOne();
    void debounceCoalescesRapidInput();
    void emptyTextClearsWithoutSearching();
};

// (1) worker 側。4 要求を連射すると、実行されるのは最初と最後だけで、
//     UI へ届くのは最後の要求の結果だけ。
void TestSearchController::staleQueuedRequestsAreDroppedBeforeExecution()
{
    auto backend = std::make_unique<GatedFakeBackend>();
    GatedFakeBackend* fake = backend.get();

    efs::SearchController controller(std::move(backend));
    efs::ResultTableModel model;
    connect(&controller, &efs::SearchController::resultsReady, &model,
            [&model](const efs::SearchResults& results) { model.setRows(results.rows); });

    QSignalSpy spy(&controller, &efs::SearchController::resultsReady);

    // "a" を検索させ、backend の中で止める。
    controller.setText(QStringLiteral("a"));
    controller.searchNow();
    QVERIFY(fake->waitStarted());

    // 止まっている間に 3 つ積む。id は 2, 3, 4 で、最新は 4。
    for (const QString& text :
         {QStringLiteral("ab"), QStringLiteral("abc"), QStringLiteral("abcdef")}) {
        controller.setText(text);
        controller.searchNow();
    }

    fake->release();              // "a" が完了 (結果は UI 側で捨てられる)
    QVERIFY(fake->waitStarted()); // 次に実行に入るのは "abcdef"。"ab"/"abc" は捨てられている
    fake->release();

    QTRY_COMPARE(spy.count(), 1);
    // 遅れて追加の結果が来ないこと。
    QTest::qWait(50);
    QCOMPARE(spy.count(), 1);

    QCOMPARE(fake->executed(), QStringList({QStringLiteral("a"), QStringLiteral("abcdef")}));
    QCOMPARE(resultsFrom(spy, 0).id, quint64(4));
    QCOMPARE(resultsFrom(spy, 0).rows.at(0).name, QStringLiteral("abcdef"));
    // 画面に残るのは最後の入力の結果。
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.index(0, efs::ResultTableModel::ColumnName).data().toString(),
             QStringLiteral("abcdef"));
}

// (2) UI 側。実行中の要求は worker 側の事前チェックをすり抜けるので、
//     完了時点で古くなっていたら UI 側で捨てなければならない。
void TestSearchController::staleResultDoesNotOverwriteNewerOne()
{
    auto backend = std::make_unique<GatedFakeBackend>();
    GatedFakeBackend* fake = backend.get();

    efs::SearchController controller(std::move(backend));
    QSignalSpy spy(&controller, &efs::SearchController::resultsReady);

    controller.setText(QStringLiteral("a"));
    controller.searchNow(); // id 1。worker の事前チェックは通過済み
    QVERIFY(fake->waitStarted());

    controller.setText(QStringLiteral("abcdef"));
    controller.searchNow(); // id 2 が最新になる

    fake->release();              // id 1 が完了 → UI 側で破棄されること
    QVERIFY(fake->waitStarted()); // id 2 が実行に入る
    fake->release();

    QTRY_COMPARE(spy.count(), 1);
    QTest::qWait(50);
    QCOMPARE(spy.count(), 1);

    // どちらも実行はされた。採用されたのは新しい方だけ。
    QCOMPARE(fake->executed(), QStringList({QStringLiteral("a"), QStringLiteral("abcdef")}));
    QCOMPARE(resultsFrom(spy, 0).id, quint64(2));
    QCOMPARE(resultsFrom(spy, 0).rows.at(0).name, QStringLiteral("abcdef"));
}

void TestSearchController::debounceCoalescesRapidInput()
{
    auto backend = std::make_unique<GatedFakeBackend>();
    GatedFakeBackend* fake = backend.get();
    fake->release(16); // このテストでは backend を止めない

    constexpr int kDebounceMs = 50;
    efs::SearchController controller(std::move(backend), kDebounceMs);
    QSignalSpy spy(&controller, &efs::SearchController::resultsReady);

    controller.setText(QStringLiteral("a"));
    controller.setText(QStringLiteral("ab"));
    controller.setText(QStringLiteral("abc"));
    // デバウンス中は 1 回も検索していない。
    QVERIFY(fake->executed().isEmpty());

    QTRY_COMPARE(spy.count(), 1);
    QCOMPARE(fake->executed(), QStringList({QStringLiteral("abc")}));
}

void TestSearchController::emptyTextClearsWithoutSearching()
{
    auto backend = std::make_unique<GatedFakeBackend>();
    GatedFakeBackend* fake = backend.get();
    fake->release(16);

    efs::SearchController controller(std::move(backend));
    QSignalSpy resultsSpy(&controller, &efs::SearchController::resultsReady);
    QSignalSpy clearedSpy(&controller, &efs::SearchController::cleared);

    controller.setText(QStringLiteral("abc"));
    controller.setText(QString());

    QCOMPARE(clearedSpy.count(), 1);
    // デバウンスが残っていて後から検索が走らないこと。
    QTest::qWait(2 * efs::SearchController::kDefaultDebounceMs);
    QVERIFY(fake->executed().isEmpty());
    QCOMPARE(resultsSpy.count(), 0);
}

QTEST_GUILESS_MAIN(TestSearchController)
#include "test_search_controller.moc"
