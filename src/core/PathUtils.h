// パス区切りにまつわる純粋関数 (計画 8 / Phase 2)。
//
// ResultRow → フルパスの連結 (ファイルを開く / Explorer で選択 / クリップボードへ
// コピー のすべてが同じ文字列を必要とする) と、検索テキストの `/` → `\` 正規化。
// どちらも「Windows のパス区切りは `\` である」という一つの事情から来るので、
// UI や backend へ散らさずここ 1 箇所に寄せる。
//
// Qt Core 以外に依存しない純粋関数。Everything SDK も Widgets も持ち込まない。
#pragma once

#include "core/SearchTypes.h"

#include <QString>

namespace efs {

// path と name を Windows のフルパスへ連結する。
//
// Everything 1.4.1 が返す形を実測して決めた規則 (README の Phase 2 検証結果):
//   通常のファイル      path="C:\dev\soft"  name="a.txt" → "C:\dev\soft\a.txt"
//   ドライブ直下        path="C:"           name="a.txt" → "C:\a.txt"
//     ("C:" + "a.txt" のように区切りを省くと、ドライブ相対パスという
//      まったく別の意味になってしまう)
//   ドライブそのもの    path=""             name="C:"    → "C:\"
//     (裸の "C:" は「C: ドライブのカレントディレクトリ」を指す相対パス)
[[nodiscard]] QString fullPath(const ResultRow& row);

// 検索テキスト中の `/` を `\` へ揃える (Regex OFF 専用)。
//
// Everything がパス区切りとして見るのは `\` だけで、`path/to/file.txt` は
// そのままだと 1 つの名前として扱われて何も当たらない。Windows のファイル名に
// `/` は入れられないので、パス区切りとして書かれたものと見てよい。
//
// ただし **Everything の `foo:` 構文の値は opaque として一切変換しない**。
// `dm:2024/01/01` の `/` は日付の区切りでパス区切りではなく、
// `content:"https://..."` のように `/` そのものが意味を持つ値もあるため。
// 関数 (`dm:`) と修飾子 (`path:`、`nopath:`) の区別はしない — どちらも値の意味論を
// efs 側で再実装しない点は同じなので、`<2 文字以上の名前>:` で始まる項をまとめて
// 対象外にする (MatchHighlight の `foo:` 構文判定と同じ見分け方)。`C:` は 1 文字
// なのでドライブ付きパス (`C:/dev/soft`) は変換対象に残る。
//
// 値は引用できる (`parent:"C:/Program Files/Common Files"`) ので、項の切れ目は
// 引用の外側でだけ数える。この結果 `path:C:/dev` のような advanced な構文の中は
// `/` のまま残るが、これは意図した制限。
//
// Regex ON では呼ばない。正規表現では `/` はただの文字で、`\` はエスケープなので
// 入れ替えるとパターンの意味が変わる (「ユーザーのパターンを一字も変えない」契約)。
[[nodiscard]] QString normalizeQuerySeparators(const QString& text);

} // namespace efs
