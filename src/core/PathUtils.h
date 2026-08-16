// ResultRow → フルパス (計画 8 / Phase 2)。
//
// ファイルを開く / Explorer で選択 / クリップボードへコピー のすべてが同じ
// 文字列を必要とする。同じ連結を UI 側へ散らさず、ここ 1 箇所に寄せる。
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

} // namespace efs
