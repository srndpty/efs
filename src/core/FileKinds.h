// FileKind → 拡張子リスト (計画 2-(C))。
//
// backend 非依存の「仕様そのもの」。Everything 組み込みマクロ (audio: 等) は
// ユーザーの Filters.csv 依存で将来の native backend に移植できないため使わない。
// リストはソースにハードコードする (確定済みの意思決定。INI 化しない)。
#pragma once

#include "core/SearchTypes.h"

#include <QStringList>

namespace efs {

// 拡張子 (先頭のドット無し、小文字)。All と Directory は空リストを返す。
// Directory は拡張子ではなく folder: 項で表現するため。
[[nodiscard]] QStringList extensionsFor(FileKind kind);

} // namespace efs
