// グローバルホットキーの表記と解釈 (Phase 4)。
//
// INI へは "Ctrl+Alt+E" のような**文字列**で保存する (int の生値にしない)。
// ここは Qt Core だけに依存する純粋関数にしてあり、Win32 の MOD_* / 仮想キー
// コードへの写像は app/GlobalHotkey.cpp が持つ — この分離のおかげで解釈の
// テストが GUI 無しで書ける。
//
// 対応キーは意図的に絞ってある (A-Z / 0-9 / F1-F24 / Space)。ホットキーに使える
// 必要が実際に出ていないキーまで表を広げない。
#pragma once

#include <QString>
#include <QtGlobal>

namespace efs {

// Win32 の MOD_* とは独立した値。写像は GlobalHotkey.cpp の 1 箇所だけ。
enum HotkeyModifier : quint32 {
    HotkeyModAlt = 0x1,
    HotkeyModControl = 0x2,
    HotkeyModShift = 0x4,
    HotkeyModMeta = 0x8, // Windows キー
};

struct HotkeySpec {
    quint32 modifiers = 0;
    // Qt::Key の値。Win32 の仮想キーコードへは GlobalHotkey.cpp で写像する。
    quint32 key = 0;

    // 修飾キーが 1 つ以上あり、かつ対応キーが 1 つあるものだけを有効とする。
    // 修飾なしのホットキー (例: "E" 単独) は OS 全体を奪ってしまうので拒否する。
    [[nodiscard]] bool isValid() const { return modifiers != 0 && key != 0; }
};

// 解釈できなければ既定値 (invalid) を返す。**入力は書き換えない。**
[[nodiscard]] HotkeySpec parseHotkey(const QString& text);

// 正規化した表記へ戻す。順序は必ず Ctrl+Alt+Shift+Meta+<key>。
// invalid なら空文字。
[[nodiscard]] QString formatHotkey(const HotkeySpec& spec);

// 既定のホットキー。Everything の慣習に合わせる。
[[nodiscard]] QString defaultHotkeyText();

} // namespace efs
