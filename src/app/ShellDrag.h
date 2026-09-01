// 結果行を他アプリへドラッグする (Phase 6a / D&D)。
//
// **自前の IDataObject を組み立てない。** シェル自身のデータオブジェクトを
// 親フォルダから取り出して DoDragDrop へ渡す。こうすると受け側が Explorer から
// ドロップされたのと区別できないため、修飾キーの意味 (無印 = 移動 / Ctrl =
// コピー / Alt = ショートカット) も、フォルダを VS Code に落とすと開く・画像を
// ビューアに落とすと表示する、といった挙動もそのまま得られる。CF_HDROP だけを
// 自前で載せると CFSTR_SHELLIDLIST を見る受け側で挙動が変わる。
//
// Windows 固有処理はこのファイルに閉じ込める (FileActions.cpp / ShellIcon.cpp と
// 同じ扱い)。呼び出し側は QString のパスしか知らない。
#pragma once

#include <QString>

class QWidget;

namespace efs {

// 1 行をドラッグし、ドロップ (またはキャンセル) まで戻らない。
// **DoDragDrop は内部でメッセージループを回す**ので、この呼び出しの間に Qt の
// イベントも配送される。呼び出し側は戻った後にモデルが差し替わっている前提で
// 書くこと (パスは呼ぶ前に文字列として確保しておく)。
//
// **複数選択へ一般化しない。** 結果テーブルは SingleSelection であり、行の
// action と対象範囲を食い違わせないため。複数行を実際に扱うようになったときに
// 初めて配列を取る形へ広げる (シェル API 側は元から配列を取る)。
//
// 戻り値はドロップが成立したか。ドロップ後の実処理 (コピー / 移動 / ショート
// カット作成) は受け側とシェルの仕事であり、ここでは行わない。失敗は戻り値
// だけでは理由が分からないので、必ず qWarning に stage / path / HRESULT を残す。
bool startShellDrag(QWidget* source, const QString& fullPath);

} // namespace efs
