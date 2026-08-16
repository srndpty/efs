// 表示用の文字列整形。UI から切り出して単体テスト可能にするためのもの。
//
// 返す文字列はすべてエンドユーザーが読むもの = 英語 (AGENTS.md 言語ポリシー)。
#pragma once

#include "core/SearchTypes.h"

#include <QDateTime>
#include <QString>

namespace efs {

// 負値 (ディレクトリ・不明) は空文字。それ以外は QLocale の単位付き表記。
[[nodiscard]] QString formatSize(qint64 size);

// 無効な QDateTime は "-"。
[[nodiscard]] QString formatModified(const QDateTime& modified);

// ステータスバー 1 行。エラーがあればそれをそのまま返す。
//   通常   : "1,234 results / 18 ms"
//   打ち切り: "1,234,567 results (showing first 5,000) / 18 ms"
[[nodiscard]] QString formatStatus(const SearchResults& results);

} // namespace efs
