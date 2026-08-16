#include "app/SearchController.h"

#include "app/SearchWorker.h"

#include <QThread>
#include <QTimer>

namespace efs {

SearchController::SearchController(std::unique_ptr<ISearchBackend> backend, int debounceMs,
                                   QObject* parent)
    : QObject(parent)
{
    qRegisterMetaType<efs::SearchQuery>();
    qRegisterMetaType<efs::SearchResults>();

    m_thread = new QThread(this);
    m_thread->setObjectName(QStringLiteral("search"));

    m_worker = new SearchWorker(std::move(backend));
    m_worker->moveToThread(m_thread);
    // worker は検索スレッド上で破棄する。backend のデストラクタ (CleanUp +
    // FreeLibrary) を SDK に触ったのと同じスレッドで走らせるため。
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);

    connect(this, &SearchController::runSearchRequested, m_worker, &SearchWorker::runSearch);
    connect(m_worker, &SearchWorker::resultsReady, this, &SearchController::onResultsReady);

    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(debounceMs);
    connect(m_debounce, &QTimer::timeout, this, &SearchController::dispatch);

    m_thread->start();
}

SearchController::~SearchController()
{
    m_thread->quit();
    m_thread->wait();
}

void SearchController::setText(const QString& text)
{
    if (text == m_query.text)
        return;
    m_query.text = text;

    // 入力が変わった時点で世代を進め、実行中/キュー内の検索をただちに stale に
    // する。dispatch() まで待つと「debounce 待ちの間に古い検索が完了し、検索欄と
    // 食い違う結果が描画される」時間窓ができる。
    startNewGeneration();

    if (!hasSearchConstraint(m_query)) {
        // 絞り込みが何も無い状態では検索しない。世代を進めるだけで結果は捨てられる。
        m_debounce->stop();
        emit cleared();
        return;
    }

    m_debounce->start();
}

void SearchController::searchNow()
{
    dispatch();
}

void SearchController::setKind(FileKind kind)
{
    if (kind == m_query.kind)
        return;
    m_query.kind = kind;
    dispatch();
}

void SearchController::setRegex(bool regex)
{
    if (regex == m_query.regex)
        return;
    m_query.regex = regex;
    dispatch();
}

void SearchController::setSort(SortKey key, SortOrder order)
{
    if (key == m_query.sortKey && order == m_query.sortOrder)
        return;
    m_query.sortKey = key;
    m_query.sortOrder = order;
    dispatch();
}

void SearchController::restoreOptions(const SearchOptions& options)
{
    // 値を全部入れてから 1 回だけ dispatch する。個々の setter を呼ぶと
    // 復元だけで最大 3 本のクエリが飛ぶ。
    m_query.kind = options.kind;
    m_query.regex = options.regex;
    m_query.sortKey = options.sortKey;
    m_query.sortOrder = options.sortOrder;
    dispatch();
}

SearchOptions SearchController::options() const
{
    return {.kind = m_query.kind,
            .regex = m_query.regex,
            .sortKey = m_query.sortKey,
            .sortOrder = m_query.sortOrder};
}

void SearchController::dispatch()
{
    // 待機中のデバウンスは畳み込む。ここで新しい世代を採番するので、
    // 経路がどれであっても実行中/キュー内の検索はこの時点で stale になる。
    m_debounce->stop();

    if (!hasSearchConstraint(m_query)) {
        startNewGeneration();
        emit cleared();
        return;
    }

    SearchQuery query = m_query;
    query.id = startNewGeneration();
    emit runSearchRequested(query);
}

quint64 SearchController::startNewGeneration()
{
    m_latestRequestId = ++m_nextId;
    m_worker->setLatestRequestId(m_latestRequestId);
    return m_latestRequestId;
}

void SearchController::onResultsReady(const efs::SearchResults& results)
{
    // 実行中だったために worker 側で捨てられなかった古い結果は、ここで捨てる。
    if (results.id != m_latestRequestId)
        return;
    emit resultsReady(results);
}

} // namespace efs
