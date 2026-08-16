// ISearchBackend の Everything 実装 (計画 6.3)。
//
// Everything.h を include するのは対応する .cpp のみ。このヘッダは
// EverythingApi.h 経由で windows.h を持ち込むが、SDK の型は公開しない。
#pragma once

#include "backend/everything/EverythingApi.h"
#include "core/ISearchBackend.h"

#include <QDateTime>

namespace efs {

// FILETIME (1601-01-01 UTC からの 100ns 単位) を現地時刻の QDateTime へ。
// 0 および Unix エポック未満は無効な QDateTime を返す。
// windows.h の型をヘッダに出さないため quint64 で受ける。
[[nodiscard]] QDateTime fileTimeToDateTime(quint64 fileTime);

class EverythingBackend final : public ISearchBackend {
public:
    EverythingBackend() = default;

    [[nodiscard]] QString name() const override;
    [[nodiscard]] bool isAvailable(QString* reason) const override;
    SearchResults search(const SearchQuery& query) override;

private:
    // DLL のロードは初回利用時 (= 検索スレッド上) まで遅らせる。const な
    // isAvailable() からも触るため mutable。load() は冪等。
    mutable EverythingApi m_api;
};

} // namespace efs
