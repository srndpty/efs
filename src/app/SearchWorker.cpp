#include "app/SearchWorker.h"

namespace efs {

SearchWorker::SearchWorker(std::unique_ptr<ISearchBackend> backend) : m_backend(std::move(backend))
{}

void SearchWorker::setLatestRequestId(quint64 id)
{
    m_latestRequestId.store(id, std::memory_order_relaxed);
}

void SearchWorker::runSearch(const efs::SearchQuery& query)
{
    // キューに溜まった古い要求を実行前に捨てる。実行中のクエリを中断する機構は
    // 作らない (Everything の IPC クエリは中断できず、1 クエリは数ms〜数十ms)。
    if (query.id != m_latestRequestId.load(std::memory_order_relaxed))
        return;

    emit resultsReady(m_backend->search(query));
}

} // namespace efs
