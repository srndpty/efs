// SearchQuery → Everything 1.4 の検索文字列 (計画 6.2)。
//
// Qt 以外に依存しない純粋関数。仕様の中心であり、単体テストの主戦場。
// Everything SDK には依存しないので Everything.h は不要。
#pragma once

#include "core/SearchTypes.h"

#include <QString>

namespace efs {

// 組み立ての規則 (Phase 0 / Phase 2 冒頭で実機確定):
//   [種別項] [テキスト項]        ← 空白区切りは Everything では AND
//   種別項  : Directory は "folder:"、その他は "ext:jpg;jpeg;..."、All は無し
//   テキスト項: regex なら "regex:\"<入力>\""、そうでなければ入力そのまま
//
// **Regex は必ず二重引用符で囲む (Phase 2 で実機確定)。** Everything の
// search-term parser は空白と TAB を項の区切りとして regex エンジンより先に
// 解釈するため、囲まないと `^IMG \d+` が `regex:^IMG` と `\d+` の 2 項へ割れる。
// 引用すると 1 項として渡り、パターン内の空白・TAB・`\` エスケープ・`[ ]`・
// 選択 `|`・量指定子 `{n}` がすべて意図どおり働く (README の Phase 2 検証結果)。
// 空白を含まないパターンでも引用して構わないことを実測したので、条件分岐は
// 設けず無条件に囲む。
//
// Everything_SetRegex は常に FALSE のまま使う。グローバルフラグは検索文字列
// **全体**を正規表現として解釈するため ext: 前置詞と併用できない (README の
// Phase 0 検証結果を参照)。「拡張子を正規表現へ畳み込むフォールバック」は
// 不要であり、実装してはならない。
//
// Regex OFF のとき、ユーザー入力は前後の空白を除く以外そのまま渡す。Everything
// の検索構文 (空白 = AND、"..." による引用、! による否定) をそのまま使えること
// を狙った意図的な設計。エスケープの補完は行わない。
//
// Regex ON でも補うのは外側の引用符だけで、パターン内部のエスケープはしない。
// パターン自体が `"` を含む場合は項が壊れるが、NTFS のファイル名に `"` は
// 入れられないので実用上意味のあるパターンではない。実測でも crash はせず、
// 単に一致しない/意図しない件数になるだけだった。多段のフォールバックは
// 書かない (AGENTS.md)。
[[nodiscard]] QString buildQueryString(const SearchQuery& query);

} // namespace efs
