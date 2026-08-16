// UI スレッド側の検索の入口 (計画 4)。
//
// 検索スレッドの生成・所有、デバウンス、request id の採番、stale 結果の破棄を
// ここ 1 箇所に閉じ込める。UI (MainWindow) は setText / searchNow を呼び、
// resultsReady / cleared を受けるだけでよい。
#pragma once

#include "core/ISearchBackend.h"
#include "core/SearchTypes.h"

#include <QObject>
#include <QString>

#include <memory>

class QThread;
class QTimer;

namespace efs {

class SearchWorker;

class SearchController : public QObject {
    Q_OBJECT

public:
    // 通常入力のデバウンス既定値。
    static constexpr int kDefaultDebounceMs = 120;

    explicit SearchController(std::unique_ptr<ISearchBackend> backend,
                              int debounceMs = kDefaultDebounceMs, QObject* parent = nullptr);
    ~SearchController() override;

    SearchController(const SearchController&) = delete;
    SearchController& operator=(const SearchController&) = delete;
    SearchController(SearchController&&) = delete;
    SearchController& operator=(SearchController&&) = delete;

    // 入力のたびに呼ぶ。デバウンス後に 1 回だけ検索する。
    void setText(const QString& text);
    // Enter。デバウンスを待たずに即時検索する。
    void searchNow();

signals:
    // 最新の request id の結果だけが流れる。
    void resultsReady(const efs::SearchResults& results);
    // 検索文字列が空になった。UI は結果を空にする。
    void cleared();
    // 検索スレッドへの要求 (queued)。外から emit しない。
    void runSearchRequested(const efs::SearchQuery& query);

private:
    void dispatch();
    void onResultsReady(const efs::SearchResults& results);
    // 新しい世代 id を採番し、UI 側と worker 側の「最新」を同時に更新する。
    // これ以前に発行された検索の結果は、以後どちらの段でも採用されない。
    quint64 startNewGeneration();

    QThread* m_thread = nullptr;
    SearchWorker* m_worker = nullptr;
    QTimer* m_debounce = nullptr;
    SearchQuery m_query;
    quint64 m_nextId = 0;
    quint64 m_latestRequestId = 0;
};

} // namespace efs
