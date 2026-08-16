// ダークテーマ (計画 7 / F7)。
//
// Phase 2 は **Dark 固定**。テーマ切替 UI / Light / System / 設定の永続化は
// Phase 3 以降なので、ここでは ThemeMode 列挙も切替関数も作らない。
#pragma once

class QApplication;
class QWidget;

namespace efs {

// Fusion スタイル + 暗い QPalette を適用する。巨大な application stylesheet で
// 全 widget を個別指定はしない。Fusion が palette だけでは表現できない
// 数点 (チェック済みツールバーボタン、ヘッダ、ツールチップの枠) にだけ
// 小さな QSS を当てる。
void applyDarkTheme(QApplication& app);

// Windows のタイトルバーを暗くする。Qt の標準機構では届かない範囲なので、
// DWM を叩く Windows 固有処理をこの関数 1 つへ閉じ込める。
// ウィンドウが生成された後 (show() の前後どちらでも可) に呼ぶこと。
void applyDarkTitleBar(QWidget* window);

} // namespace efs
