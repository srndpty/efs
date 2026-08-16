// SearchQuery → Everything 1.4 の検索文字列 (計画 6.2)。
//
// Qt 以外に依存しない純粋関数。仕様の中心であり、単体テストの主戦場。
// Everything SDK には依存しないので Everything.h は不要。
#pragma once

#include "core/SearchTypes.h"

#include <QString>

namespace efs {

// 組み立ての規則 (Phase 0 / Phase 2 で実機確定):
//   [種別項] [テキスト項]        ← 空白区切りは Everything では AND
//   種別項  : Directory は "folder:"、その他は "ext:jpg;jpeg;..."、All は無し
//   テキスト項: regex なら "regex:\"<入力>\""
//              regex でなく種別項があるなら "<入力>"  (下の「種別を hard
//              constraint にする」を参照)
//              regex でなく種別項も無いなら入力そのまま
//
// **種別フィルタは hard constraint とする。** 種別項があるとき、Regex OFF の
// ユーザー入力は Everything のグルーピング `<...>` で囲む。Everything は演算子の
// 優先順位を設定で変更できるため、`ext:... a|b` のまま渡すと種別項が OR の片側
// からしか掛からない可能性がある。ツールバーで選んだ種別が結果に必ず効くことを
// 優先する。実測では現在の既定設定において `<>` の有無で結果は変わらず、
// AND / OR / 否定 / 引用 / ワイルドカード / 他の修飾子 / 不均衡な `<` `>` を
// 含む入力でも同一の結果だった (README の Phase 2 検証結果)。
// Regex 項は引用済みの 1 項なので囲まない。
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
// 空白の扱いは Regex の ON/OFF で異なる。`core/SearchTypes.h` の
// hasSearchConstraint() と同じ契約であること。
//   Regex OFF — 前後の空白は Everything の項区切りでしかないので trim する。
//               それ以外は入力をそのまま渡す。Everything の検索構文
//               (空白 = AND、"..." による引用、! による否定) をそのまま使える
//               ことを狙った意図的な設計。エスケープの補完は行わない。
//   Regex ON  — **ユーザーのパターンを一字も変えない。** 引用の内側では前後の
//               空白も TAB も意味を持つため、trim すると別のパターンになる。
//               空白だけのパターン (" " = 名前に空白を含む) も有効な検索条件。
//
// Regex ON でも補うのは外側の引用符だけで、パターン内部のエスケープはしない。
// パターン自体が `"` を含む場合は項が壊れるが、NTFS のファイル名に `"` は
// 入れられないので実用上意味のあるパターンではない。実測でも crash はせず、
// 単に一致しない/意図しない件数になるだけだった。多段のフォールバックは
// 書かない (AGENTS.md)。
[[nodiscard]] QString buildQueryString(const SearchQuery& query);

} // namespace efs
