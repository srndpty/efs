// Windows のファイル種別アイコン取得 (Phase 3)。Win32 (shell) をここへ閉じ込める。
//
// **実ファイルには一切アクセスしない。** SHGFI_USEFILEATTRIBUTES を付け、
// 「拡張子 + ファイル属性」だけからアイコンを引く。結果行のパスを渡して
// しまうと 1 行ごとにディスクへ行くことになるため、渡してはならない。
//
// この方針の代償として .exe / .ico 等の「ファイル固有アイコン」は再現されず、
// 汎用の種別アイコンになる。Phase 3 では高速・安定であることを優先する。
#pragma once

#include "app/IconCache.h"

class QImage;

namespace efs {

// worker thread からも UI スレッドからも呼べる (COM はスレッドごとに初期化する)。
// 取得できなければ null QImage。
[[nodiscard]] QImage shellIconImage(const IconKey& key);

} // namespace efs
