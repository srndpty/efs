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
            m_queries << query;
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

    // 検索スレッドが search() に入るのを待つ。**permit を 1 つ消費する**ので、
    // 式を複数回評価する QTRY_* の中で使ってはならない (待機には副作用の無い
    // executed() を使う)。
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

    // backend が実際に受け取った SearchQuery。kind / regex / sort の伝播を
    // 見るために記録する。
    [[nodiscard]] QList<efs::SearchQuery> queries() const
    {
        const QMutexLocker locker(&m_mutex);
        return m_queries;
    }

private:
    mutable QMutex m_mutex;
    QStringList m_executed;
    QList<efs::SearchQuery> m_queries;
    QSemaphore m_started;
    QSemaphore m_gate;
};

efs::SearchResults resultsFrom(const QSignalSpy& spy, int index)
{
    return spy.at(index).at(0).value<efs::SearchResults>();
}

} // namespace

// QTest::addColumn で使うため。SearchTypes.h 側に置くと、テスト以外に不要な
// メタタイプ登録が増えるのでここで宣言する。
Q_DECLARE_METATYPE(efs::FileKind)

class TestSearchController : public QObject {
    Q_OBJECT

private slots:
    void staleQueuedRequestsAreDroppedBeforeExecution();
    void staleResultDoesNotOverwriteNewerOne();
    void resultCompletingDuringDebounceIsDropped();
    void debounceCoalescesRapidInput();
    void emptyTextClearsWithoutSearching();

    // --- Phase 2: フィルタ / Regex / ソートの状態遷移 -------------------------
    void hasSearchConstraint_data();
    void hasSearchConstraint();
    void emptyTextWithFileKindSearches_data();
    void emptyTextWithFileKindSearches();
    void returningToAllWhileEmptyClears();
    void kindChangeSearchesImmediately();
    void regexChangeSearchesImmediately();
    void sortChangePropagatesImmediately();
    void redundantStateChangesIssueNoQuery();
    void whitespaceOnlyTextSearchesOnlyWithRegex();
    void stateChangeInvalidatesRunningSearch_data();
    void stateChangeInvalidatesRunningSearch();
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

    // 止まっている間に 3 つ積む。最後の "abcdef" が最新世代になる。
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
    // id の具体値は世代の採番規則に依存するので、内容で判定する。
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
    controller.searchNow(); // worker の事前チェックは通過済み
    QVERIFY(fake->waitStarted());

    controller.setText(QStringLiteral("abcdef"));
    controller.searchNow(); // "abcdef" が最新世代になる

    fake->release();              // "a" が完了 → UI 側で破棄されること
    QVERIFY(fake->waitStarted()); // "abcdef" が実行に入る
    fake->release();

    QTRY_COMPARE(spy.count(), 1);
    QTest::qWait(50);
    QCOMPARE(spy.count(), 1);

    // どちらも実行はされた。採用されたのは新しい方だけ。
    QCOMPARE(fake->executed(), QStringList({QStringLiteral("a"), QStringLiteral("abcdef")}));
    QCOMPARE(resultsFrom(spy, 0).rows.at(0).name, QStringLiteral("abcdef"));
}

// (3) デバウンス待ちの時間窓。A の実行中に検索欄が B へ変わったら、その時点で
//     A は stale になる。B の dispatch を待ってから世代を進めると、その間に
//     完了した A の結果が「検索欄は B なのに A の結果が描かれる」形で流れる。
void TestSearchController::resultCompletingDuringDebounceIsDropped()
{
    auto backend = std::make_unique<GatedFakeBackend>();
    GatedFakeBackend* fake = backend.get();

    // A の完了を debounce 満了より前へ置くための余裕。なお B は gate で止まる
    // ので、spy が埋まりうる経路は「A の結果が流れた」場合しかない。判定自体は
    // タイミングに依存しない。
    constexpr int kDebounceMs = 1000;
    efs::SearchController controller(std::move(backend), kDebounceMs);
    QSignalSpy spy(&controller, &efs::SearchController::resultsReady);

    // A を即時 dispatch し、backend の中で止める。
    controller.setText(QStringLiteral("A"));
    controller.searchNow();
    QVERIFY(fake->waitStarted());

    // 検索欄が B になる。searchNow() は呼ばない = まだ debounce 待ち。
    controller.setText(QStringLiteral("B"));

    // debounce 満了より前に A を完了させる。
    fake->release();

    // A の結果は UI へ流れてはならない。
    QTest::qWait(400);
    QVERIFY2(spy.isEmpty(), "debounce 待ちの間に完了した古い結果が流れた");

    // その後、debounce 満了により B が実行される。ここは semaphore で待てない —
    // ブロックすると UI スレッドの event loop が止まり、debounce の QTimer が
    // 発火しなくなる。event loop を回しながら、副作用の無い executed() で待つ。
    QTRY_COMPARE(fake->executed(), QStringList({QStringLiteral("A"), QStringLiteral("B")}));
    fake->release();

    QTRY_COMPARE(spy.count(), 1);
    QTest::qWait(50);
    QCOMPARE(spy.count(), 1);

    // A も B も実行はされたが、採用されたのは B だけ。
    QCOMPARE(resultsFrom(spy, 0).rows.at(0).name, QStringLiteral("B"));
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

// --- Phase 2 -----------------------------------------------------------------
//
// 「検索しない」の判定は Phase 1 の「テキストが空」ではなく「絞り込みが 1 つも
// 無い」へ移した。SearchController は backend 非依存のこの述語だけを見る
// (Everything 固有の buildQueryString() は呼ばない)。
void TestSearchController::hasSearchConstraint_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<efs::FileKind>("kind");
    QTest::addColumn<bool>("regex");
    QTest::addColumn<bool>("expected");

    QTest::newRow("empty + All") << QString() << efs::FileKind::All << false << false;
    QTest::newRow("whitespace + All")
        << QStringLiteral(" \t ") << efs::FileKind::All << false << false;
    QTest::newRow("text + All") << QStringLiteral("a") << efs::FileKind::All << false << true;
    QTest::newRow("empty + Image") << QString() << efs::FileKind::Image << false << true;
    QTest::newRow("empty + Video") << QString() << efs::FileKind::Video << false << true;
    QTest::newRow("empty + Audio") << QString() << efs::FileKind::Audio << false << true;
    QTest::newRow("empty + Document") << QString() << efs::FileKind::Document << false << true;
    QTest::newRow("empty + Directory") << QString() << efs::FileKind::Directory << false << true;
    QTest::newRow("text + Image") << QStringLiteral("a") << efs::FileKind::Image << false << true;

    // Regex ON では空白もパターンの一部。空文字だけが「条件なし」。
    QTest::newRow("regex + empty + All") << QString() << efs::FileKind::All << true << false;
    QTest::newRow("regex + one space + All")
        << QStringLiteral(" ") << efs::FileKind::All << true << true;
    QTest::newRow("regex + multiple spaces + All")
        << QStringLiteral("   ") << efs::FileKind::All << true << true;
    QTest::newRow("regex + tab + All")
        << QStringLiteral("\t") << efs::FileKind::All << true << true;
    QTest::newRow("regex + leading space + All")
        << QStringLiteral(" a") << efs::FileKind::All << true << true;
    QTest::newRow("regex + empty + Image") << QString() << efs::FileKind::Image << true << true;
}

void TestSearchController::hasSearchConstraint()
{
    QFETCH(QString, text);
    QFETCH(efs::FileKind, kind);
    QFETCH(bool, regex);
    QFETCH(bool, expected);

    efs::SearchQuery query;
    query.text = text;
    query.kind = kind;
    query.regex = regex;
    QCOMPARE(efs::hasSearchConstraint(query), expected);
}

// テキストが空でも種別フィルタだけで検索する (Phase 1 の判定をそのまま流用
// してはならない、という持ち越し事項)。
void TestSearchController::emptyTextWithFileKindSearches_data()
{
    QTest::addColumn<efs::FileKind>("kind");

    QTest::newRow("Image") << efs::FileKind::Image;
    QTest::newRow("Video") << efs::FileKind::Video;
    QTest::newRow("Audio") << efs::FileKind::Audio;
    QTest::newRow("Document") << efs::FileKind::Document;
    QTest::newRow("Directory") << efs::FileKind::Directory;
}

void TestSearchController::emptyTextWithFileKindSearches()
{
    QFETCH(efs::FileKind, kind);

    auto backend = std::make_unique<GatedFakeBackend>();
    GatedFakeBackend* fake = backend.get();
    fake->release(16);

    // debounce が発火しえない長さにする。検索が走ったなら即時経路しかない。
    constexpr int kNeverFiresMs = 600000;
    efs::SearchController controller(std::move(backend), kNeverFiresMs);
    QSignalSpy clearedSpy(&controller, &efs::SearchController::cleared);

    controller.setKind(kind);

    QTRY_COMPARE(fake->queries().size(), 1);
    const efs::SearchQuery query = fake->queries().at(0);
    QCOMPARE(query.kind, kind);
    QVERIFY(query.text.isEmpty());
    QCOMPARE(clearedSpy.count(), 0);
}

void TestSearchController::returningToAllWhileEmptyClears()
{
    auto backend = std::make_unique<GatedFakeBackend>();
    GatedFakeBackend* fake = backend.get();
    fake->release(16);

    constexpr int kNeverFiresMs = 600000;
    efs::SearchController controller(std::move(backend), kNeverFiresMs);
    QSignalSpy resultsSpy(&controller, &efs::SearchController::resultsReady);
    QSignalSpy clearedSpy(&controller, &efs::SearchController::cleared);

    controller.setKind(efs::FileKind::Image);
    QTRY_COMPARE(resultsSpy.count(), 1);

    // テキストが空のまま All へ戻すと、絞り込みが無くなるので結果を消す。
    controller.setKind(efs::FileKind::All);
    QCOMPARE(clearedSpy.count(), 1);
    QCOMPARE(fake->queries().size(), 1); // 追加の検索を発行しない

    // Image の結果が遅れて届いても採用されない (世代が進んでいる)。
    QTest::qWait(50);
    QCOMPARE(resultsSpy.count(), 1);
}

void TestSearchController::kindChangeSearchesImmediately()
{
    auto backend = std::make_unique<GatedFakeBackend>();
    GatedFakeBackend* fake = backend.get();
    fake->release(16);

    constexpr int kNeverFiresMs = 600000;
    efs::SearchController controller(std::move(backend), kNeverFiresMs);

    controller.setText(QStringLiteral("report")); // debounce 待ちのまま
    QCOMPARE(fake->queries().size(), 0);

    controller.setKind(efs::FileKind::Document); // 即時。待機中の入力も畳み込む
    QTRY_COMPARE(fake->queries().size(), 1);
    QCOMPARE(fake->queries().at(0).kind, efs::FileKind::Document);
    QCOMPARE(fake->queries().at(0).text, QStringLiteral("report"));
    QCOMPARE(controller.kind(), efs::FileKind::Document);
}

void TestSearchController::regexChangeSearchesImmediately()
{
    auto backend = std::make_unique<GatedFakeBackend>();
    GatedFakeBackend* fake = backend.get();
    fake->release(16);

    constexpr int kNeverFiresMs = 600000;
    efs::SearchController controller(std::move(backend), kNeverFiresMs);

    controller.setText(QStringLiteral("^IMG \\d+"));
    controller.searchNow();
    QTRY_COMPARE(fake->queries().size(), 1);
    QCOMPARE(fake->queries().at(0).regex, false);

    controller.setRegex(true);
    QTRY_COMPARE(fake->queries().size(), 2);
    QCOMPARE(fake->queries().at(1).regex, true);
    QCOMPARE(fake->queries().at(1).text, QStringLiteral("^IMG \\d+"));
    QVERIFY(controller.regex());

    controller.setRegex(false);
    QTRY_COMPARE(fake->queries().size(), 3);
    QCOMPARE(fake->queries().at(2).regex, false);
}

// ソートは必ず backend へ渡す。5,000 行の局所ソートは行わない (計画 7)。
void TestSearchController::sortChangePropagatesImmediately()
{
    auto backend = std::make_unique<GatedFakeBackend>();
    GatedFakeBackend* fake = backend.get();
    fake->release(16);

    constexpr int kNeverFiresMs = 600000;
    efs::SearchController controller(std::move(backend), kNeverFiresMs);

    controller.setText(QStringLiteral("a"));
    controller.searchNow();
    QTRY_COMPARE(fake->queries().size(), 1);
    // 既定は Name 昇順。
    QCOMPARE(fake->queries().at(0).sortKey, efs::SortKey::Name);
    QCOMPARE(fake->queries().at(0).sortOrder, efs::SortOrder::Asc);

    struct Step {
        efs::SortKey key;
        efs::SortOrder order;
    };
    const QList<Step> steps{
        {efs::SortKey::Name, efs::SortOrder::Desc}, // 同じ列の Asc/Desc 反転
        {efs::SortKey::Path, efs::SortOrder::Asc},  // 別の列
        {efs::SortKey::Size, efs::SortOrder::Desc},
        {efs::SortKey::DateModified, efs::SortOrder::Desc},
    };

    int expected = 1;
    for (const Step& step : steps) {
        controller.setSort(step.key, step.order);
        ++expected;
        QTRY_COMPARE(fake->queries().size(), expected);
        QCOMPARE(fake->queries().last().sortKey, step.key);
        QCOMPARE(fake->queries().last().sortOrder, step.order);
        QCOMPARE(controller.sortKey(), step.key);
        QCOMPARE(controller.sortOrder(), step.order);
    }
}

void TestSearchController::redundantStateChangesIssueNoQuery()
{
    auto backend = std::make_unique<GatedFakeBackend>();
    GatedFakeBackend* fake = backend.get();
    fake->release(16);

    constexpr int kDebounceMs = 20;
    efs::SearchController controller(std::move(backend), kDebounceMs);

    // 1 本ずつ完了させてから次の状態を変える。連続で変えると (正しい挙動として)
    // 途中の要求が worker 側で stale 破棄され、本数で判定できなくなるため。
    controller.setText(QStringLiteral("a"));
    QTRY_COMPARE(fake->queries().size(), 1);
    controller.setKind(efs::FileKind::Image);
    QTRY_COMPARE(fake->queries().size(), 2);
    controller.setRegex(true);
    QTRY_COMPARE(fake->queries().size(), 3);
    controller.setSort(efs::SortKey::Size, efs::SortOrder::Desc);
    QTRY_COMPARE(fake->queries().size(), 4);

    // 同じ値の再設定では 1 本も発行しない。
    controller.setText(QStringLiteral("a"));
    controller.setKind(efs::FileKind::Image);
    controller.setRegex(true);
    controller.setSort(efs::SortKey::Size, efs::SortOrder::Desc);

    QTest::qWait(4 * kDebounceMs);
    QCOMPARE(fake->queries().size(), 4);
}

// 空白だけの入力は、Regex OFF では検索条件にならず、Regex ON では有効な
// パターン (「名前に空白を含む」) になる。SearchController がこの契約どおりに
// 振る舞うこと。
void TestSearchController::whitespaceOnlyTextSearchesOnlyWithRegex()
{
    auto backend = std::make_unique<GatedFakeBackend>();
    GatedFakeBackend* fake = backend.get();
    fake->release(16);

    constexpr int kDebounceMs = 20;
    efs::SearchController controller(std::move(backend), kDebounceMs);
    QSignalSpy clearedSpy(&controller, &efs::SearchController::cleared);

    // Regex OFF: 空白だけでは検索しない。
    controller.setText(QStringLiteral(" "));
    QTest::qWait(4 * kDebounceMs);
    QCOMPARE(fake->queries().size(), 0);
    QCOMPARE(clearedSpy.count(), 1);

    // Regex ON にした時点で、同じ空白だけの入力が有効な検索条件になる。
    controller.setRegex(true);
    QTRY_COMPARE(fake->queries().size(), 1);
    QCOMPARE(fake->queries().at(0).text, QStringLiteral(" "));
    QVERIFY(fake->queries().at(0).regex);

    // Regex を戻すと再び「条件なし」。
    controller.setRegex(false);
    QTRY_COMPARE(clearedSpy.count(), 2);
    QCOMPARE(fake->queries().size(), 1);
}

// 状態変更の経路が増えても stale 破棄が成立すること。実行中の検索は
// 変更の時点で古くなり、その結果は UI へ流れない。
void TestSearchController::stateChangeInvalidatesRunningSearch_data()
{
    QTest::addColumn<int>("change");

    QTest::newRow("kind") << 0;
    QTest::newRow("regex") << 1;
    QTest::newRow("sort") << 2;
}

void TestSearchController::stateChangeInvalidatesRunningSearch()
{
    QFETCH(int, change);

    auto backend = std::make_unique<GatedFakeBackend>();
    GatedFakeBackend* fake = backend.get();

    efs::SearchController controller(std::move(backend));
    QSignalSpy spy(&controller, &efs::SearchController::resultsReady);

    controller.setText(QStringLiteral("a"));
    controller.searchNow();
    QVERIFY(fake->waitStarted()); // worker の事前チェックは通過済み

    switch (change) {
    case 0:
        controller.setKind(efs::FileKind::Image);
        break;
    case 1:
        controller.setRegex(true);
        break;
    default:
        controller.setSort(efs::SortKey::Size, efs::SortOrder::Desc);
        break;
    }

    fake->release();              // 1 本目が完了 → UI 側で破棄されること
    QVERIFY(fake->waitStarted()); // 2 本目が実行に入る
    fake->release();

    QTRY_COMPARE(spy.count(), 1);
    QTest::qWait(50);
    QCOMPARE(spy.count(), 1);

    const QList<efs::SearchQuery> queries = fake->queries();
    QCOMPARE(queries.size(), 2);
    // 採用されたのは状態変更後の検索の結果だけ。
    QCOMPARE(resultsFrom(spy, 0).id, queries.at(1).id);
    QVERIFY(queries.at(1).id > queries.at(0).id);
}

QTEST_GUILESS_MAIN(TestSearchController)
#include "test_search_controller.moc"
