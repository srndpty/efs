// グローバルホットキー (Phase 4)。Win32 の RegisterHotKey をここへ閉じ込める。
//
// 常駐アプリの実質的な入口。ウィンドウが隠れていても OS から呼び出せる必要が
// あるので、Qt のショートカット (QAction) では届かない。
//
// HWND は使わず nullptr で登録する。WM_HOTKEY はスレッドのメッセージキューへ
// 直接届き、Qt の native event filter が拾える — ウィンドウの生成/破棄や
// 再作成に左右されない。
#pragma once

#include "app/HotkeySpec.h"

#include <QAbstractNativeEventFilter>
#include <QObject>
#include <QString>

namespace efs {

class GlobalHotkey : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT

public:
    explicit GlobalHotkey(QObject* parent = nullptr);
    ~GlobalHotkey() override;

    GlobalHotkey(const GlobalHotkey&) = delete;
    GlobalHotkey& operator=(const GlobalHotkey&) = delete;
    GlobalHotkey(GlobalHotkey&&) = delete;
    GlobalHotkey& operator=(GlobalHotkey&&) = delete;

    // 登録し直す。spec が invalid なら解除だけして true を返す (= ホットキー無効)。
    //
    // **失敗しても起動を止めない契約。** 他アプリが同じ組み合わせを既に握って
    // いることは普通に起こるので、false + reason を返すだけにし、呼び出し側は
    // 非モーダルに理由を出す。
    bool bind(const HotkeySpec& spec, QString* reason);
    void unbind();

    [[nodiscard]] bool isBound() const { return m_bound; }

signals:
    void activated();

protected:
    bool nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) override;

private:
    bool m_bound = false;
    bool m_filterInstalled = false;
};

} // namespace efs
