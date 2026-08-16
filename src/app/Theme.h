// テーマ (計画 7 / F7)。
//
// Phase 3 で Dark 固定から System / Dark / Light の 3 択へ広げた。
// **既定は Dark のまま** — 標準のダークモードが無いことがこのアプリを作った
// 理由の 1 つなので、初回起動の見た目は変えない。
//
// テーマ適用の責務はここ (Theme.cpp) に集約する。MainWindow の各 widget が
// 個別に色を持ち始めないこと。Windows 固有処理 (タイトルバー) もここへ閉じ込める。
#pragma once

class QApplication;
class QWidget;

namespace efs {

enum class ThemeMode { System, Dark, Light };

// System を OS の現在の配色へ解決する。Phase 3 では OS 側の変更を実行中に
// 追従するところまではやらない (起動時と明示選択時に正しければよい)。
[[nodiscard]] bool resolveDark(ThemeMode mode);

// Fusion スタイル + palette を適用する。巨大な application stylesheet で全
// widget を個別指定はしない。Fusion が palette だけでは表現できない数点
// (チェック済みツールバーボタン、ヘッダ、ツールチップの枠) にだけ小さな QSS を
// 当てる。
//
// stylesheet は毎回まるごと差し替える (setStyleSheet)。何度切り替えても
// 累積しない。
void applyTheme(QApplication& app, ThemeMode mode);

// Windows のタイトルバーの明暗。Qt の標準機構では届かない範囲なので、DWM を
// 叩く Windows 固有処理をこの関数 1 つへ閉じ込める。
// ウィンドウが生成された後 (show() の前後どちらでも可) に呼ぶこと。
void applyTitleBarTheme(QWidget* window, bool dark);

} // namespace efs
