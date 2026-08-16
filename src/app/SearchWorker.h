// 検索スレッド側のオブジェクト (計画 4)。
//
// backend を所有し、同期 search() を呼ぶだけ。Everything SDK はグローバル状態を
// 持つため、SDK に触るのはこのオブジェクトが載っている 1 本のスレッドだけに
// 直列化する。スレッドプール化は禁止。
#pragma once

#include "core/ISearchBackend.h"
#include "core/SearchTypes.h"

#include <QObject>

#include <atomic>
#include <memory>

namespace efs {

class SearchWorker : public QObject {
    Q_OBJECT

public:
    explicit SearchWorker(std::unique_ptr<ISearchBackend> backend);

    // UI スレッドから直接呼ぶ。UI スレッドと検索スレッドが共有する唯一の
    // 状態であり、atomic 1 個で足りるので cancellation の仕組みは作らない。
    void setLatestRequestId(quint64 id);

    // SearchController から Qt::QueuedConnection で呼ばれる = 検索スレッド上で
    // 実行される。新シグナル/スロット構文では普通のメンバ関数で接続できるので
    // slots マクロは付けない。
    void runSearch(const efs::SearchQuery& query);

signals:
    void resultsReady(const efs::SearchResults& results);

private:
    std::unique_ptr<ISearchBackend> m_backend;
    std::atomic<quint64> m_latestRequestId{0};
};

} // namespace efs
