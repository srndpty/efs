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

    if (m_query.text.trimmed().isEmpty()) {
        // 空入力では検索しない。世代だけ進めて、実行中/キュー内の結果を無効化する。
        m_debounce->stop();
        startNewGeneration();
        emit cleared();
        return;
    }

    m_debounce->start();
}

void SearchController::searchNow()
{
    dispatch();
}

void SearchController::dispatch()
{
    m_debounce->stop();

    if (m_query.text.trimmed().isEmpty()) {
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
