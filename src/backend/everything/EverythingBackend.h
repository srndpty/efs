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
// 無効を返すのは 0 (日時なし) と qint64 で表せない値だけ。1970 より前の日時は
// 負の Unix epoch time として正しく変換する。
// windows.h の型をヘッダに出さないため quint64 で受ける。
[[nodiscard]] QDateTime fileTimeToDateTime(quint64 fileTime);

class EverythingBackend final : public ISearchBackend {
public:
    EverythingBackend() = default;

    [[nodiscard]] QString name() const override;
    [[nodiscard]] bool isAvailable(QString* reason) const override;
    SearchResults search(const SearchQuery& query) override;

private:
    // 接続先の Everything の版を検証する。IPC が通ってからでないと版が取れない
    // (未起動だと 0 が返る) ので、最初に成功したクエリの直後に行う。
    // 対象外の版なら理由を返す (空 = 使ってよい)。
    //
    // **判定を latch するのは有効な版が取れたときだけ。** 版が取れなかった場合
    // (= IPC が通っていない) は transient として扱い、その検索だけ失敗させて
    // 次回やり直す。取れない状態を「対象外」として覚えると、Everything を
    // 起こし直しても永久に失敗し続ける。
    QString checkServerVersion();

    // DLL のロードは初回利用時 (= 検索スレッド上) まで遅らせる。const な
    // isAvailable() からも触るため mutable。load() は冪等。
    mutable EverythingApi m_api;
    // 版の検証は 1 度で足りる (プロセスの寿命の間に Everything が別の版へ
    // 入れ替わることは想定しない)。判定を覚えておき、対象外なら以後の検索も
    // すべて同じ理由で落とす。
    bool m_versionChecked = false;
    QString m_versionError;
};

} // namespace efs
