#include "app/GlobalHotkey.h"

#include <QCoreApplication>
#include <Qt>

#include <windows.h>

namespace efs {

namespace {

// このプロセス内で使うホットキー ID。1 本しか登録しないので固定でよい。
constexpr int kHotkeyId = 1;

// HotkeyModifier → Win32 MOD_*。写像はここ 1 箇所だけ。
UINT toWin32Modifiers(quint32 modifiers)
{
    UINT result = 0;
    if ((modifiers & HotkeyModAlt) != 0)
        result |= MOD_ALT;
    if ((modifiers & HotkeyModControl) != 0)
        result |= MOD_CONTROL;
    if ((modifiers & HotkeyModShift) != 0)
        result |= MOD_SHIFT;
    if ((modifiers & HotkeyModMeta) != 0)
        result |= MOD_WIN;
    // キーリピートで何度も前面化しないようにする。
    result |= MOD_NOREPEAT;
    return result;
}

// Qt::Key → Win32 仮想キーコード。HotkeySpec が受け付ける範囲だけを扱う。
UINT toVirtualKey(quint32 key)
{
    // A-Z / 0-9 は Qt::Key と VK が同じ値 (0x41-0x5A / 0x30-0x39)。
    if (key >= static_cast<quint32>(Qt::Key_A) && key <= static_cast<quint32>(Qt::Key_Z))
        return static_cast<UINT>(key);
    if (key >= static_cast<quint32>(Qt::Key_0) && key <= static_cast<quint32>(Qt::Key_9))
        return static_cast<UINT>(key);
    if (key == static_cast<quint32>(Qt::Key_Space))
        return VK_SPACE;
    if (key >= static_cast<quint32>(Qt::Key_F1) && key <= static_cast<quint32>(Qt::Key_F1) + 23)
        return VK_F1 + (key - static_cast<quint32>(Qt::Key_F1));
    return 0;
}

QString describeError(DWORD code)
{
    // ユーザーに出すのは「何が起きたか」だけ。code は診断用に添える。
    if (code == ERROR_HOTKEY_ALREADY_REGISTERED)
        return QStringLiteral("already in use by another application");
    return QStringLiteral("Win32 error %1").arg(code);
}

} // namespace

GlobalHotkey::GlobalHotkey(QObject* parent) : QObject(parent) {}

GlobalHotkey::~GlobalHotkey()
{
    unbind();
    if (m_filterInstalled)
        QCoreApplication::instance()->removeNativeEventFilter(this);
}

bool GlobalHotkey::bind(const HotkeySpec& spec, QString* reason)
{
    unbind();

    // 空 / 解釈できない指定は「ホットキー無効」。エラーではない。
    if (!spec.isValid())
        return true;

    const UINT virtualKey = toVirtualKey(spec.key);
    if (virtualKey == 0) {
        if (reason)
            *reason = QStringLiteral("unsupported key");
        return false;
    }

    // hwnd = nullptr。WM_HOTKEY はスレッドのメッセージキューへ届くので、
    // ウィンドウが隠れていても・作り直されても影響を受けない。
    if (::RegisterHotKey(nullptr, kHotkeyId, toWin32Modifiers(spec.modifiers), virtualKey) == 0) {
        if (reason)
            *reason = describeError(::GetLastError());
        return false;
    }

    if (!m_filterInstalled) {
        QCoreApplication::instance()->installNativeEventFilter(this);
        m_filterInstalled = true;
    }
    m_bound = true;
    return true;
}

void GlobalHotkey::unbind()
{
    if (!m_bound)
        return;
    ::UnregisterHotKey(nullptr, kHotkeyId);
    m_bound = false;
}

bool GlobalHotkey::nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result)
{
    Q_UNUSED(result)
    // Qt は Windows で 2 種類の eventType を投げる。ウィンドウ宛が
    // "windows_generic_MSG"、イベントディスパッチャ宛 (スレッドメッセージ) が
    // "windows_dispatcher_MSG"。**WM_HOTKEY は hwnd=nullptr で登録している以上、
    // どちらで来るかは Qt の内部実装次第なので両方受ける。** 片方に決め打つと、
    // Qt の版が変わった途端にホットキーが黙って効かなくなる。
    if (eventType != QByteArrayLiteral("windows_generic_MSG") &&
        eventType != QByteArrayLiteral("windows_dispatcher_MSG"))
        return false;

    const auto* msg = static_cast<const MSG*>(message);
    if (msg->message != WM_HOTKEY || msg->wParam != kHotkeyId)
        return false;

    // 実際にどちらで届いたかは 1 度だけ診断へ残す (UI には出さない)。
    static bool reported = false;
    if (!reported) {
        reported = true;
        qInfo("WM_HOTKEY の nativeEventFilter eventType: %s", eventType.constData());
    }

    emit activated();
    // 消費した。他のフィルタへは回さない。
    return true;
}

} // namespace efs
