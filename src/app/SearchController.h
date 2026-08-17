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

    // --- 明示操作による状態変更 -----------------------------------------------
    // いずれもユーザーが UI を 1 クリック/1 ショートカットで起こしたものなので
    // デバウンスせず即時に再検索する。同じ値の再設定では何も発行しない。
    // UI は SearchQuery を直接編集せず、必ずここを通す (検索状態の authority)。
    void setKind(FileKind kind);
    void setRegex(bool regex);
    void setSort(SortKey key, SortOrder order);

    // 起動時に永続化されたオプションをまとめて戻す (Phase 3 / F9)。
    //
    // setKind() / setRegex() / setSort() を順に呼ぶと 1 回の復元で最大 3 本の
    // クエリが backend へ飛ぶ。ここは 4 つの値を先に全部入れてから 1 回だけ
    // dispatch する。結果として発行されるクエリは高々 1 本 (検索欄は起動時に
    // 空なので、復元された kind が All なら 0 本 = cleared)。
    void restoreOptions(const SearchOptions& options);

    [[nodiscard]] SearchOptions options() const;

    [[nodiscard]] FileKind kind() const { return m_query.kind; }
    [[nodiscard]] bool regex() const { return m_query.regex; }
    [[nodiscard]] SortKey sortKey() const { return m_query.sortKey; }
    [[nodiscard]] SortOrder sortOrder() const { return m_query.sortOrder; }

signals:
    // 実際に backend へクエリを発行した。**同期で発火する** (dispatch の中)。
    //
    // 用途は「表示中の結果が現在の query と食い違っている」状態を潰すこと。
    // Everything の IPC クエリは中断できず、条件によっては 20 秒以上かかるため
    // (README の実測)、これが無いと検索欄は新しい query なのに古い結果が
    // 表示され、しかも操作できてしまう。cancel / timeout / fallback は追加せず、
    // 表示だけを fail-closed にする。
    //
    // 絞り込み条件が無いときはクエリを出さないので、この signal ではなく
    // 既存の cleared() が飛ぶ。
    void searchStarted();
    // 最新の request id の結果だけが流れる。
    void resultsReady(const efs::SearchResults& results);
    // 絞り込み条件が 1 つも無くなった (テキスト空 かつ FileKind::All)。
    // UI は結果を空にする。
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
