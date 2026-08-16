// SearchQuery → Everything 1.4 の検索文字列 (計画 6.2)。
//
// Qt 以外に依存しない純粋関数。仕様の中心であり、単体テストの主戦場。
// Everything SDK には依存しないので Everything.h は不要。
#pragma once

#include "core/SearchTypes.h"

#include <QString>

namespace efs {

// 組み立ての規則 (Phase 0 で実機確定):
//   [種別項] [テキスト項]        ← 空白区切りは Everything では AND
//   種別項  : Directory は "folder:"、その他は "ext:jpg;jpeg;..."、All は無し
//   テキスト項: regex なら "regex:<入力>"、そうでなければ入力そのまま
//
// Everything_SetRegex は常に FALSE のまま使う。グローバルフラグは検索文字列
// **全体**を正規表現として解釈するため ext: 前置詞と併用できない (README の
// Phase 0 検証結果を参照)。「拡張子を正規表現へ畳み込むフォールバック」は
// 不要であり、実装してはならない。
//
// ユーザー入力は前後の空白を除く以外そのまま渡す。Everything の検索構文
// (空白 = AND、"..." による引用、! による否定) をそのまま使えることを狙った
// 意図的な設計。エスケープや引用符の補完は行わない。
[[nodiscard]] QString buildQueryString(const SearchQuery& query);

} // namespace efs
