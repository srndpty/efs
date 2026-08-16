// 検索バックエンドの抽象 (計画 6.4)。
//
// 意図的に**同期 API** にしてある。非同期化・デバウンス・stale 破棄は
// SearchController / SearchWorker の 1 箇所だけが持つ。backend 実装者は
// 「クエリを受けて結果を返す関数」を書くだけでよい。
//
// 契約:
//   - search() は例外を投げない。失敗は SearchResults::error に載せて返す。
//   - search() は検索スレッドからのみ呼ばれる。実装はスレッド安全でなくてよい。
#pragma once

#include "core/SearchTypes.h"

#include <QString>

namespace efs {

class ISearchBackend {
public:
    ISearchBackend() = default;
    virtual ~ISearchBackend() = default;

    ISearchBackend(const ISearchBackend&) = delete;
    ISearchBackend& operator=(const ISearchBackend&) = delete;
    ISearchBackend(ISearchBackend&&) = delete;
    ISearchBackend& operator=(ISearchBackend&&) = delete;

    [[nodiscard]] virtual QString name() const = 0;

    // 利用可能でなければ false を返し、reason (非 null なら) に理由を書く。
    [[nodiscard]] virtual bool isAvailable(QString* reason) const = 0;

    virtual SearchResults search(const SearchQuery& query) = 0;
};

} // namespace efs
