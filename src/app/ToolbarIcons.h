// フィルタツールバー用の小さなアイコン (計画 F3)。
//
// 画像アセット / qrc / Qt Svg を持ち込まず、QPainter で単純な図形として描く。
// 目的は「ラベルだけでなく形でも区別が付く」ところまでで、アイコンテーマ機構は
// 作らない。**結果行ごとのファイルアイコン (QFileIconProvider / shell icon
// cache) は Phase 3 であり、ここには含めない。**
#pragma once

#include "core/SearchTypes.h"

class QIcon;

namespace efs {

[[nodiscard]] QIcon kindIcon(FileKind kind);
[[nodiscard]] QIcon regexIcon();

} // namespace efs
