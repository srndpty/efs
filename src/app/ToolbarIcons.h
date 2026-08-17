// フィルタツールバー用の小さなアイコン (計画 F3)。
//
// 画像アセット / qrc / Qt Svg を持ち込まず、QPainter で単純な図形として描く。
// 目的は「ラベルだけでなく形でも区別が付く」ところまでで、アイコンテーマ機構は
// 作らない。**結果行ごとのファイルアイコンはここには含めない** — あれは Windows
// の shell から引くもので、app/IconCache.* と app/ShellIcon.* が担当する。
#pragma once

#include "core/SearchTypes.h"

class QColor;
class QIcon;
class QPainter;

namespace efs {

// ink は描線の色。**テーマごとに呼び直す**こと — 色を焼き込んだアイコンを
// 使い回すと、Light テーマで薄い地に薄い線が乗って見えなくなる
// (Phase 3 の Light 対応で実際に起きた)。呼び出し側は palette の
// ButtonText を渡す。
[[nodiscard]] QIcon kindIcon(FileKind kind, const QColor& ink);
[[nodiscard]] QIcon regexIcon(const QColor& ink);
// テーマ切替メニュー用 (Phase 3)。明暗の対比そのものを図形にする。
[[nodiscard]] QIcon themeIcon(const QColor& ink);

// ウィンドウ / タスクトレイ用のアプリアイコン (Phase 4)。
// トレイの地は OS のテーマ次第で明暗どちらもありうるので、単色の線画ではなく
// 塗りつぶした円の上に描いて、どちらの地でも視認できるようにする。
[[nodiscard]] QIcon appIcon();

// アプリアイコンの図形の**唯一の定義**。size × size の正方形へ描く。
// 実行時の appIcon() と、Explorer 用に exe へ埋め込む .ico (tools/make_app_icon.cpp
// がビルド時に生成する) の両方がここを通る。**片方だけ描き変えないこと。**
// QPixmap を作らないので QGuiApplication 無しでも呼べる (生成ツールがそうする)。
void paintAppIcon(QPainter& painter, int size);

} // namespace efs
