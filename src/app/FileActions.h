// 結果行に対するファイル操作 (計画 7 / F8)。
//
// Phase 2 の範囲は Open / Show in Explorer だけ。rename / delete / move / copy /
// 複数選択の一括処理は Phase 3 以降なので作らない。
//
// **cmd.exe や PowerShell を経由したコマンド文字列を組み立てない。** パスに空白・
// 日本語・`,` `&` `(` `)` が入っても quoting 事故が起きないよう、Win32 / Qt の
// API へパスを引数として直接渡す。Windows 固有の処理はこの 2 関数に閉じ込める。
#pragma once

#include <QString>

namespace efs {

// 既定のアプリケーションで開く (ディレクトリならエクスプローラで開く)。
[[nodiscard]] bool openPath(const QString& fullPath);

// エクスプローラで親フォルダを開き、対象を選択状態にする。
[[nodiscard]] bool revealInExplorer(const QString& fullPath);

} // namespace efs
